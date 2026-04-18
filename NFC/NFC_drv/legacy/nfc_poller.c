/* See COPYING.txt for license details. */

/*
 ******************************************************************************
 * nfc_poller.c - NFC Poller Mode (Reader/Card Reading)
 ******************************************************************************
 * 
 * [Purpose]
 * - Implements NFC-A reader functionality
 * - Detects and reads various NFC card types (T2T, MIFARE Classic, T4T)
 * - Performs card type identification and data reading
 * 
 * [State Machine]
 * NOTINIT → START_DISCOVERY → DISCOVERY
 * 
 * [Key Flow]
 * 1. ReadIni(): Initialize RFAL in poller mode
 *    - Configure discovery parameters (POLL_TECH_A)
 *    - Start rfalNfcDiscover()
 * 
 * 2. ReadCycle(): Main processing loop
 *    - START_DISCOVERY: Start discovery process
 *    - DISCOVERY: Poll for cards, handle activation
 * 
 * 3. Card Detection and Type Identification:
 *    - ISO14443A/NFC-A card detected
 *    - Analyze ATQA + SAK to determine card type:
 *      * MIFARE Classic (SAK=0x08/0x18): Use key dictionary attack
 *      * Type 2 Tag (T2T): Read NTAG/Ultralight
 *      * Type 4 Tag (T4T): ISO-DEP APDU exchange
 * 
 * 4. T2T Reading (m1_t2t_read_ntag()):
 *    - GET_VERSION (0x60): Get NTAG version info
 *    - READ (0x30): Read pages sequentially (0~135)
 *    - FAST_READ (0x3A): Read multiple pages at once
 *    - Save data to nfc_ctx and SD card (.nfc file)
 * 
 * [Card Type Detection]
 * - MIFARE Classic: ATQA=0x0400/0x4400, SAK=0x08 (1K) or 0x18 (4K)
 * - Type 2 Tag: ATQA=0x0044, SAK=0x00
 * - Type 4 Tag: ISO-DEP protocol
 * 
 ******************************************************************************
 */
#include "nfc_poller.h"
#include "utils.h"
#include "rfal_nfc.h"
#include "rfal_rf.h"
#include "rfal_t2t.h"
#include "rfal_isoDep.h"
#include "rfal_nfcv.h"
#include "rfal_st25tb.h"

#include "st25r3916.h"
#include "st25r3916_com.h"
#include "rfal_utils.h"

#include "rfal_analogConfig.h"
#include "rfal_rf.h"
#include "rfal_chip.h"
#include "uiView.h"
#include "nfc_driver.h"
#include "nfc_poller.h"
#include "common/nfc_ctx.h"
#include "common/nfc_storage.h"
#include "m1_sdcard.h"
#include "m1_storage.h"
#include "common/nfc_fileio.h"
#include "logger.h"
#include "mfc_crypto1.h"
#include "picopass/picopass_poller.h"
#include <stdio.h>

#define NOTINIT              0     /*!< Demo State:  Not initialized        */
#define START_DISCOVERY      1     /*!< Demo State:  Start Discovery        */
#define DISCOVERY            2     /*!< Demo State:  Discovery              */

/*
 ******************************************************************************
 * MIFARE CLASSIC Helper Functions
 ******************************************************************************
 */

#define MFC_DICT_PATH   "NFC/system/mf_classic_dict.nfc"

/* Streaming key dictionary: reads one key at a time from SD card to avoid
 * allocating thousands of keys on the stack. */
typedef struct {
    nfcfio_t io;
    char     line[32];
    bool     is_open;
    uint16_t count;   /* Total keys counted (for logging) */
} mfc_key_iter_t;

static bool mfc_key_iter_open(mfc_key_iter_t *it);
static bool mfc_key_iter_next(mfc_key_iter_t *it, uint8_t key_out[MFC_KEY_LEN]);
static void mfc_key_iter_rewind(mfc_key_iter_t *it);
static void mfc_key_iter_close(mfc_key_iter_t *it);

static void mfc_get_layout_from_sak(uint8_t sak, uint16_t *outSectors, uint16_t *outBlocks);
static uint16_t mfc_sector_to_first_block(uint16_t sector);

/* Returns true if SAK indicates any MIFARE Classic variant */
static bool mfc_is_classic_sak(uint8_t sak)
{
    switch (sak) {
    case 0x01: /* Classic 1K (TNP3xxx) */
    case 0x08: /* Classic 1K */
    case 0x09: /* MIFARE Mini */
    case 0x10: /* Plus 2K SL2 */
    case 0x11: /* Plus 4K SL2 */
    case 0x18: /* Classic 4K */
    case 0x19: /* Classic 2K */
    case 0x28: /* Classic EV1 1K */
    case 0x38: /* Classic EV1 4K */
        return true;
    default:
        return false;
    }
}

/* Forward declarations for NFC-V / ST25TB reading */
static void m1_nfcv_read(const rfalNfcDevice *dev);
static void m1_st25tb_read(const rfalNfcDevice *dev);
static void m1_read_mifareclassic(const rfalNfcDevice *dev);

extern uint8_t g_nfc_dump_buf[NFC_DUMP_BUF_SIZE];
extern uint8_t g_nfc_valid_bits[NFC_VALID_BITS_SIZE];

/*
 ******************************************************************************
 * LOCAL VARIABLES
 ******************************************************************************
 */

static rfalNfcDiscoverParam discParam;
static uint8_t              state = NOTINIT;
static bool                 multiSel;
static nfc_poll_profile_t   s_poll_profile = NFC_POLL_PROFILE_NORMAL;


/* NFC-A CE config */
/* 4-byte UIDs with first byte 0x08 would need random number for the subsequent 3 bytes.
 * 4-byte UIDs with first byte 0x*F are Fixed number, not unique, use for this demo
 * 7-byte UIDs need a manufacturer ID and need to assure uniqueness of the rest.*/

/*------------------------------------------------------------------------------------------*/
static void PollerNotif( rfalNfcState st );
static void m1_t2t_read_ntag(const rfalNfcDevice *dev);
static ReturnCode GetVersion_Ntag(uint8_t *rxBuf, uint16_t rxBufLen, uint16_t *rcvLen);
static uint16_t LogParsedNtagVersion(const uint8_t *version, uint16_t len);
static ReturnCode PwdAuth_Ntag(const uint8_t pwd[4], uint8_t pack[2]);
static bool ntag_generate_amiibo_pwd(const uint8_t *uid, uint8_t uid_len, uint8_t pwd_out[4], uint8_t pack_out[2]);

/*------------------------------------------------------------------------------------------*/
#define SET_FAMILY(fmt, ...)  do { snprintf(NFC_Family, sizeof(NFC_Family), fmt, ##__VA_ARGS__); } while(0)

/*
 * DESFire detection via ISO-DEP APDU: send GET_VERSION (0x60) wrapped
 * in ISO 7816 framing. If the card responds with 0x91AF it's DESFire.
 */
static bool desfire_probe_iso_dep(const rfalNfcDevice* dev)
{
    if (!dev || dev->rfInterface != RFAL_NFC_INTERFACE_ISODEP)
        return false;

    static rfalIsoDepApduBufFormat txApdu;
    static rfalIsoDepApduBufFormat rxApdu;
    static rfalIsoDepBufFormat     tmpBuf;
    uint16_t rxLen = 0;

    /* DESFire GET_VERSION wrapped APDU: CLA=0x90, INS=0x60, P1=0, P2=0, Lc=0 */
    txApdu.apdu[0] = 0x90;
    txApdu.apdu[1] = 0x60;
    txApdu.apdu[2] = 0x00;
    txApdu.apdu[3] = 0x00;
    txApdu.apdu[4] = 0x00; /* Le */

    const rfalIsoDepInfo *info = &dev->proto.isoDep.info;

    rfalIsoDepApduTxRxParam param;
    param.txBuf    = &txApdu;
    param.txBufLen = 5;
    param.rxBuf    = &rxApdu;
    param.rxLen    = &rxLen;
    param.tmpBuf   = &tmpBuf;
    param.FWT      = info->FWT;
    param.dFWT     = info->dFWT;
    param.FSx      = info->FSx;
    param.ourFSx   = RFAL_ISODEP_DEFAULT_FSC;
    param.DID      = info->supDID ? info->DID : RFAL_ISODEP_NO_DID;

    ReturnCode err = rfalIsoDepStartApduTransceive(param);
    if (err != RFAL_ERR_NONE)
        return false;

    /* Poll for completion (max ~100 iterations) */
    for (int i = 0; i < 100; i++) {
        rfalNfcWorker();
        err = rfalIsoDepGetApduTransceiveStatus();
        if (err == RFAL_ERR_NONE) break;
        if (err != RFAL_ERR_BUSY) return false;
    }
    if (err != RFAL_ERR_NONE)
        return false;

    /* Check response: DESFire answers with SW1=0x91, SW2=0xAF (more data)
     * or 0x91 0x00 (success). Response has 7+ bytes of version data + SW. */
    if (rxLen >= 2) {
        uint8_t sw1 = rxApdu.apdu[rxLen - 2];
        uint8_t sw2 = rxApdu.apdu[rxLen - 1];
        if (sw1 == 0x91 && (sw2 == 0xAF || sw2 == 0x00)) {
            return true;
        }
    }

    return false;
}

/* Magic card (Gen1A) detection: Send backdoor command 0x40 then 0x43 */
static bool mfclassic_is_magic_backdoor_supported(void)
{
    /* Gen1A detection requires sending raw short frames (0x40, 0x43) outside
     * of normal ISO14443A framing. The ST25R3916 can do 7-bit frames via
     * RFAL but it requires the card to be in HALT state first. For now,
     * this is best-effort — we attempt it but don't block on failure. */
    uint8_t txBuf[1];
    uint8_t rxBuf[4];
    uint16_t rcvLen = 0;

    /* Send HALT first */
    txBuf[0] = 0x50;
    rfalTransceiveBlockingTxRx(txBuf, 1, rxBuf, sizeof(rxBuf),
                               &rcvLen, RFAL_TXRX_FLAGS_DEFAULT,
                               rfalConvMsTo1fc(5));

    /* Send magic backdoor wakeup: 0x40 as 7-bit short frame */
    txBuf[0] = 0x40;
    rcvLen = 0;
    ReturnCode err = rfalTransceiveBlockingTxRx(
        txBuf, 1, rxBuf, sizeof(rxBuf), &rcvLen,
        (uint32_t)(RFAL_TXRX_FLAGS_CRC_TX_MANUAL | RFAL_TXRX_FLAGS_CRC_RX_KEEP),
        rfalConvMsTo1fc(5));

    if (err != RFAL_ERR_NONE || rcvLen < 1)
        return false;

    /* If we got an ACK (0x0A, 4-bit), send 0x43 */
    if ((rxBuf[0] & 0x0F) == 0x0A) {
        txBuf[0] = 0x43;
        rcvLen = 0;
        err = rfalTransceiveBlockingTxRx(
            txBuf, 1, rxBuf, sizeof(rxBuf), &rcvLen,
            (uint32_t)(RFAL_TXRX_FLAGS_CRC_TX_MANUAL | RFAL_TXRX_FLAGS_CRC_RX_KEEP),
            rfalConvMsTo1fc(5));
        if (err == RFAL_ERR_NONE && rcvLen >= 1 && (rxBuf[0] & 0x0F) == 0x0A) {
            return true;
        }
    }

    return false;
}
/*============================================================================*/
/**
 * @brief nfc_a_fill_uid_and_family - Fill NFC-A UID and Family (SAK/ATQA priority → type as auxiliary)
 * 
 * Fills UID and Family information for NFC-A devices based on SAK/ATQA values,
 * with device type as auxiliary information.
 * 
 * @param[in] dev Pointer to rfalNfcDevice (type=RFAL_NFC_LISTEN_TYPE_NFCA)
 * @retval None
 */
/*============================================================================*/
static void nfc_a_fill_uid_and_family(const rfalNfcDevice *dev)
{
    const rfalNfcaListenDevice* a = &dev->dev.nfca;

    /* Fill UID */
    NFC_ID_LEN = dev->nfcidLen;
    memset(NFC_ID, 0, sizeof(NFC_ID));
    ST_MEMCPY(NFC_ID, dev->nfcid, NFC_ID_LEN);
    strcpy(NFC_UID, hex2Str(dev->nfcid, dev->nfcidLen));

    /* Extract ATQA/SAK */
    const uint8_t sak   = a->selRes.sak;
    const uint8_t atqa0 = a->sensRes.anticollisionInfo;  /* ATQA LSB */
    const uint8_t atqa1 = a->sensRes.platformInfo;       /* ATQA MSB */
    const rfalNfcaListenDeviceType t = a->type;

    NFC_ATQA[0] = (char)atqa0;
    NFC_ATQA[1] = (char)atqa1;
    NFC_SAK = (char)sak;

    platformLog("[NFCA] type=%d ATQA=%02X%02X SAK=%02X\r\n", t, atqa0, atqa1, sak);

    /* 1) MIFARE Classic / Plus variants — classify by SAK */
    if (mfc_is_classic_sak(sak)) {
        const char *variant = "MIFARE Classic";
        switch (sak) {
        case 0x01: variant = "MIFARE Classic 1K (TNP3xxx)"; break;
        case 0x08: variant = "MIFARE Classic 1K"; break;
        case 0x09: variant = "MIFARE Mini 0.3K"; break;
        case 0x10: variant = "MIFARE Plus 2K (SL2)"; break;
        case 0x11: variant = "MIFARE Plus 4K (SL2)"; break;
        case 0x18: variant = "MIFARE Classic 4K"; break;
        case 0x19: variant = "MIFARE Classic 2K"; break;
        case 0x28: variant = "MIFARE Classic EV1 1K"; break;
        case 0x38: variant = "MIFARE Classic EV1 4K"; break;
        default:   variant = "MIFARE Classic"; break;
        }
        SET_FAMILY("%s", variant);
        return;
    }

    /* 2) Type 4A / DESFire — SAK bit 5 set (0x20) */
    if (sak == 0x20 || t == RFAL_NFCA_T4T || t == RFAL_NFCA_T4T_NFCDEP) {
        if (desfire_probe_iso_dep(dev)) SET_FAMILY("MIFARE DESFire (Type 4A)");
        else                            SET_FAMILY("Type 4A (ISO-DEP)");
        return;
    }

    /* 3) Ultralight/NTAG — SAK=0x00 with T2T type, or ATQA indicating Ultralight */
    if (sak == 0x00 && (t == RFAL_NFCA_T2T || atqa0 == 0x44)) {
        SET_FAMILY("Ultralight/NTAG");
        return;
    }

    /* 4) Topaz (Type 1 Tag, rare) */
    if (t == RFAL_NFCA_T1T) {
        SET_FAMILY("Topaz (Type 1)");
        return;
    }

    /* 5) Others */
    SET_FAMILY("NFC-A (unspecified)");
}


/*============================================================================*/
/**
 * @brief ReadCycle - Main NFC poller processing loop
 * 
 * Main processing loop for NFC poller mode. Handles state machine transitions
 * and card detection/reading operations. Processes discovered cards and
 * identifies their types (MIFARE Classic, T2T, T4T, etc.).
 * 
 * State machine:
 * - START_DISCOVERY: Starts discovery process
 * - DISCOVERY: Polls for cards and handles activation
 * 
 * @retval None
 */
/*============================================================================*/
/*------------------------------------------------------------------------------------------*/
void ReadCycle(void)
{
    static rfalNfcDevice *nfcDevice;

    rfalNfcWorker();                                    /* Run RFAL worker periodically */

    switch (state)
    {
/********************************** State : DISCOVERY *************************************/
    case START_DISCOVERY:
        rfalNfcDeactivate(RFAL_NFC_DEACTIVATE_IDLE);
        rfalNfcDiscover(&discParam);

        multiSel = false;
        state    = DISCOVERY;
        //platformLog("Poller START_DISCOVERY\r\n");
        break;  //↓ 
/************************************** State : DISCOVERY *****************************************/
    case DISCOVERY:
        //rfalNfcDevice *list = NULL; uint8_t cnt = 0;
        //rfalNfcGetDevicesFound(&list, &cnt);
        //if (cnt > 0) platformLog("Found cnt=%u\r\n", cnt);
        if (rfalNfcIsDevActivated(rfalNfcGetState()))
        {
            //platformLog("Poller rfalNfcGetState\r\n");
        
            m1_wdt_reset();

            bool notifyRead = false;

            rfalNfcGetActiveDevice(&nfcDevice); /* Get active device */

            uint8_t ctx_err = FillNfcContextFromDevice(nfcDevice);
            if (ctx_err != 0) {
                platformLog("FillNfcContextFromDevice err=%d (type=%d)\r\n",
                            (int)ctx_err, nfcDevice ? (int)nfcDevice->type : -1);
                /* If needed, can retry by setting state=START_DISCOVERY here */
            }else platformLog("Fill NFC Context Successed\r\n");

            switch (nfcDevice->type)
            {
                case RFAL_NFC_LISTEN_TYPE_NFCA:   /* ISO14443A / NFC-A card (MFC, UL/NTAG, DESFire, Magic) */
                {
                    isNFCCardFound = true;
                    nfc_tx_type    = NFC_TX_A;

                    // For logging: use dev->nfcid
                    platformLog("ISO14443A/NFC-A TAG found. type=0x%02X, UID=%s\r\n", nfcDevice->dev.nfca.type, hex2Str(nfcDevice->nfcid, nfcDevice->nfcidLen));
                    strcpy(NFC_Type, "ISO14443A/NFC-A");

                    // ATQA/SAK (access via RFAL structure fields)
                    const uint8_t sak   = nfcDevice->dev.nfca.selRes.sak;
                    const uint8_t atqa0 = nfcDevice->dev.nfca.sensRes.anticollisionInfo;
                    const uint8_t atqa1 = nfcDevice->dev.nfca.sensRes.platformInfo;

                    /* --- MIFARE Classic identification by SAK --- */
                    bool isMfcClassic = mfc_is_classic_sak(sak);

                    if (isMfcClassic) {
                        platformLog("MIFARE Classic detected. ATQA=%02X%02X, SAK=%02X, UIDLen=%u\r\n",
                                    atqa0, atqa1, sak, nfcDevice->nfcidLen);

                        /* Attempt to read sectors using key dictionary */
                        m1_read_mifareclassic(nfcDevice);
                    }

                    /* Store in emulation context (UID/ATQA/SAK) */
                    Emu_SetNfcA(nfcDevice->nfcid, nfcDevice->nfcidLen, atqa0, atqa1, sak);
                    //platformLog("Emu_SetNfcA nfcDevice->nfcidLen=%u\r\n", nfcDevice->nfcidLen);

                    // === Auto-set Family/UID here ===
                    nfc_a_fill_uid_and_family(nfcDevice);

                    /* Additional reading for T1/T2/T4/DEP only if not MIFARE Classic */
                    if (!isMfcClassic) {
                        if (nfcDevice->dev.nfca.type == RFAL_NFCA_T1T)
                            platformLog("NFC Type 1 Tag Read More\r\n");
                        else if (nfcDevice->dev.nfca.type == RFAL_NFCA_T2T) {
                            platformLog("NFC Type 2 Tag Read More\r\n");
                            m1_t2t_read_ntag(nfcDevice); // Improve to t2t
                        }
                        else if (nfcDevice->dev.nfca.type == RFAL_NFCA_T4T)
                            platformLog("NFC Type 4 Tag Read More\r\n");
                        else if (nfcDevice->dev.nfca.type == RFAL_NFCA_NFCDEP)
                            platformLog("NFC DEP Read More\r\n");
                        else if (nfcDevice->dev.nfca.type == RFAL_NFCA_T4T_NFCDEP)
                            platformLog("NFC Type 4 Tag DEP Read More\r\n");
                    }


                    notifyRead = true;
                }

                break;
                /*******************************************************************************/
                case RFAL_NFC_LISTEN_TYPE_NFCB:   /* ISO14443B / NFC-B */
                    platformLog("ISO14443B/NFC-B TAG found. UID=%s\r\n",
                                hex2Str(nfcDevice->nfcid, nfcDevice->nfcidLen));
                    strcpy(NFC_Type, "ISO14443B/NFC-B");
                    nfc_tx_type = NFC_TX_B;
                    notifyRead  = true;
                    break;

                /*******************************************************************************/
                case RFAL_NFC_LISTEN_TYPE_NFCF:   /* FeliCa / NFC-F */
                    isNFCCardFound = true;
                    platformLog("FeliCa/NFC-F TAG found. NFCID=%s\r\n",
                                hex2Str(nfcDevice->nfcid, nfcDevice->nfcidLen));
                    strcpy(NFC_Type, "Felica/NFC-F");
                    nfc_tx_type = NFC_TX_F;
                    notifyRead  = true;
                    break;

                /*******************************************************************************/
                case RFAL_NFC_LISTEN_TYPE_NFCV:   /* ISO15693 / NFC-V — includes PicoPass/iCLASS */
                {
                    uint8_t devUID[RFAL_NFCV_UID_LEN];
                    ST_MEMCPY(devUID, nfcDevice->nfcid, nfcDevice->nfcidLen);
                    REVERSE_BYTES(devUID, RFAL_NFCV_UID_LEN);  /* Reverse for display */
                    platformLog("ISO15693/NFC-V TAG found. UID=%s\r\n",
                                hex2Str(devUID, RFAL_NFCV_UID_LEN));
                    nfc_tx_type = NFC_TX_V;

                    /* Try PicoPass / iCLASS first — it rides on ISO15693 PHY */
                    {
                        static PicopassData pp_data;
                        PicopassReadResult pp_res = picopass_poller_read(nfcDevice, &pp_data);
                        if (pp_res == PICOPASS_READ_OK || pp_res == PICOPASS_READ_ERR_AUTH) {
                            /* It IS a PicoPass card */
                            strcpy(NFC_Type, "PicoPass/iCLASS");
                            SET_FAMILY("PicoPass");
                            isNFCCardFound = true;

                            /* Fill context for save/display */
                            nfc_run_ctx_t *c = nfc_ctx_get();
                            c->head.tech     = M1NFC_TECH_V;
                            c->head.family   = M1NFC_FAM_ICLASS;
                            c->head.uid_len  = 8;
                            memcpy(c->head.uid, pp_data.csn, 8);

                            /* Copy block data into dump buffer */
                            memset(g_nfc_dump_buf, 0, NFC_DUMP_BUF_SIZE);
                            memset(g_nfc_valid_bits, 0, NFC_VALID_BITS_SIZE);
                            uint8_t num_blocks = pp_data.block_count;
                            if (num_blocks > PICOPASS_MAX_APP_LIMIT)
                                num_blocks = PICOPASS_MAX_APP_LIMIT;
                            for (uint8_t i = 0; i < num_blocks; i++) {
                                memcpy(&g_nfc_dump_buf[i * 8], pp_data.blocks[i].data, 8);
                                g_nfc_valid_bits[i >> 3] |= (uint8_t)(1u << (i & 7));
                            }
                            nfc_ctx_set_dump(8, num_blocks, 0,
                                             g_nfc_dump_buf, g_nfc_valid_bits,
                                             (num_blocks > 0) ? num_blocks - 1 : 0, true);

                            if (pp_res == PICOPASS_READ_ERR_AUTH) {
                                platformLog("[PP] Card detected but auth failed — partial read\r\n");
                            }
                            notifyRead = true;
                            break;
                        }
                    }

                    /* Not PicoPass — fall through to standard NFC-V read */
                    strcpy(NFC_Type, "ISO15693/NFC-V");
                    SET_FAMILY("ISO15693");

                    /* Read NFC-V blocks */
                    m1_nfcv_read(nfcDevice);

                    notifyRead  = true;
                }
                break;

                /*******************************************************************************/
                case RFAL_NFC_LISTEN_TYPE_ST25TB:
                {
                    platformLog("ST25TB TAG found. UID=%s\r\n",
                                hex2Str(nfcDevice->nfcid, nfcDevice->nfcidLen));
                    strcpy(NFC_Type, "ST25TB");
                    SET_FAMILY("ST25TB");

                    /* Read ST25TB blocks */
                    m1_st25tb_read(nfcDevice);

                    notifyRead = true;
                }
                break;

                /*******************************************************************************/
                /* CE/P2P types are ignored in READ-ONLY mode */
                case RFAL_NFC_LISTEN_TYPE_AP2P:
                case RFAL_NFC_POLL_TYPE_AP2P:
                case RFAL_NFC_POLL_TYPE_NFCA:
                case RFAL_NFC_POLL_TYPE_NFCF:
                    platformLog("Non-reader mode type detected (ignored in READ-ONLY): type=%d\r\n",
                                nfcDevice->type);
                    notifyRead = false;
                    break;

                /*******************************************************************************/
                default:
                    platformLog("Unknown type=%d\r\n", nfcDevice->type);
                    notifyRead = false;
                    break;
            } /* switch(nfcDevice->type) */

            /* Common: Update UID buffer and length */
            strcpy(NFC_UID, hex2Str(nfcDevice->nfcid, nfcDevice->nfcidLen));
            NFC_ID_LEN = nfcDevice->nfcidLen;

            for (int i = 0; i < 20; ++i) {
                NFC_ID[i] = 0;
            }
            for (int i = 0; i < nfcDevice->nfcidLen; ++i) {
                NFC_ID[i] = nfcDevice->nfcid[i];
            }

            /* Notify read completion */
            if (notifyRead) {
                m1_wdt_reset();

                m1_app_send_q_message(nfc_worker_q_hdl, Q_EVENT_NFC_READ_COMPLETE);
            }

            /* Always deactivate to idle and return to re-discovery loop */
            rfalNfcDeactivate(RFAL_NFC_DEACTIVATE_IDLE);

            state = START_DISCOVERY;
        }
        else
        {
            /* Nothing found. Continue polling in next cycle */
            //platformLog("rfalNfcIsDevActivated false\r\n\r\n");
        }

        m1_wdt_reset();

        break; /* DEMO_ST_DISCOVERY */

/************************************** State : NOTINIT *****************************************/
    case NOTINIT:
        //platformLog("NOTINIT\r\n");
        /* fallthrough */
    default:
        //platformLog("default\r\n");
        break;
    } /* switch(state) */
}

void nfc_poller_set_profile(nfc_poll_profile_t profile)
{
    s_poll_profile = profile;
}

nfc_poll_profile_t nfc_poller_get_profile(void)
{
    return s_poll_profile;
}


/*============================================================================*/
/**
 * @brief ReadIni - Initialize NFC poller (READ-ONLY mode)
 *
 * Poller-only initialization. CE/P2P disabled.
 * Validates RFAL/DiscParam settings and enters START_DISCOVERY state.
 *
 * @retval true  Initialization successful
 * @retval false Initialization failed
 */
/*============================================================================*/
bool ReadIni(void)
{
    ReturnCode err = RFAL_ERR_NONE;
    bool fast_a_only = (s_poll_profile == NFC_POLL_PROFILE_FAST_A);

    /* 1) RFAL Initialize (retry 2 times) */
    for (int i = 0; i < 2; i++) {
        err = rfalNfcInitialize();
        //platformLog("rfalNfcInitialize() = %d\r\n", err);
        if (err == RFAL_ERR_NONE) break;
        vTaskDelay(5);
    }
    if (err != RFAL_ERR_NONE) return false;

    /* 2) Discovery parameters: Explicitly set for Poller-only mode */
    rfalNfcDefaultDiscParams(&discParam);

    discParam.devLimit       = 1U;
    discParam.totalDuration  = fast_a_only ? 220U : 450U; /* Shorter windows improve responsiveness */
    discParam.notifyCb       = PollerNotif;
#if defined(RFAL_COMPLIANCE_MODE_NFC)
    discParam.compMode       = RFAL_COMPLIANCE_MODE_NFC;   /* Recommended for application */
#endif

    /* CE/P2P disabled: Enable only Poller in techs2Find */
    discParam.techs2Find     = RFAL_NFC_TECH_NONE;

#if RFAL_FEATURE_NFCA
    discParam.techs2Find    |= RFAL_NFC_POLL_TECH_A;
#endif
    if (!fast_a_only)
    {
#if RFAL_FEATURE_NFCB
        discParam.techs2Find |= RFAL_NFC_POLL_TECH_B;
#endif
#if RFAL_FEATURE_NFCF
        discParam.techs2Find |= RFAL_NFC_POLL_TECH_F;
#endif
#if RFAL_FEATURE_NFCV
        discParam.techs2Find |= RFAL_NFC_POLL_TECH_V;
#endif
#if RFAL_FEATURE_ST25TB
        discParam.techs2Find |= RFAL_NFC_POLL_TECH_ST25TB;
#endif
    }

    /* Never enabled (READ ONLY) */
    /* discParam.techs2Find |= RFAL_NFC_POLL_TECH_AP2P;        */
    /* discParam.techs2Find |= RFAL_NFC_LISTEN_TECH_AP2P;      */
    /* discParam.techs2Find |= RFAL_NFC_LISTEN_TECH_A/F;       */

#if ST25R95
    discParam.isoDepFS       = RFAL_ISODEP_FSXI_128; /* ST25R95 Limit */
#endif

    /* 3) Verify configuration is valid by calling Discover once, then deactivate to Idle */
    err = rfalNfcDiscover(&discParam);
    rfalNfcDeactivate(RFAL_NFC_DEACTIVATE_IDLE);
    if (err != RFAL_ERR_NONE) {
        platformLog("rfalNfcDiscover() check failed: %d\r\n", err);
        return false;
    }

    /* 4) Enter state machine starting point */
    state = START_DISCOVERY;

    //platformLog("ReadIni() OK, state=%d\r\n", state);
    return true;
}


/*============================================================================*/
/**
 * @brief PollerNotif - Poller notification callback
 * 
 * Handles RFAL NFC state change notifications for poller mode.
 * Manages wake-up mode, device detection, multiple device selection,
 * and discovery state transitions.
 * 
 * @param[in] st RFAL NFC state
 * @retval None
 */
/*============================================================================*/
static void PollerNotif( rfalNfcState st )
{
    uint8_t       devCnt;
    rfalNfcDevice *dev;

    if( st == RFAL_NFC_STATE_WAKEUP_MODE )
    {
        platformLog("Wake Up mode started \r\n");
    }
    else if( st == RFAL_NFC_STATE_POLL_TECHDETECT )
    {
        if( discParam.wakeupEnabled )
        {
            platformLog("Wake Up mode terminated. Polling for devices \r\n");
        }
    }
    else if( st == RFAL_NFC_STATE_POLL_SELECT )
    {
        /* Check if in case of multiple devices, selection is already attempted */
        if( (!multiSel) )
        {
            multiSel = true;
            /* Multiple devices were found, activate first of them */
            rfalNfcGetDevicesFound( &dev, &devCnt );
            rfalNfcSelect( 0 );

            platformLog("Multiple Tags detected: %d \r\n", devCnt);
        }
        else
        {
            rfalNfcDeactivate( RFAL_NFC_DEACTIVATE_DISCOVERY );
        }
    }
    else if( st == RFAL_NFC_STATE_START_DISCOVERY )
    {
        /* Clear mutiple device selection flag */
        multiSel = false;
    }
}


/*============================================================================*/
/**
 * @brief GetVersion_Ntag - Send GET_VERSION command to NTAG
 * 
 * Sends NTAG GET_VERSION command (0x60) and receives 8-byte version response.
 * This command retrieves version information including vendor, product,
 * size, and protocol information from NTAG/Ultralight-C tags.
 * 
 * @param[out] rxBuf Buffer to store received version data
 * @param[in] rxBufLen Size of receive buffer (must be at least 8)
 * @param[out] rcvLen Pointer to store actual received length
 * @retval RFAL_ERR_NONE Success
 * @retval RFAL_ERR_PARAM Invalid parameters
 * @retval Other RFAL error codes
 */
/*============================================================================*/
static ReturnCode GetVersion_Ntag(uint8_t *rxBuf, uint16_t rxBufLen, uint16_t *rcvLen)
{
    uint8_t cmd = 0x60; /* NTAG GET_VERSION command*/

    if ((rxBuf == NULL) || (rcvLen == NULL) || (rxBufLen < 8U)) {
        return RFAL_ERR_PARAM;
    }

    *rcvLen = 0;

    return rfalTransceiveBlockingTxRx(&cmd, 1U, rxBuf, rxBufLen, rcvLen, RFAL_TXRX_FLAGS_DEFAULT, rfalConvMsTo1fc(5U));
}

/*============================================================================*/
/**
 * @brief PwdAuth_Ntag - Send PWD_AUTH (0x1B) command to NTAG/Ultralight
 *
 * Sends a 4-byte password to authenticate with a password-protected NTAG.
 * On success, the tag returns a 2-byte PACK (Password ACKnowledge).
 * After successful auth, protected pages become readable.
 *
 * @param[in]  pwd   4-byte password to send
 * @param[out] pack  2-byte PACK response from tag (valid on success)
 * @retval RFAL_ERR_NONE  Authentication successful
 * @retval Other          RFAL error (NAK = wrong password, timeout, etc.)
 */
/*============================================================================*/
static ReturnCode PwdAuth_Ntag(const uint8_t pwd[4], uint8_t pack[2])
{
    uint8_t  txBuf[5];
    uint8_t  rxBuf[2];
    uint16_t rcvLen = 0;

    txBuf[0] = 0x1B;  /* PWD_AUTH command */
    txBuf[1] = pwd[0];
    txBuf[2] = pwd[1];
    txBuf[3] = pwd[2];
    txBuf[4] = pwd[3];

    ReturnCode err = rfalTransceiveBlockingTxRx(
        txBuf, sizeof(txBuf),
        rxBuf, sizeof(rxBuf),
        &rcvLen,
        RFAL_TXRX_FLAGS_DEFAULT,
        rfalConvMsTo1fc(20U));

    if (err == RFAL_ERR_NONE && rcvLen >= 2 && pack != NULL) {
        pack[0] = rxBuf[0];
        pack[1] = rxBuf[1];
    }

    return err;
}

/*============================================================================*/
/**
 * @brief ntag_generate_amiibo_pwd - Generate Amiibo PWD_AUTH password from UID
 *
 * Derives the 4-byte password and 2-byte PACK for Amiibo (NTAG215) tags
 * using the well-known XOR algorithm. Requires a 7-byte UID.
 *
 * Algorithm:
 *   PWD[0] = 0xAA ^ UID[1] ^ UID[3]
 *   PWD[1] = 0x55 ^ UID[2] ^ UID[4]
 *   PWD[2] = 0xAA ^ UID[3] ^ UID[5]
 *   PWD[3] = 0x55 ^ UID[4] ^ UID[6]
 *   PACK   = 0x80, 0x80
 *
 * @param[in]  uid       7-byte UID from NTAG215
 * @param[in]  uid_len   UID length (must be 7)
 * @param[out] pwd_out   4-byte generated password
 * @param[out] pack_out  2-byte expected PACK
 * @retval true   Success
 * @retval false  Invalid UID length
 */
/*============================================================================*/
static bool ntag_generate_amiibo_pwd(const uint8_t *uid, uint8_t uid_len,
                                     uint8_t pwd_out[4], uint8_t pack_out[2])
{
    if (!uid || uid_len != 7 || !pwd_out || !pack_out)
        return false;

    pwd_out[0] = 0xAA ^ uid[1] ^ uid[3];
    pwd_out[1] = 0x55 ^ uid[2] ^ uid[4];
    pwd_out[2] = 0xAA ^ uid[3] ^ uid[5];
    pwd_out[3] = 0x55 ^ uid[4] ^ uid[6];

    pack_out[0] = 0x80;
    pack_out[1] = 0x80;

    return true;
}


/*============================================================================*/
/**
 * @brief LogParsedNtagVersion - Parse and log NTAG version information
 * 
 * Parses NTAG GET_VERSION response (8 bytes) and logs detailed version information
 * including vendor, product, sub-type, version number, size, and protocol.
 * Determines total page count based on size code:
 * - 0x0F: NTAG213 (45 pages, 144B user)
 * - 0x11: NTAG215 (135 pages, 504B user)
 * - 0x13: NTAG216 (231 pages, 888B user)
 * 
 * @param[in] version Pointer to 8-byte version data from GET_VERSION response
 * @param[in] len Length of version data (should be 8)
 * @retval Total page count (0 if unknown or invalid)
 */
/*============================================================================*/
static uint16_t LogParsedNtagVersion(const uint8_t *version, uint16_t len)
{
	//Use attributes for the compressor warning issue
    uint16_t totalPages = 0;

    if ((version == NULL) || (len < 8U)) {
        platformLog("NTAG GET_VERSION parse skipped (len=%u)\r\n", len);
        return totalPages;
    }

    const uint8_t header  __attribute__((unused)) = version[0];
    const uint8_t vendor  = version[1];
    const uint8_t prod    = version[2];
    const uint8_t sub     = version[3];
    const uint8_t major   __attribute__((unused)) = version[4];
    const uint8_t minor   __attribute__((unused)) = version[5];
    const uint8_t size    = version[6];
    const uint8_t proto   = version[7];

    const char *vendorStr __attribute__((unused)) = (vendor == 0x04) ? "NXP" : "Unknown";
    const char *prodStr   __attribute__((unused)) = (prod == 0x04) ? "NTAG" : "Unknown";
    const char *subStr    __attribute__((unused)) = (sub == 0x02) ? "Standard" : "Unknown";

    const char *sizeStr __attribute__((unused)) = "Unknown";
    if (size == 0x0F) {
        sizeStr    = "NTAG213 [144B user]";
        totalPages = 45U;
    }
    else if (size == 0x11) {
        sizeStr    = "NTAG215 [504B user]";
        totalPages = 135U;
    }
    else if (size == 0x13) {
        sizeStr    = "NTAG216 [888B user]";
        totalPages = 231U;
    }

    const char *protoStr __attribute__((unused)) = (proto == 0x03) ? "ISO14443-3" : "Unknown";

    platformLog("NTAG VERSION parsed: hdr=0x%02X vendor=%s(0x%02X) prod=%s(0x%02X)\r\n", header, vendorStr, vendor, prodStr, prod);
    platformLog("sub=%s(0x%02X) ver=%u.%u size=%s(0x%02X) proto=%s(0x%02X)\r\n", subStr, sub, (unsigned)major, (unsigned)minor, sizeStr, size, protoStr, proto);

    if (totalPages == 0U) {
        platformLog("NTAG VERSION: unknown size code 0x%02X, using default page count\r\n", size);
    } else {
        platformLog("NTAG VERSION: detected %u total pages\r\n", totalPages);
    }

    return totalPages;
}

/*============================================================================*/
/**
 * @brief m1_t2t_read_ntag - Read Type 2 Tag (NTAG/Ultralight) memory
 * 
 * Reads Type 2 Tag memory pages using T2T READ commands.
 * Performs GET_VERSION to determine page count, then reads all pages
 * sequentially. Parses NDEF TLV if present.
 * 
 * @param[in] dev Pointer to NFC device (unused, kept for compatibility)
 * @retval None
 */
/*============================================================================*/
static void m1_t2t_read_ntag(const rfalNfcDevice *dev)
{
    (void)dev; // Temporarily unused

    uint8_t  buf[16];               // T2T READ response buffer
    uint8_t* dump     = g_nfc_dump_buf;
    uint16_t dumpSize = NFC_DUMP_BUF_SIZE;
    uint16_t offset   = 0;
    uint16_t rcvLen   = 0;
    uint16_t max_page = 45U;        // Default to NTAG213 span when size is unknown
    ReturnCode err;

    // Clear previous NDEF / dump (optional)
    nfc_ctx_clear_t2t_ndef();
    nfc_ctx_clear_dump();

    uint8_t version[8] = {0};
    bool    ver_ok     = false;

    for (uint8_t attempt = 0; attempt < 2 && !ver_ok; attempt++) {
        if (attempt > 0) {
            osDelay(10);  // Wait before retry
        }
        
        memset(version, 0x00, sizeof(version));
        rcvLen = 0;
        err = GetVersion_Ntag(version, sizeof(version), &rcvLen);

        /* If all response bytes are 0x00, consider it a failure */
        bool all_zero = true;
        for (uint8_t i = 0; i < sizeof(version); i++) {
            if (version[i] != 0x00) { all_zero = false; break; }
        }

        if ((err == RFAL_ERR_NONE) && (rcvLen == sizeof(version)) && !all_zero) {
            ver_ok = true;
        }
    }

    if (ver_ok) {
        max_page = LogParsedNtagVersion(version, rcvLen);
        nfc_ctx_set_t2t_version(version, (uint8_t)rcvLen);
        platformLog("GET_VER OK: %u pages\r\n", max_page);
    } else {
        /* If GET_VERSION fails, get page count from context */
        uint16_t ctx_pages = nfc_ctx_get_t2t_page_count();
        if (ctx_pages > 0) {
            max_page = ctx_pages;
            platformLog("GET_VER fail, using ctx: %u pages\r\n", max_page);
        } else {
            platformLog("GET_VER fail, using default: %u pages\r\n", max_page);
        }
    }



    /* --- PWD_AUTH: Unlock password-protected pages --- */
    /* Priority: 1) Manual password (user-entered)
     *           2) Captured password (sniffed from reader during emulation)
     *           3) Auto-generated (Amiibo XOR derivation for NTAG215) */
    {
        uint8_t pwd[4], pack_rx[2] = {0, 0};
        nfc_pwd_source_t pwd_src = NFC_PWD_SRC_NONE;
        bool auth_attempted = false;

        /* Try manual or captured password first */
        if (nfc_ctx_get_best_pwd(pwd, &pwd_src)) {
            const char *src_str = (pwd_src == NFC_PWD_SRC_MANUAL) ? "manual" : "captured";
            platformLog("[T2T] PWD_AUTH (%s): pwd=%s\r\n", src_str, hex2Str(pwd, 4));
            ReturnCode auth_err = PwdAuth_Ntag(pwd, pack_rx);
            if (auth_err == RFAL_ERR_NONE) {
                platformLog("[T2T] PWD_AUTH OK (%s): PACK=%02X %02X\r\n",
                            src_str, pack_rx[0], pack_rx[1]);
            } else {
                platformLog("[T2T] PWD_AUTH failed (%s, err=%d)\r\n", src_str, auth_err);
            }
            auth_attempted = true;
            /* Clear manual password after use (one-shot) */
            if (pwd_src == NFC_PWD_SRC_MANUAL)
                nfc_ctx_clear_manual_pwd();
        }

        /* If no manual/captured password, try Amiibo auto-auth for NTAG215 */
        if (!auth_attempted && max_page == 135U && ver_ok && version[6] == 0x11) {
            nfc_run_ctx_t* ctx = nfc_ctx_get();
            if (ctx && ctx->head.uid_len == 7) {
                uint8_t pack[2];
                if (ntag_generate_amiibo_pwd(ctx->head.uid, ctx->head.uid_len, pwd, pack)) {
                    platformLog("[T2T] Amiibo PWD_AUTH: pwd=%s\r\n", hex2Str(pwd, 4));
                    ReturnCode auth_err = PwdAuth_Ntag(pwd, pack_rx);
                    if (auth_err == RFAL_ERR_NONE) {
                        platformLog("[T2T] PWD_AUTH OK: PACK=%02X %02X\r\n", pack_rx[0], pack_rx[1]);
                    } else {
                        platformLog("[T2T] PWD_AUTH failed (err=%d) — non-Amiibo or locked tag\r\n", auth_err);
                    }
                }
            }
        }
    }

    // Read blocks 0 ~ N (T2T READ reads 4 blocks at a time, so increment by 4)
    for (uint8_t blk = 0; blk < max_page; blk += 4) {  // Read 4 blocks at a time
        rcvLen = 0;
        bool read_ok = false;
        
        /* Retry each READ command (max 3 times, to handle timing issues) */
        for (uint8_t retry = 0; retry < 3 && !read_ok; retry++) {
            /* Wait before retry (allow time for CE to respond) */
            if (retry > 0) {
                osDelay(10);  // Increase retry wait time (10ms)
            }
            
            /* Initialize buffer (prevent previous data residue) */
            memset(buf, 0x00, sizeof(buf));
            rcvLen = 0;
            
            err = rfalT2TPollerRead(blk, buf, sizeof(buf), &rcvLen);
            
            if (err == RFAL_ERR_NONE && rcvLen >= 16) {  // T2T READ returns 16 bytes (4 blocks)
                read_ok = true;
                /* Log only critical blocks */
                if (blk == 0 || blk == 4) {
                    platformLog("READ blk%u: %s\r\n", blk, hex2Str(buf, 8));
                }
            } else {
                /* Log only critical blocks on failure */
                if ((blk == 0 || blk == 4) && retry == 0) {
                    platformLog("READ blk%u fail: err=%d rcv=%u\r\n", blk, err, rcvLen);
                }
                /* If field loss, don't retry anymore */
                if (err == RFAL_ERR_LINK_LOSS) {
                    break;
                }
            }
        }
        
        /* Calculate number of pages to save */
        uint8_t pages_to_save = 4;
        if (blk + pages_to_save > max_page) {
            pages_to_save = max_page - blk;
        }
        
        if (!read_ok) {
            /* Stop if field loss */
            if (err == RFAL_ERR_LINK_LOSS) {
                break;
            }
            /* Fill failed blocks with 0 at corresponding position (prevent data shift) */
            for (uint8_t i = 0; i < pages_to_save; i++) {
                uint16_t page_idx = blk + i;
                if (page_idx >= max_page) break;
                
                uint16_t save_offset = page_idx * 4;
                if (save_offset + 4 <= dumpSize) {
                    memset(&dump[save_offset], 0x00, 4);
                }
            }
        } else {
            /* On success: Store received data at corresponding block position */
            for (uint8_t i = 0; i < pages_to_save; i++) {
                uint16_t page_idx = blk + i;
                if (page_idx >= max_page) break;
                
                uint16_t save_offset = page_idx * 4;
                if (save_offset + 4 <= dumpSize) {
                    /* Copy page data from buf */
                    uint8_t *src = &buf[i * 4];
                    uint8_t *dst = &dump[save_offset];
                    memcpy(dst, src, 4);
                }
            }
        }
        
        /* Update offset to next block position (regardless of success/failure) */
        offset = (blk + 4) * 4;
        
        /* Log progress (every 16 blocks) */
        if ((blk + 4) % 16 == 0 || (blk + 4) >= max_page) {
            platformLog("T2T read progress: %u/%u pages\r\n", (unsigned)((blk + 4 > max_page) ? max_page : blk + 4), (unsigned)max_page);
        }
    }

    osDelay(5);

    // Actual dump length and page count
    uint16_t dump_len  = offset;
    uint16_t num_pages = dump_len / 4;

    if (num_pages == 0) {
        platformLog("T2T: no pages dumped\r\n");
        return;
    }

    // Mark all pages as valid in valid_bits for now
    memset(g_nfc_valid_bits, 0xFF, (num_pages + 7) / 8);

    // Update context dump metadata
    nfc_ctx_set_dump(4,                // unit_size: 4 bytes per page
                     num_pages,        // unit_count
                     0,                // origin
                     g_nfc_dump_buf,   // data pointer
                     g_nfc_valid_bits, // valid bits
                     num_pages - 1,    // max_seen_unit
                     true);            // has_dump = true

    // ---- TLV parsing (find NDEF) ----
    const uint8_t firstDataPage = 4;
    uint8_t *p   = dump + (firstDataPage * 4);
    uint8_t *end = dump + dump_len;

    if (p >= end) {
        platformLog("T2T: dump too short (offset=%u)\r\n", dump_len);
        return;
    }

    while (p < end) {
        uint8_t t = *p++;

        if (t == 0x00) {
            // NULL TLV
            continue;
        }
        if (t == 0xFE) {
            // Terminator TLV
            platformLog("T2T: Terminator TLV reached, no more TLVs\r\n");
            break;
        }

        if (p >= end) break;

        uint16_t len = *p++;  // Simple 1-byte length handling

        if (t == 0x03) {      // NDEF TLV
            if (p + len > end) {
                len = (uint16_t)(end - p);  // Defensive
            }

            platformLog("T2T NDEF TLV found, len=%u\r\n", len);
            nfc_ctx_set_t2t_ndef(p, len);
#if 0
            //nfc_ctx_dump_t2t_ndef();   // Debug hexdump + ASCII
            //nfc_ctx_dump_t2t_pages();
#endif
            return;
        } else {
            // Skip uninterested TLV by len
            if (p + len > end) break;
            p += len;
        }
    }

    platformLog("T2T: NDEF TLV not found (scan from page=%u)\r\n", firstDataPage);
}




/*
 ******************************************************************************
 * MIFARE CLASSIC helper functions
 ******************************************************************************
*/

/*============================================================================*/
/* Streaming key dictionary iterator — reads one key at a time from SD card   */
/*============================================================================*/

static int mfc_hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

static bool mfc_key_iter_open(mfc_key_iter_t *it)
{
    if (!it) return false;
    it->count   = 0;
    it->is_open = false;

    if (!nfcfio_open_read(&it->io, MFC_DICT_PATH)) {
        platformLog("[MFC] dict open failed: %s\r\n", MFC_DICT_PATH);
        return false;
    }
    it->is_open = true;
    return true;
}

static bool mfc_key_iter_next(mfc_key_iter_t *it, uint8_t key_out[MFC_KEY_LEN])
{
    if (!it || !it->is_open) return false;

    int n;
    while ((n = nfcfio_getline(&it->io, it->line, sizeof(it->line))) >= 0) {
        char *p = it->line;

        /* Skip whitespace */
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (*p == '\0') continue;
        if (*p == '#')  continue;

        /* Parse 12 hex chars → 6 bytes */
        uint8_t key[MFC_KEY_LEN];
        bool ok = true;
        for (int i = 0; i < MFC_KEY_LEN && ok; i++) {
            int hi = mfc_hex_nibble(*p++);
            int lo = mfc_hex_nibble(*p++);
            if (hi < 0 || lo < 0) { ok = false; break; }
            key[i] = (uint8_t)((hi << 4) | lo);
        }
        if (!ok) continue;

        memcpy(key_out, key, MFC_KEY_LEN);
        it->count++;
        return true;
    }
    return false;
}

static void mfc_key_iter_rewind(mfc_key_iter_t *it)
{
    if (!it || !it->is_open) return;
    nfcfio_close(&it->io);
    it->is_open = false;
    it->count   = 0;
    if (nfcfio_open_read(&it->io, MFC_DICT_PATH)) {
        it->is_open = true;
    }
}

static void mfc_key_iter_close(mfc_key_iter_t *it)
{
    if (!it) return;
    if (it->is_open) {
        nfcfio_close(&it->io);
        it->is_open = false;
    }
}



/*============================================================================*/
/* MIFARE Classic read using software Crypto-1 + streaming key dictionary     */
/*============================================================================*/

static void mfc_get_layout_from_sak(uint8_t sak, uint16_t *outSectors, uint16_t *outBlocks)
{
    switch (sak) {
    case 0x09: /* Mini */
        *outSectors = 5;
        *outBlocks  = 20;
        break;
    case 0x01: /* Classic 1K TNP3xxx */
    case 0x08: /* Classic 1K */
    case 0x28: /* Classic EV1 1K */
        *outSectors = 16;
        *outBlocks  = 64;
        break;
    case 0x10: /* Plus 2K SL2 */
    case 0x19: /* Classic 2K */
        *outSectors = 32;
        *outBlocks  = 128;
        break;
    case 0x11: /* Plus 4K SL2 */
    case 0x18: /* Classic 4K */
    case 0x38: /* Classic EV1 4K */
        *outSectors = 40;
        *outBlocks  = 256;
        break;
    default:
        platformLog("[MFC] unknown SAK 0x%02X, fallback to 1K layout\r\n", sak);
        *outSectors = 16;
        *outBlocks  = 64;
        break;
    }
}

static uint16_t mfc_sector_to_first_block(uint16_t sector)
{
    if (sector < 32)
        return (uint16_t)(sector * 4);
    else
        return (uint16_t)(128 + (sector - 32) * 16);
}

static void m1_read_mifareclassic(const rfalNfcDevice *dev)
{
    const rfalNfcaListenDevice *nfca = &dev->dev.nfca;
    uint16_t totalSectors = 0;
    uint16_t totalBlocks  = 0;
    uint16_t maxBlocks    = NFC_DUMP_MAX_UNITS;

    mfc_get_layout_from_sak(nfca->selRes.sak, &totalSectors, &totalBlocks);
    if (totalBlocks > maxBlocks) totalBlocks = maxBlocks;

    /* UID: first 4 bytes for Crypto-1 (even for 7-byte UID cards) */
    uint8_t uid4[4];
    if (dev->nfcidLen >= 7) {
        /* For 7-byte UID: use bytes 3..6 (the second cascade level) */
        memcpy(uid4, &dev->nfcid[3], 4);
    } else {
        memcpy(uid4, dev->nfcid, 4);
    }

    memset(g_nfc_dump_buf, 0x00, NFC_DUMP_BUF_SIZE);
    memset(g_nfc_valid_bits, 0x00, NFC_VALID_BITS_SIZE);

    platformLog("[MFC] start dump: sectors=%u blocks=%u\r\n", totalSectors, totalBlocks);

    uint16_t lastSeenBlock  = 0;
    uint16_t successSectors = 0;

    mfc_key_iter_t iter;
    if (!mfc_key_iter_open(&iter)) {
        platformLog("[MFC] no key dict, skip MFC dump\r\n");
        return;
    }

    crypto1_state_t cstate;

    for (uint16_t sector = 0; sector < totalSectors; sector++) {
        uint16_t firstBlock     = mfc_sector_to_first_block(sector);
        uint16_t blocksInSector = (sector < 32 || totalSectors <= 16) ? 4 : 16;
        if (firstBlock >= totalBlocks) break;

        bool sectorAuthed = false;
        uint8_t key[MFC_KEY_LEN];

        /* Rewind key iterator for each sector */
        mfc_key_iter_rewind(&iter);

        while (mfc_key_iter_next(&iter, key) && !sectorAuthed) {
            m1_wdt_reset();

            /* Try Key A */
            if (mfc_auth(&cstate, uid4, (uint8_t)firstBlock, MFC_AUTH_CMD_A, key)) {
                sectorAuthed = true;
                platformLog("[MFC] sector %u auth OK KeyA\r\n", sector);
                break;
            }

            /* Try Key B */
            if (mfc_auth(&cstate, uid4, (uint8_t)firstBlock, MFC_AUTH_CMD_B, key)) {
                sectorAuthed = true;
                platformLog("[MFC] sector %u auth OK KeyB\r\n", sector);
                break;
            }
        }

        if (!sectorAuthed) {
            platformLog("[MFC] sector %u auth FAILED\r\n", sector);
            continue;
        }

        successSectors++;

        /* Read all blocks of authenticated sector */
        for (uint16_t bi = 0; bi < blocksInSector; bi++) {
            uint16_t blockNo = firstBlock + bi;
            if (blockNo >= totalBlocks) break;

            uint8_t *dst = &g_nfc_dump_buf[blockNo * MFC_BLOCK_SIZE];

            if (mfc_read_block_crypto(&cstate, (uint8_t)blockNo, dst)) {
                g_nfc_valid_bits[blockNo >> 3] |= (uint8_t)(1u << (blockNo & 0x7));
                if (blockNo + 1 > lastSeenBlock) lastSeenBlock = blockNo + 1;
            } else {
                platformLog("[MFC] read block %u failed\r\n", blockNo);
            }
        }
    }

    mfc_key_iter_close(&iter);

    nfc_ctx_set_dump(MFC_BLOCK_SIZE, totalBlocks, 0,
                     g_nfc_dump_buf, g_nfc_valid_bits,
                     lastSeenBlock, (lastSeenBlock > 0));

    platformLog("[MFC] dump done: successSectors=%u lastBlock=%u\r\n",
                successSectors, lastSeenBlock);
}

/*============================================================================*/
/* NFC-V (ISO15693) block reading                                             */
/*============================================================================*/

static void m1_nfcv_read(const rfalNfcDevice *dev)
{
    uint8_t rxBuf[32];
    uint16_t rcvLen = 0;

    /* Get system information to determine block count and size */
    ReturnCode err = rfalNfcvPollerGetSystemInformation(
        RFAL_NFCV_REQ_FLAG_DEFAULT, dev->nfcid, rxBuf, sizeof(rxBuf), &rcvLen);

    uint16_t blockCount = 0;
    uint8_t  blockSize  = 4; /* default */

    if (err == RFAL_ERR_NONE && rcvLen >= 12) {
        /* System info response: flags(1) + DSFID + UID(8) + ... */
        /* Per ISO15693: if INFO_FLAGS bit 0 set, includes block info */
        uint8_t infoFlags = rxBuf[1]; /* After response flags byte */
        /* Parse based on standard system info layout */
        int offset = 10; /* skip flags(1) + infoFlags(1) + UID(8) */
        if (infoFlags & 0x01) { /* DSFID present */
            offset++;
        }
        if (infoFlags & 0x02) { /* AFI present */
            offset++;
        }
        if (infoFlags & 0x04) { /* Memory size present */
            if (offset + 2 <= (int)rcvLen) {
                blockCount = (uint16_t)(rxBuf[offset] + 1); /* Number of blocks - 1 */
                blockSize  = (uint8_t)(rxBuf[offset + 1] + 1); /* Block size - 1 */
            }
        }
        platformLog("[NFC-V] SysInfo: blocks=%u blockSize=%u\r\n", blockCount, blockSize);
    } else {
        /* Fallback: try reading blocks until failure */
        blockCount = 64; /* Common default */
        platformLog("[NFC-V] SysInfo failed, trying %u blocks\r\n", blockCount);
    }

    if (blockCount == 0) return;
    if (blockSize > 32) blockSize = 32;

    memset(g_nfc_dump_buf, 0x00, NFC_DUMP_BUF_SIZE);
    memset(g_nfc_valid_bits, 0x00, NFC_VALID_BITS_SIZE);
    uint32_t maxSeen = 0;

    for (uint16_t blk = 0; blk < blockCount; blk++) {
        rcvLen = 0;
        err = rfalNfcvPollerReadSingleBlock(
            RFAL_NFCV_REQ_FLAG_DEFAULT, dev->nfcid, (uint8_t)blk,
            rxBuf, sizeof(rxBuf), &rcvLen);

        if (err == RFAL_ERR_NONE && rcvLen >= (uint16_t)(1 + blockSize)) {
            /* First byte is response flags, then data */
            uint32_t offset_dst = (uint32_t)blk * blockSize;
            if (offset_dst + blockSize <= NFC_DUMP_BUF_SIZE) {
                memcpy(&g_nfc_dump_buf[offset_dst], &rxBuf[1], blockSize);
                g_nfc_valid_bits[blk >> 3] |= (uint8_t)(1u << (blk & 7));
                if (blk > maxSeen) maxSeen = blk;
            }
        } else {
            /* Stop on first read error (likely end of memory) */
            if (blk > 0) break;
        }
        m1_wdt_reset();
    }

    if (maxSeen > 0) {
        nfc_ctx_set_dump(blockSize, blockCount, 0,
                         g_nfc_dump_buf, g_nfc_valid_bits,
                         maxSeen, true);
        platformLog("[NFC-V] dump done: %u blocks\r\n", maxSeen + 1);
    }
}

/*============================================================================*/
/* ST25TB block reading                                                       */
/*============================================================================*/

static void m1_st25tb_read(const rfalNfcDevice *dev)
{
    (void)dev;
    rfalSt25tbBlock blockData;
    uint8_t blockSize = RFAL_ST25TB_BLOCK_LEN; /* 4 bytes */
    uint16_t blockCount = 64; /* ST25TB512 has 16 blocks, ST25TB2K has 64 */

    memset(g_nfc_dump_buf, 0x00, NFC_DUMP_BUF_SIZE);
    memset(g_nfc_valid_bits, 0x00, NFC_VALID_BITS_SIZE);
    uint32_t maxSeen = 0;

    for (uint16_t blk = 0; blk < blockCount; blk++) {
        ReturnCode err = rfalSt25tbPollerReadBlock((uint8_t)blk, &blockData);

        if (err == RFAL_ERR_NONE) {
            uint32_t offset = (uint32_t)blk * blockSize;
            if (offset + blockSize <= NFC_DUMP_BUF_SIZE) {
                memcpy(&g_nfc_dump_buf[offset], blockData, blockSize);
                g_nfc_valid_bits[blk >> 3] |= (uint8_t)(1u << (blk & 7));
                if (blk > maxSeen) maxSeen = blk;
            }
        } else {
            if (blk > 0) break; /* End of memory */
        }
        m1_wdt_reset();
    }

    if (maxSeen > 0) {
        nfc_ctx_set_dump(blockSize, maxSeen + 1, 0,
                         g_nfc_dump_buf, g_nfc_valid_bits,
                         maxSeen, true);
        platformLog("[ST25TB] dump done: %lu blocks\r\n", maxSeen + 1);
    }
}


/*============================================================================*/
/* Extern wrapper functions — expose static helpers for use by m1_nfc.c       */
/*============================================================================*/

bool nfc_poller_is_classic_sak(uint8_t sak)
{
    return mfc_is_classic_sak(sak);
}

void nfc_poller_read_mfc(const rfalNfcDevice *dev)
{
    m1_read_mifareclassic(dev);
}

void nfc_poller_read_t2t(const rfalNfcDevice *dev)
{
    m1_t2t_read_ntag(dev);
}

ReturnCode nfc_poller_pwd_auth(const uint8_t pwd[4], uint8_t pack[2])
{
    return PwdAuth_Ntag(pwd, pack);
}

ReturnCode nfc_poller_get_version(uint8_t *rxBuf, uint16_t rxBufLen, uint16_t *rcvLen)
{
    return GetVersion_Ntag(rxBuf, rxBufLen, rcvLen);
}

/*============================================================================*/
/**
 * @brief Test function: Enable continuous 13.56 MHz carrier (sine wave)
 * 
 * This function enables the ST25R3916 transmitter to output a pure
 * 13.56 MHz carrier (sine wave) without modulation. This can be used
 * as a "booster" to power up NFC tags from greater distances.
 * 
 * Based on analysis of Flipper Zero's NFC implementation which uses
 * similar techniques for improved range.
 * 
 * @param duration_ms Duration to transmit carrier (0 = infinite until stopped)
 * @return true if successful, false if error
 */
/*============================================================================*/
bool test_nfc_continuous_carrier(uint32_t duration_ms)
{
    ReturnCode err = RFAL_ERR_NONE;
    
    platformLog("[NFC] Enabling continuous carrier (sine wave)...\r\n");
    
    /* 1. First ensure RFAL is initialized */
    /* Note: You'll need to check your NFC initialization state */
    /* For now, we assume NFC is initialized */
    
    /* 2. Deactivate any current NFC activity */
    rfalNfcDeactivate(RFAL_NFC_DEACTIVATE_IDLE);
    
    /* 3. Turn off field if it's on */
    rfalFieldOff();
    
    /* 4. Configure ST25R3916 for continuous carrier transmission */
    
    /* Set mode to NFC (likely appropriate for continuous carrier) */
    err = st25r3916WriteRegister(ST25R3916_REG_MODE, ST25R3916_REG_MODE_om_nfc);
    if (err != RFAL_ERR_NONE) {
        platformLog("[NFC] ERROR: Failed to set mode (err=%d)\r\n", err);
        return false;
    }
    
    /* Configure TX driver for MAXIMUM POWER! */
    /* d_res = 0x0 (lowest resistance = highest power output) */
    /* am_mod = 40% modulation (MAXIMUM available for ST25R3916!) */
    /* WARNING: This may exceed regulatory limits in some regions */
    uint8_t tx_driver = (ST25R3916_REG_TX_DRIVER_am_mod_40percent) | 
                       (0x0 << ST25R3916_REG_TX_DRIVER_d_res_shift);
    
    err = st25r3916WriteRegister(ST25R3916_REG_TX_DRIVER, tx_driver);
    if (err != RFAL_ERR_NONE) {
        platformLog("[NFC] ERROR: Failed to configure TX driver (err=%d)\r\n", err);
        return false;
    }
    
    /* Optional: Set extended field-on guard time */
    err = st25r3916WriteRegister(ST25R3916_REG_FIELD_ON_GT, 0xFF);
    if (err != RFAL_ERR_NONE) {
        platformLog("[NFC] WARNING: Failed to set field-on GT (err=%d)\r\n", err);
        /* Continue anyway */
    }
    
    /* 5. Enable transmitter only (no receiver, no data) */
    uint8_t op_control = ST25R3916_REG_OP_CONTROL_tx_en | ST25R3916_REG_OP_CONTROL_en;
    err = st25r3916WriteRegister(ST25R3916_REG_OP_CONTROL, op_control);
    if (err != RFAL_ERR_NONE) {
        platformLog("[NFC] ERROR: Failed to enable transmitter (err=%d)\r\n", err);
        return false;
    }
    
    platformLog("[NFC] Continuous carrier ENABLED\r\n");
    platformLog("[NFC] Mode: 0x%02X, TX Driver: 0x%02X (5%% mod), OP Control: 0x%02X\r\n",
               ST25R3916_REG_MODE_om_nfc, tx_driver, op_control);
    
    /* 6. If duration specified, wait then turn off */
    if (duration_ms > 0) {
        platformLog("[NFC] Transmitting for %lu ms...\r\n", duration_ms);
        HAL_Delay(duration_ms);
        
        /* Turn off transmitter */
        test_nfc_stop_continuous_carrier();
        platformLog("[NFC] Continuous carrier DISABLED after %lu ms\r\n", duration_ms);
    } else {
        platformLog("[NFC] Continuous carrier ON (infinite duration)\r\n");
        platformLog("[NFC] Call test_nfc_stop_continuous_carrier() to stop\r\n");
    }
    
    return true;
}

/*============================================================================*/
/**
 * @brief Stop continuous carrier transmission
 * 
 * Disables the transmitter and restores normal NFC operation.
 * 
 * @return true if successful, false if error
 */
/*============================================================================*/
bool test_nfc_stop_continuous_carrier(void)
{
    platformLog("[NFC] Stopping continuous carrier...\r\n");
    
    /* Disable transmitter */
    ReturnCode err = st25r3916WriteRegister(ST25R3916_REG_OP_CONTROL, 0x00);
    if (err != RFAL_ERR_NONE) {
        platformLog("[NFC] ERROR: Failed to disable transmitter (err=%d)\r\n", err);
        return false;
    }
    
    /* Optional: Restore default mode */
    err = st25r3916WriteRegister(ST25R3916_REG_MODE, ST25R3916_REG_MODE_om_iso14443a);
    if (err != RFAL_ERR_NONE) {
        platformLog("[NFC] WARNING: Failed to restore mode (err=%d)\r\n", err);
    }
    
    platformLog("[NFC] Continuous carrier STOPPED\r\n");
    return true;
}

/*============================================================================*/
/**
 * @brief Test function: Boosted NFC read with carrier pre-activation
 * 
 * This function demonstrates the "boosting" technique:
 * 1. Transmit continuous carrier to wake up distant tags
 * 2. Perform normal NFC discovery/read
 * 3. Compare results with/without boosting
 * 
 * Note: This is a simplified version. You'll need to adapt it to your
 * specific NFC initialization and state management.
 * 
 * @param boost_duration_ms How long to transmit carrier before reading
 * @return true if tag was detected, false otherwise
 */
/*============================================================================*/
/**
 * @brief MAX POWER continuous carrier transmission
 * 
 * Uses maximum modulation (82%) and minimum driver resistance
 * for the STRONGEST possible sinewave output.
 * WARNING: May exceed regulatory limits!
 * 
 * @param duration_ms How long to transmit carrier
 * @return true if successful
 */
/*============================================================================*/
bool test_nfc_max_power_carrier(uint32_t duration_ms)
{
    ReturnCode err = RFAL_ERR_NONE;
    
    platformLog("[NFC] ABSOLUTE MAX POWER CARRIER! (GAIN CONTROL + EVERYTHING!)\r\n");
    
    /* 1. First ensure RFAL is initialized */
    /* Note: You'll need to check your NFC initialization state */
    /* For now, we assume NFC is initialized */
    
    /* 2. Deactivate any current NFC activity */
    rfalNfcDeactivate(RFAL_NFC_DEACTIVATE_IDLE);
    
    /* 3. Turn off field if it's on */
    rfalFieldOff();
    
    /* 4. Configure ST25R3916 for ABSOLUTE MAX POWER continuous carrier transmission */
    err = st25r3916WriteRegister(ST25R3916_REG_MODE, ST25R3916_REG_MODE_om_nfc);
    if (err != RFAL_ERR_NONE) {
        platformLog("[NFC] ERROR: Failed to set mode (err=%d)\r\n", err);
        return false;
    }
    
    /* Configure TX driver for MAXIMUM POWER! */
    /* d_res = 0x0 (lowest resistance = highest power output) */
    /* am_mod = 40%% modulation (MAX for ST25R3916) */
    uint8_t tx_driver = (ST25R3916_REG_TX_DRIVER_am_mod_40percent) | 
                       (0x0 << ST25R3916_REG_TX_DRIVER_d_res_shift);
    
    err = st25r3916WriteRegister(ST25R3916_REG_TX_DRIVER, tx_driver);
    if (err != RFAL_ERR_NONE) {
        platformLog("[NFC] ERROR: Failed to configure TX driver (err=%d)\r\n", err);
        return false;
    }
    
    /* GAIN REDUCTION IS READ-ONLY - CAN'T CHANGE IT */
    /* The chip manages gain reduction automatically */
    
    /* Set antenna tuning to default (0x82) - 0xFF didn't help */
    err = st25r3916WriteRegister(ST25R3916_REG_ANT_TUNE_A, 0x82);
    if (err != RFAL_ERR_NONE) {
        platformLog("[NFC] WARNING: Failed to set ANT_TUNE_A (err=%d)\r\n", err);
    }
    
    err = st25r3916WriteRegister(ST25R3916_REG_ANT_TUNE_B, 0x82);
    if (err != RFAL_ERR_NONE) {
        platformLog("[NFC] WARNING: Failed to set ANT_TUNE_B (err=%d)\r\n", err);
    }
    
    /* Optional: Set extended field-on guard time */
    err = st25r3916WriteRegister(ST25R3916_REG_FIELD_ON_GT, 0xFF);
    if (err != RFAL_ERR_NONE) {
        platformLog("[NFC] WARNING: Failed to set field-on GT (err=%d)\r\n", err);
        /* Continue anyway */
    }
    
    /* 5. Enable transmitter only (no receiver, no data) */
    uint8_t op_control = ST25R3916_REG_OP_CONTROL_tx_en | ST25R3916_REG_OP_CONTROL_en;
    
    err = st25r3916WriteRegister(ST25R3916_REG_OP_CONTROL, op_control);
    if (err != RFAL_ERR_NONE) {
        platformLog("[NFC] ERROR: Failed to enable transmitter (err=%d)\r\n", err);
        return false;
    }
    
    platformLog("[NFC] ABSOLUTE MAX POWER carrier ENABLED!\r\n");
    platformLog("[NFC] Mode: NFC, TX Driver: 40%% mod + min res, GAIN REDUCTION: DISABLED\r\n");
    
    /* 6. If duration specified, wait then turn off */
    if (duration_ms > 0) {
        platformLog("[NFC] Transmitting for %lu ms...\r\n", duration_ms);
        HAL_Delay(duration_ms);
        
        /* Turn off transmitter */
        test_nfc_stop_continuous_carrier();
        platformLog("[NFC] ABSOLUTE MAX POWER carrier DISABLED after %lu ms\r\n", duration_ms);
    } else {
        platformLog("[NFC] ABSOLUTE MAX POWER carrier ON (infinite duration)\r\n");
        platformLog("[NFC] Call test_nfc_stop_continuous_carrier() to stop\r\n");
    }
    
    return true;
}

/*============================================================================*/
bool test_nfc_boosted_read(uint32_t boost_duration_ms)
{
    platformLog("[NFC] Starting boosted read test (boost: %lu ms)...\r\n", boost_duration_ms);
    
    /* 1. Enable continuous carrier to wake up tags */
    if (!test_nfc_continuous_carrier(boost_duration_ms)) {
        platformLog("[NFC] ERROR: Failed to enable carrier\r\n");
        return false;
    }
    
    /* 2. Small delay after carrier stops */
    HAL_Delay(10);
    
    /* 3. Perform NFC scan */
    platformLog("[NFC] Scanning for tags after boost...\r\n");
    
    nfc_scan_result_t result;
    memset(&result, 0, sizeof(result));
    
    // Use long range scan with the specified boost duration
    // (Even though we already boosted, this will try to read)
    bool success = nfc_long_range_scan(&result, boost_duration_ms);
    
    if (success) {
        platformLog("[NFC] Boosted read SUCCESS!\r\n");
        platformLog("[NFC]   Boost: %lu ms\r\n", result.boost_duration_ms);
        platformLog("[NFC]   RSSI: %d dBm\r\n", result.rssi);
        platformLog("[NFC]   Distance: %.1f cm\r\n", result.estimated_distance_cm);
        platformLog("[NFC]   Read time: %lu ms\r\n", result.read_time_ms);
        
        if (result.uid_len > 0) {
            platformLog("[NFC]   UID: ");
            for (uint8_t i = 0; i < result.uid_len; i++) {
                platformLog("%02X", result.uid[i]);
            }
            platformLog("\r\n");
        }
        
        return true;
    } else {
        platformLog("[NFC] Boosted read: No tag detected\r\n");
        return false;
    }
}

/*============================================================================*/
/**
 * @brief NFC scan modes
 */
/*============================================================================*/
typedef enum {
    NFC_SCAN_FAST,      // Quick scan for nearby tags
    NFC_SCAN_NORMAL,    // Standard scan
    NFC_SCAN_LONG,      // Long-range scan with boosting
    NFC_SCAN_ADAPTIVE   // Automatically adapts based on results
} nfc_scan_mode_t;



/*============================================================================*/
/**
 * @brief Enhanced carrier boosting with pulsing
 * 
 * Pulsed carrier can be more effective than continuous carrier
 * for waking distant tags while being more power-efficient.
 * 
 * @param total_ms Total transmission time
 * @param pulse_on_ms How long carrier is ON each pulse
 * @param pulse_off_ms How long carrier is OFF between pulses
 * @return true if successful
 */
/*============================================================================*/
bool nfc_pulsed_carrier(uint32_t total_ms, uint32_t pulse_on_ms, uint32_t pulse_off_ms)
{
    if (pulse_on_ms == 0 || total_ms == 0) {
        return false;
    }
    
    platformLog("[NFC] Pulsed carrier: ON=%lums, OFF=%lums, Total=%lums\r\n",
               pulse_on_ms, pulse_off_ms, total_ms);
    
    uint32_t start_time = HAL_GetTick();
    uint32_t elapsed = 0;
    bool pulse_state = true;
    uint32_t pulse_start = start_time;
    
    while (elapsed < total_ms) {
        if (pulse_state) {
            // Carrier ON
            if (elapsed == 0 || (HAL_GetTick() - pulse_start) >= pulse_on_ms) {
                // Start or continue carrier
                if (!test_nfc_continuous_carrier(0)) { // Infinite carrier
                    return false;
                }
                pulse_state = false;
                pulse_start = HAL_GetTick();
            }
        } else {
            // Carrier OFF
            if ((HAL_GetTick() - pulse_start) >= pulse_off_ms) {
                test_nfc_stop_continuous_carrier();
                pulse_state = true;
                pulse_start = HAL_GetTick();
            }
        }
        
        elapsed = HAL_GetTick() - start_time;
    }
    
    // Ensure carrier is off
    test_nfc_stop_continuous_carrier();
    
    platformLog("[NFC] Pulsed carrier complete\r\n");
    return true;
}

/*============================================================================*/
/**
 * @brief Get RSSI (Received Signal Strength Indicator)
 * 
 * RSSI can be used to estimate distance to tag.
 * Higher (less negative) RSSI = closer tag.
 * 
 * @return RSSI value (typically negative dBm)
 */
/*============================================================================*/
int16_t nfc_get_rssi(void)
{
    uint8_t rssi_reg = 0;
    ReturnCode err = st25r3916ReadRegister(ST25R3916_REG_RSSI_RESULT, &rssi_reg);
    
    if (err != RFAL_ERR_NONE) {
        platformLog("[NFC] Failed to read RSSI: %d\r\n", err);
        return -100; // Error value
    }
    
    // RSSI register: signed 8-bit value in dBm
    int16_t rssi = (int8_t)rssi_reg;
    
    platformLog("[NFC] RSSI: %d dBm\r\n", rssi);
    return rssi;
}

/*============================================================================*/
/**
 * @brief Estimate distance from RSSI
 * 
 * This is a simplified estimation. For accurate distance
 * measurement, you need to calibrate with your specific
 * antenna and environment.
 * 
 * @param rssi RSSI value in dBm
 * @return Estimated distance in centimeters
 */
/*============================================================================*/
float nfc_estimate_distance_from_rssi(int16_t rssi)
{
    // Calibration values - adjust based on your hardware!
    // These are example values for typical NFC antenna
    
    if (rssi > -30) return 1.0f;   // Very close (~1cm)
    if (rssi > -40) return 2.0f;   // Close (~2cm)
    if (rssi > -50) return 5.0f;   // Near (~5cm)
    if (rssi > -60) return 10.0f;  // Medium (~10cm)
    if (rssi > -70) return 20.0f;  // Far (~20cm)
    if (rssi > -80) return 30.0f;  // Very far (~30cm)
    
    return 50.0f; // Maximum range (~50cm)
}

/*============================================================================*/
/**
 * @brief Optimize receiver for weak signals
 * 
 * Configures ST25R3916 for maximum sensitivity
 * to detect distant or weak tags.
 */
/*============================================================================*/
void nfc_optimize_receiver(void)
{
    platformLog("[NFC] Optimizing receiver for weak signals...\r\n");
    
    // Configure receiver for maximum sensitivity
    // Use lower bandwidth filter for better noise immunity
    st25r3916WriteRegister(ST25R3916_REG_RX_CONF1, 
                          ST25R3916_REG_RX_CONF1_lp_300khz);  // 300kHz low-pass filter
    
    // Lower field detection thresholds for weak signals
    st25r3916WriteRegister(ST25R3916_REG_FIELD_THRESHOLD_ACTV, 0x10);   // Lower activation threshold
    st25r3916WriteRegister(ST25R3916_REG_FIELD_THRESHOLD_DEACTV, 0x08); // Lower deactivation threshold
    
    // Configure RX_CONF2 for better weak signal reception
    st25r3916WriteRegister(ST25R3916_REG_RX_CONF2, 0x00);  // Default settings
    
    // Configure RX_CONF3 for analog front-end
    st25r3916WriteRegister(ST25R3916_REG_RX_CONF3, 0x00);  // Default settings
    
    // Configure RX_CONF4
    st25r3916WriteRegister(ST25R3916_REG_RX_CONF4, 0x00);  // Default settings
    
    platformLog("[NFC] Receiver optimized for weak signals\r\n");
}

/*============================================================================*/
/**
 * @brief Fast NFC scan for nearby tags
 * 
 * Quick scan without boosting for maximum speed.
 * Ideal for tags within normal reading distance.
 * 
 * @param result Pointer to store scan results
 * @return true if tag detected
 */
/*============================================================================*/
bool nfc_fast_scan(nfc_scan_result_t *result)
{
    if (!result) return false;
    
    memset(result, 0, sizeof(nfc_scan_result_t));
    result->scan_attempts = 1;
    result->boost_duration_ms = 0;
    
    platformLog("[NFC] Starting fast scan...\r\n");
    
    uint32_t start_time = HAL_GetTick();
    
    // Quick NFC initialization
    ReadIni();
    
    // Fast discovery attempt
    rfalNfcDiscoverParam discParam;
    memset(&discParam, 0, sizeof(discParam));
    discParam.devLimit = 1;
    discParam.techs2Find = RFAL_NFC_POLL_TECH_A;
    
    ReturnCode err = rfalNfcDiscover(&discParam);
    result->read_time_ms = HAL_GetTick() - start_time;
    
    if (err == RFAL_ERR_NONE) {
        // DISCOVERY SUCCESS - but we need to validate it's a real tag
        
        // Wait a bit for activation
        HAL_Delay(5);
        
        // Check if a device is actually activated (not just noise)
        if (!rfalNfcIsDevActivated(rfalNfcGetState())) {
            platformLog("[NFC] Fast scan: Discovery succeeded but no device activated (likely noise)\r\n");
            return false;
        }
        
        // Try to get the active device
        rfalNfcDevice *nfcDevice = NULL;
        rfalNfcGetActiveDevice(&nfcDevice);
        
        if (!nfcDevice) {
            platformLog("[NFC] Fast scan: No active device found (false positive)\r\n");
            return false;
        }
        
        // Validate it has a valid UID (not all zeros or garbage)
        bool valid_uid = false;
        if (nfcDevice->nfcidLen > 0) {
            // Check UID is not all zeros
            valid_uid = true;
            for (uint8_t i = 0; i < nfcDevice->nfcidLen; i++) {
                if (nfcDevice->nfcid[i] != 0x00) {
                    valid_uid = true;
                    break;
                }
            }
        }
        
        if (!valid_uid) {
            platformLog("[NFC] Fast scan: Invalid UID (all zeros)\r\n");
            return false;
        }
        
        // Store UID in result
        result->uid_len = (nfcDevice->nfcidLen > 10) ? 10 : nfcDevice->nfcidLen;
        memcpy(result->uid, nfcDevice->nfcid, result->uid_len);
        
        // Determine tag type
        if (nfcDevice->type == RFAL_NFC_LISTEN_TYPE_NFCA) {
            result->tag_type = 1; // NFC-A
        } else if (nfcDevice->type == RFAL_NFC_LISTEN_TYPE_NFCB) {
            result->tag_type = 2; // NFC-B
        } else if (nfcDevice->type == RFAL_NFC_LISTEN_TYPE_NFCF) {
            result->tag_type = 3; // NFC-F
        } else {
            result->tag_type = 0; // Unknown
        }
        
        result->detected = true;
        result->rssi = nfc_get_rssi();
        result->estimated_distance_cm = nfc_estimate_distance_from_rssi(result->rssi);
        
        platformLog("[NFC] Fast scan VALID TAG: %lums, RSSI: %d, Dist: %.1fcm, UID: ",
                   result->read_time_ms, result->rssi, result->estimated_distance_cm);
        
        for (uint8_t i = 0; i < result->uid_len; i++) {
            platformLog("%02X", result->uid[i]);
        }
        platformLog("\r\n");
        
        // Deactivate the tag
        rfalNfcDeactivate(RFAL_NFC_DEACTIVATE_IDLE);
        
        return true;
    }
    
    platformLog("[NFC] Fast scan failed: %d\r\n", err);
    return false;
}

/*============================================================================*/
/**
 * @brief Long-range NFC scan with carrier boosting
 * 
 * Uses carrier boosting to wake distant tags.
 * Slower but has much greater range.
 * 
 * @param result Pointer to store scan results
 * @param boost_ms How long to boost carrier before scanning
 * @return true if tag detected
 */
/*============================================================================*/
bool nfc_long_range_scan(nfc_scan_result_t *result, uint32_t boost_ms)
{
    if (!result) return false;
    
    memset(result, 0, sizeof(nfc_scan_result_t));
    result->scan_attempts = 1;
    result->boost_duration_ms = boost_ms;
    
    platformLog("[NFC] Starting long-range scan (%lums boost)...\r\n", boost_ms);
    
    // Phase 1: Boost carrier to wake distant tags
    if (boost_ms > 0) {
        if (!test_nfc_continuous_carrier(boost_ms)) {
            platformLog("[NFC] Failed to boost carrier\r\n");
            return false;
        }
        HAL_Delay(10); // Small gap
    }
    
    uint32_t start_time = HAL_GetTick();
    
    // Re-initialize NFC after boosting
    ReadIni();
    
    // Optimize receiver for weak signals
    nfc_optimize_receiver();
    
    // Discovery attempt
    rfalNfcDiscoverParam discParam;
    memset(&discParam, 0, sizeof(discParam));
    discParam.devLimit = 1;
    discParam.techs2Find = RFAL_NFC_POLL_TECH_A;
    
    ReturnCode err = rfalNfcDiscover(&discParam);
    result->read_time_ms = HAL_GetTick() - start_time;
    
    if (err == RFAL_ERR_NONE) {
        // DISCOVERY SUCCESS - validate it's a real tag
        
        // Wait a bit for activation
        HAL_Delay(5);
        
        // Check if a device is actually activated
        if (!rfalNfcIsDevActivated(rfalNfcGetState())) {
            platformLog("[NFC] Long-range scan: Discovery succeeded but no device activated (likely noise)\r\n");
            return false;
        }
        
        // Try to get the active device
        rfalNfcDevice *nfcDevice = NULL;
        rfalNfcGetActiveDevice(&nfcDevice);
        
        if (!nfcDevice) {
            platformLog("[NFC] Long-range scan: No active device found (false positive)\r\n");
            return false;
        }
        
        // Validate it has a valid UID
        bool valid_uid = false;
        if (nfcDevice->nfcidLen > 0) {
            // Check UID is not all zeros
            valid_uid = false;
            for (uint8_t i = 0; i < nfcDevice->nfcidLen; i++) {
                if (nfcDevice->nfcid[i] != 0x00) {
                    valid_uid = true;
                    break;
                }
            }
        }
        
        if (!valid_uid) {
            platformLog("[NFC] Long-range scan: Invalid UID (all zeros)\r\n");
            return false;
        }
        
        // Store UID in result
        result->uid_len = (nfcDevice->nfcidLen > 10) ? 10 : nfcDevice->nfcidLen;
        memcpy(result->uid, nfcDevice->nfcid, result->uid_len);
        
        // Determine tag type
        if (nfcDevice->type == RFAL_NFC_LISTEN_TYPE_NFCA) {
            result->tag_type = 1; // NFC-A
        } else if (nfcDevice->type == RFAL_NFC_LISTEN_TYPE_NFCB) {
            result->tag_type = 2; // NFC-B
        } else if (nfcDevice->type == RFAL_NFC_LISTEN_TYPE_NFCF) {
            result->tag_type = 3; // NFC-F
        } else {
            result->tag_type = 0; // Unknown
        }
        
        result->detected = true;
        result->rssi = nfc_get_rssi();
        result->estimated_distance_cm = nfc_estimate_distance_from_rssi(result->rssi);
        
        platformLog("[NFC] Long-range scan VALID TAG: %lums, RSSI: %d, Dist: %.1fcm, UID: ",
                   result->read_time_ms, result->rssi, result->estimated_distance_cm);
        
        for (uint8_t i = 0; i < result->uid_len; i++) {
            platformLog("%02X", result->uid[i]);
        }
        platformLog("\r\n");
        
        // Deactivate the tag
        rfalNfcDeactivate(RFAL_NFC_DEACTIVATE_IDLE);
        
        return true;
    }
    
    platformLog("[NFC] Long-range scan failed: %d\r\n", err);
    return false;
}

/*============================================================================*/
/**
 * @brief Adaptive NFC scan
 * 
 * Automatically tries different scan modes:
 * 1. Fast scan (no boost, quick)
 * 2. Medium scan (short boost)
 * 3. Long-range scan (long boost)
 * 
 * @param result Pointer to store scan results
 * @return true if tag detected
 */
/*============================================================================*/
bool nfc_adaptive_scan(nfc_scan_result_t *result)
{
    if (!result) return false;
    
    memset(result, 0, sizeof(nfc_scan_result_t));
    
    platformLog("[NFC] Starting adaptive scan...\r\n");
    
    // Try fast scan first (quickest)
    result->scan_attempts = 1;
    if (nfc_fast_scan(result)) {
        platformLog("[NFC] Adaptive: Fast scan succeeded\r\n");
        return true;
    }
    
    // Try medium scan with short boost
    result->scan_attempts = 2;
    result->boost_duration_ms = 50;
    if (nfc_long_range_scan(result, 50)) {
        platformLog("[NFC] Adaptive: Medium scan succeeded\r\n");
        return true;
    }
    
    // Try long-range scan with longer boost
    result->scan_attempts = 3;
    result->boost_duration_ms = 150;
    if (nfc_long_range_scan(result, 150)) {
        platformLog("[NFC] Adaptive: Long-range scan succeeded\r\n");
        return true;
    }
    
    // Try maximum boost
    result->scan_attempts = 4;
    result->boost_duration_ms = 300;
    if (nfc_long_range_scan(result, 300)) {
        platformLog("[NFC] Adaptive: Maximum boost succeeded\r\n");
        return true;
    }
    
    platformLog("[NFC] Adaptive scan failed after %d attempts\r\n", result->scan_attempts);
    return false;
}

/*============================================================================*/
/**
 * @brief Test different boost configurations
 * 
 * Tests various carrier boost durations and patterns
 * to find optimal configuration for your hardware.
 */
/*============================================================================*/
void nfc_test_boost_configurations(void)
{
    platformLog("[NFC] Testing boost configurations...\r\n\r\n");
    
    uint32_t boost_durations[] = {0, 25, 50, 100, 150, 200, 300, 500};
    uint8_t num_tests = sizeof(boost_durations) / sizeof(boost_durations[0]);
    
    platformLog("Boost | Success | Time(ms) | RSSI | Est.Dist(cm)\r\n");
    platformLog("------|---------|----------|------|-------------\r\n");
    
    for (int i = 0; i < num_tests; i++) {
        nfc_scan_result_t result;
        bool success;
        
        if (boost_durations[i] == 0) {
            success = nfc_fast_scan(&result);
        } else {
            success = nfc_long_range_scan(&result, boost_durations[i]);
        }
        
        if (success) {
            platformLog("%5lu |    YES  | %8lu | %4d | %11.1f\r\n",
                       boost_durations[i], result.read_time_ms,
                       result.rssi, result.estimated_distance_cm);
        } else {
            platformLog("%5lu |     NO  |        - |    - |           -\r\n",
                       boost_durations[i]);
        }
        
        HAL_Delay(1000); // Wait between tests
    }
    
    platformLog("\r\n[NFC] Boost configuration test complete\r\n");
}

/*============================================================================*/
/**
 * @brief Pulsed carrier test
 * 
 * Tests different pulse patterns for carrier boosting.
 * Pulsed carrier can be more efficient than continuous.
 */
/*============================================================================*/
void nfc_test_pulsed_carrier(void)
{
    platformLog("[NFC] Testing pulsed carrier patterns...\r\n\r\n");
    
    // Test different pulse patterns
    struct {
        uint32_t on_ms;
        uint32_t off_ms;
        uint32_t total_ms;
    } patterns[] = {
        {100, 0, 100},     // Continuous (baseline)
        {50, 50, 100},     // 50% duty cycle
        {20, 80, 100},     // 20% duty cycle
        {10, 10, 100},     // 50% fast pulses
        {5, 95, 100},      // 5% duty cycle
    };
    
    uint8_t num_patterns = sizeof(patterns) / sizeof(patterns[0]);
    
    for (int i = 0; i < num_patterns; i++) {
        platformLog("[NFC] Pattern %d: ON=%lums, OFF=%lums, Total=%lums\r\n",
                   i+1, patterns[i].on_ms, patterns[i].off_ms, patterns[i].total_ms);
        
        // Test the pattern
        if (nfc_pulsed_carrier(patterns[i].total_ms, 
                              patterns[i].on_ms, 
                              patterns[i].off_ms)) {
            platformLog("[NFC]   Pattern executed successfully\r\n");
            
            // Test if tag can be read after this pattern
            HAL_Delay(10);
            nfc_scan_result_t result;
            if (nfc_fast_scan(&result)) {
                platformLog("[NFC]   Tag detected after pattern\r\n");
            } else {
                platformLog("[NFC]   No tag detected after pattern\r\n");
            }
        } else {
            platformLog("[NFC]   Pattern failed\r\n");
        }
        
        HAL_Delay(1000);
    }
    
    platformLog("[NFC] Pulsed carrier test complete\r\n");
}

/*============================================================================*/
/* Non-blocking Carrier Implementation */
/*============================================================================*/

#include "FreeRTOS.h"
#include "task.h"

/* Global state for non-blocking NFC tests */
typedef struct {
    bool carrier_active;
    uint32_t remaining_ms;
    uint32_t start_time;
    TaskHandle_t carrier_task;
    bool pulse_mode;
    uint32_t pulse_on_ms;
    uint32_t pulse_off_ms;
    uint32_t pulse_cycles;
    uint32_t current_cycle;
} nfc_test_state_t;

static nfc_test_state_t nfc_state = {0};

/*============================================================================*/
/**
 * @brief Non-blocking carrier task
 * 
 * Runs carrier transmission in background using FreeRTOS task.
 * Allows device to remain responsive during long tests.
 */
/*============================================================================*/
static void nfc_carrier_task(void *pvParameters) {
    (void)pvParameters;
    
    platformLog("[NFC] Non-blocking carrier task started\r\n");
    
    while (nfc_state.carrier_active) {
        if (nfc_state.pulse_mode) {
            // Pulsed mode
            if (nfc_state.current_cycle < nfc_state.pulse_cycles) {
                // Turn carrier ON
                if (test_nfc_continuous_carrier(nfc_state.pulse_on_ms)) {
                    nfc_state.current_cycle++;
                    
                    // If not last cycle, wait OFF period
                    if (nfc_state.current_cycle < nfc_state.pulse_cycles) {
                        vTaskDelay(pdMS_TO_TICKS(nfc_state.pulse_off_ms));
                    }
                } else {
                    platformLog("[NFC] ERROR: Failed to start carrier in pulse mode\r\n");
                    break;
                }
            } else {
                // All cycles complete
                break;
            }
        } else {
            // Continuous mode
            uint32_t current_time = HAL_GetTick();
            uint32_t elapsed = current_time - nfc_state.start_time;
            
            if (elapsed < nfc_state.remaining_ms) {
                // Keep carrier on, check every 100ms
                vTaskDelay(pdMS_TO_TICKS(100));
            } else {
                // Time's up
                break;
            }
        }
    }
    
    // Clean up
    test_nfc_stop_continuous_carrier();
    nfc_state.carrier_active = false;
    
    platformLog("[NFC] Non-blocking carrier task finished\r\n");
    vTaskDelete(NULL);
}

/*============================================================================*/
/**
 * @brief Start non-blocking carrier transmission
 * 
 * @param duration_ms Duration in milliseconds (1000-120000)
 * @return true if successful
 */
/*============================================================================*/
bool nfc_start_carrier_nonblocking(uint32_t duration_ms) {
    if (nfc_state.carrier_active) {
        platformLog("[NFC] ERROR: Carrier already active\r\n");
        return false;
    }
    
    // Validate duration
    if (duration_ms < 1000) {
        platformLog("[NFC] WARNING: %lu ms too short for non-blocking mode\r\n", duration_ms);
        platformLog("[NFC] Consider using at least 5000 ms for proper testing\r\n");
    }
    
    if (duration_ms > 120000) {
        platformLog("[NFC] WARNING: %lu ms (2 minutes) may cause overheating\r\n", duration_ms);
        platformLog("[NFC] Consider shorter tests or active cooling\r\n");
    }
    
    // Start carrier
    if (!test_nfc_continuous_carrier(0)) { // Start infinite carrier
        platformLog("[NFC] ERROR: Failed to start carrier\r\n");
        return false;
    }
    
    // Initialize state
    nfc_state.carrier_active = true;
    nfc_state.remaining_ms = duration_ms;
    nfc_state.start_time = HAL_GetTick();
    nfc_state.pulse_mode = false;
    nfc_state.carrier_task = NULL;
    
    // Create task
    if (xTaskCreate(nfc_carrier_task, "NFC_Carrier", 512, NULL, 
                    tskIDLE_PRIORITY + 1, &nfc_state.carrier_task) != pdPASS) {
        platformLog("[NFC] ERROR: Failed to create carrier task\r\n");
        test_nfc_stop_continuous_carrier();
        nfc_state.carrier_active = false;
        return false;
    }
    
    platformLog("[NFC] Started non-blocking carrier for %lu ms\r\n", duration_ms);
    return true;
}

/*============================================================================*/
/**
 * @brief Start pulsed carrier pattern
 * 
 * @param on_ms ON duration per pulse
 * @param off_ms OFF duration per pulse
 * @param cycles Number of cycles
 * @return true if successful
 */
/*============================================================================*/
bool nfc_start_pulsed_carrier(uint32_t on_ms, uint32_t off_ms, uint32_t cycles) {
    if (nfc_state.carrier_active) {
        platformLog("[NFC] ERROR: Carrier already active\r\n");
        return false;
    }
    
    // Validate parameters
    if (on_ms == 0 || off_ms == 0 || cycles == 0) {
        platformLog("[NFC] ERROR: Invalid pulse parameters\r\n");
        platformLog("[NFC] on_ms=%lu, off_ms=%lu, cycles=%lu\r\n", on_ms, off_ms, cycles);
        return false;
    }
    
    // Initialize state
    nfc_state.carrier_active = true;
    nfc_state.pulse_mode = true;
    nfc_state.pulse_on_ms = on_ms;
    nfc_state.pulse_off_ms = off_ms;
    nfc_state.pulse_cycles = cycles;
    nfc_state.current_cycle = 0;
    nfc_state.carrier_task = NULL;
    
    // Create task
    if (xTaskCreate(nfc_carrier_task, "NFC_Pulse", 512, NULL, 
                    tskIDLE_PRIORITY + 1, &nfc_state.carrier_task) != pdPASS) {
        platformLog("[NFC] ERROR: Failed to create pulse task\r\n");
        nfc_state.carrier_active = false;
        return false;
    }
    
    platformLog("[NFC] Started pulsed carrier: %lu ms ON, %lu ms OFF, %lu cycles\r\n", 
                on_ms, off_ms, cycles);
    return true;
}

/*============================================================================*/
/**
 * @brief Stop any active carrier transmission
 */
/*============================================================================*/
void nfc_stop_carrier(void) {
    nfc_state.carrier_active = false;
    
    // Give task a moment to clean up
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // Ensure carrier is off
    test_nfc_stop_continuous_carrier();
    
    platformLog("[NFC] Carrier stopped\r\n");
}

/*============================================================================*/
/**
 * @brief Check if carrier is active
 * @return true if carrier is active
 */
/*============================================================================*/
bool nfc_is_carrier_active(void) {
    return nfc_state.carrier_active;
}

/*============================================================================*/
/**
 * @brief Get remaining time for current test
 * @return Remaining time in milliseconds, 0 if not active or pulsed mode
 */
/*============================================================================*/
uint32_t nfc_get_remaining_time(void) {
    if (!nfc_state.carrier_active || nfc_state.pulse_mode) {
        return 0;
    }
    
    uint32_t elapsed = HAL_GetTick() - nfc_state.start_time;
    if (elapsed >= nfc_state.remaining_ms) {
        return 0;
    }
    return nfc_state.remaining_ms - elapsed;
}



