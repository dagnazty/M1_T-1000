/* See COPYING.txt for license details. */

/*
 * m1_link.c
 *
 * M1 Link — device-to-device wireless link between M1 units over the Si4463
 * sub-GHz radio (915 MHz FSK packet mode). Phase 0: radio bring-up + raw
 * fixed-length packet TX/RX and a beacon/listen spike to prove the air
 * interface. See documentation/m1_link_design.md and plan.md.
 *
 * IMPORTANT — why this file includes the FSK radio config directly:
 * m1_sub_ghz_api.c includes BOTH the OOK and FSK 915 headers, which share the
 * include guard RADIO_915_CONFIG_H_. The OOK header is included first, so the
 * FSK config is guarded out there and RadioConfiguration_915 is actually the
 * OOK config. The existing sub-GHz app also transmits in direct/raw mode, not
 * the FIFO packet path. To get a real FSK *packet* link (hardware preamble,
 * sync word and CRC-16) we load the genuine FSK config array here, in a
 * translation unit that includes ONLY the FSK header.
 *
 * M1 Project
 */

#include "m1_link.h"

#ifdef M1_APP_LINK_ENABLE

#include <stdio.h>
#include <string.h>
#include "main.h"
#include "m1_sub_ghz.h"       /* S_M1_SubGHz_Band, SUB_GHZ_BAND_915 */
#include "m1_sub_ghz_api.h"   /* Si4463 packet-mode driver API */
#include "m1_crypto.h"        /* AES-256-CBC */
#include "m1_md5_hash.h"      /* passphrase -> key KDF */
#include "ff.h"               /* FatFs (config load/save) */
#include "m1_buzzer.h"        /* locate: beep */
#include "m1_led_indicator.h" /* locate: LED flash */

#include "m1_badusb.h"        /* remote trigger: BadUSB */
#include "m1_badbt.h"         /* remote trigger: Bad-BT */
#include "m1_ir_universal.h"  /* remote trigger: IR */

/* Remote trigger: replay a saved .sub on the receiving unit. */
extern uint8_t sub_ghz_replay_flipper_file(const char *sub_path);

/* FSK packet configuration array (this TU does not include the OOK 915 header,
 * so RADIO_915_CONFIGURATION_DATA_ARRAY resolves to the FSK values). */
#include "radio_config/m1_sub_ghz_915_rxtx_spc25_bwauto_baud106_dev200_e1_10_fsk.h"

static const uint8_t link_fsk_cfg_array[] = RADIO_915_CONFIGURATION_DATA_ARRAY;

/* Packet-Handler pending-flag masks (from si446x_cmd.h) */
#define LINK_PH_PACKET_SENT   SI446X_CMD_GET_INT_STATUS_REP_PH_PEND_PACKET_SENT_PEND_MASK
#define LINK_PH_PACKET_RX     SI446X_CMD_GET_INT_STATUS_REP_PH_PEND_PACKET_RX_PEND_MASK
#define LINK_PH_CRC_ERROR     SI446X_CMD_GET_INT_STATUS_REP_PH_PEND_CRC_ERROR_PEND_MASK

#define LINK_TX_TIMEOUT_MS    500u

/* Listen-before-talk: raw-RSSI threshold above which the channel is "busy". */
#define LINK_LBT_RSSI_BUSY    90u    /* ~ -85 dBm */
#define LINK_LBT_MAX_WAIT_MS  60u

static bool link_radio_ready = false;

/* --- Phase 4 configuration --- */
static m1_link_cfg_t s_cfg;
static uint8_t       s_key[M1_CRYPTO_AES_KEY_SIZE];
static bool          s_key_valid = false;
static const uint8_t s_tx_power_tbl[4] = { 10, 40, 80, 127 };

/* Convert a raw Si4463 RSSI reading to an approximate dBm (fits int8_t). */
static int8_t link_rssi_to_dbm(uint8_t raw)
{
    return (int8_t)((raw >> 1) - 130);
}

/*============================================================================*/
/*  Phase 4: configuration + key derivation                                   */
/*============================================================================*/

/* Derive a 32-byte AES key from the passphrase using two salted MD5 hashes.
 * Deterministic, so two units sharing a passphrase get the same key. (Not a
 * slow KDF like PBKDF2 — adequate for a low-value link, noted in the plan.) */
static void link_derive_key(void)
{
    uint8_t plen = (uint8_t)strlen(s_cfg.passphrase);
    uint8_t h[16];

    if ( plen == 0 )
    {
        s_key_valid = false;
        memset(s_key, 0, sizeof(s_key));
        return;
    }

    mh_md5_init(0, 0);
    mh_md5_update((const uint8_t *)"M1Lk", 4);
    mh_md5_update((const uint8_t *)s_cfg.passphrase, plen);
    mh_md5_final(h);
    memcpy(&s_key[0], h, 16);

    mh_md5_init(0, 0);
    mh_md5_update((const uint8_t *)s_cfg.passphrase, plen);
    mh_md5_update((const uint8_t *)"K2y!", 4);
    mh_md5_final(h);
    memcpy(&s_key[16], h, 16);

    s_key_valid = true;
}

m1_link_cfg_t *m1_link_cfg(void)      { return &s_cfg; }
bool           m1_link_encrypted(void) { return s_key_valid; }

void m1_link_cfg_defaults(void)
{
    memset(&s_cfg, 0, sizeof(s_cfg));
    snprintf(s_cfg.callsign, sizeof(s_cfg.callsign), "M1-%04X",
             (unsigned)m1_link_my_id());
    s_cfg.passphrase[0] = '\0';
    s_cfg.channel = 0;
    s_cfg.tx_power_idx = 3;   /* max */
}

/* Retune the radio to the current channel's absolute frequency. The radio must
 * be initialised (call after bring-up or once link_radio_ready). */
static void link_set_channel_freq(void)
{
    SI446x_Change_State(SI446X_CMD_CHANGE_STATE_ARG_NEXT_STATE1_NEW_STATE_ENUM_READY);
    SI446x_Set_Frequency(M1_LINK_CH_BASE_HZ + (uint32_t)s_cfg.channel * M1_LINK_CH_STEP_HZ);
}

void m1_link_cfg_apply(void)
{
    if ( s_cfg.channel > M1_LINK_CHAN_MAX ) s_cfg.channel = M1_LINK_CHAN_MAX;
    if ( s_cfg.tx_power_idx > 3 )           s_cfg.tx_power_idx = 3;
    if ( s_cfg.callsign[0] == '\0' )
        snprintf(s_cfg.callsign, sizeof(s_cfg.callsign), "M1-%04X",
                 (unsigned)m1_link_my_id());
    link_derive_key();
    /* Apply the channel frequency live if the radio is already up (e.g. the
     * settings screen changed the channel mid-session). */
    if ( link_radio_ready )
        link_set_channel_freq();
}

void m1_link_cfg_load(void)
{
    FIL     fp;
    UINT    br = 0;
    m1_link_cfg_t tmp;

    m1_link_cfg_defaults();
    if ( f_open(&fp, M1_LINK_CFG_PATH, FA_READ) == FR_OK )
    {
        if ( f_read(&fp, &tmp, sizeof(tmp), &br) == FR_OK && br == sizeof(tmp) )
        {
            /* Ensure strings are terminated before trusting them. */
            tmp.callsign[sizeof(tmp.callsign) - 1] = '\0';
            tmp.passphrase[sizeof(tmp.passphrase) - 1] = '\0';
            s_cfg = tmp;
        }
        f_close(&fp);
    }
    m1_link_cfg_apply();
}

void m1_link_cfg_save(void)
{
    FIL  fp;
    UINT bw = 0;

    m1_link_cfg_apply();
    if ( f_open(&fp, M1_LINK_CFG_PATH, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK )
    {
        f_write(&fp, &s_cfg, sizeof(s_cfg), &bw);
        f_close(&fp);
    }
}

static bool s_cfg_loaded = false;

/*============================================================================*/
bool m1_link_radio_bringup(void)
{
    /* Load config once (defaults + SD) so channel/power/callsign/key are valid. */
    if ( !s_cfg_loaded )
    {
        m1_link_cfg_load();
        s_cfg_loaded = true;
    }

    /* Reset + power the radio, then load the FSK packet config. Retry on the
     * same terms as radio_init_rx_tx(): re-power and re-load on failure. */
    SI446x_PowerUp();

    uint8_t attempts = 0;
    while ( SI446X_SUCCESS != SI446x_ConfigInit(link_fsk_cfg_array) )
    {
        HAL_Delay(1);
        SI446x_PowerUp();
        if ( ++attempts > 8 )
        {
            link_radio_ready = false;
            return false;
        }
    }

    SI446x_Select_Frontend(SUB_GHZ_BAND_915);

    /* --- Fix up the packet handler ---
     * The loaded 915 FSK config is a raw/direct-mode config: PREAMBLE_TX_LENGTH=0,
     * SYNC skipped on TX, packet fields zeroed. That radiates energy but carries
     * no preamble/sync for the receiver to lock onto (confirmed on hardware:
     * RSSI rises but modem never detects preamble/sync). Override with a proper
     * fixed-length packet: 8-byte preamble, 2-byte sync word, one 25-byte field. */

    /* PREAMBLE group (0x10): TX_LENGTH=8, STD_1=0x14, NSTD=0, STD_2=0x0F,
     * CONFIG=0x31 (standard 1010 preamble, length in bytes, standard RX). */
    {
        static const uint8_t preamble[] = { 0x08, 0x14, 0x00, 0x0F, 0x31 };
        SI446x_Set_Property(0x10, 0x00, sizeof(preamble), preamble);
    }

    /* SYNC group (0x11): CONFIG=0x01 (TX enabled, LENGTH=1 -> 2 bytes),
     * SYNC word = 0x2D 0xD4. */
    {
        static const uint8_t sync[] = { 0x01, 0x2D, 0xD4, 0x00, 0x00 };
        SI446x_Set_Property(0x11, 0x00, sizeof(sync), sync);
    }

    /* PKT group (0x12): fixed length, no CRC, single 25-byte field for both
     * TX (FIELD_1) and RX (RX_FIELD_1). */
    {
        static const uint8_t crc_cfg[]   = { 0x00 };             /* PKT_CRC_CONFIG: no CRC */
        static const uint8_t pkt_cfg1[]  = { 0x00 };             /* PKT_CONFIG1 */
        /* PKT_LEN(0x0C)=0, LEN_FIELD_SOURCE(0x0D)=0 */
        static const uint8_t pkt_len[]   = { 0x00, 0x00 };
        /* FIELD_1: LEN_12_8(0x11)=0, LEN_7_0(0x12)=25, CONFIG(0x13)=0, CRC(0x14)=0 */
        static const uint8_t tx_field[]  = { 0x00, M1_LINK_PKT_LEN, 0x00, 0x00 };
        /* RX_FIELD_1: LEN_12_8(0x21)=0, LEN_7_0(0x22)=25, CONFIG(0x23)=0, CRC(0x24)=0 */
        static const uint8_t rx_field[]  = { 0x00, M1_LINK_PKT_LEN, 0x00, 0x00 };
        SI446x_Set_Property(0x12, 0x00, sizeof(crc_cfg),  crc_cfg);
        SI446x_Set_Property(0x12, 0x06, sizeof(pkt_cfg1), pkt_cfg1);
        SI446x_Set_Property(0x12, 0x0C, sizeof(pkt_len),  pkt_len);
        SI446x_Set_Property(0x12, 0x11, sizeof(tx_field), tx_field);
        SI446x_Set_Property(0x12, 0x21, sizeof(rx_field), rx_field);
    }

    /* Put this channel on its own absolute frequency (channels are 1 MHz apart,
     * far wider than the RX bandwidth, so they don't overlap). TX/RX then run on
     * radio-channel 0 against this frequency. */
    SI446x_Change_State(SI446X_CMD_CHANGE_STATE_ARG_NEXT_STATE1_NEW_STATE_ENUM_READY);
    SI446x_Set_Frequency(M1_LINK_CH_BASE_HZ + (uint32_t)s_cfg.channel * M1_LINK_CH_STEP_HZ);

    SI446x_Get_IntStatus(0, 0, 0);   /* clear any stale pending flags */

    link_radio_ready = true;
    return true;
}

/*============================================================================*/
bool m1_link_tx_raw(const uint8_t *buf, uint8_t len)
{
    uint8_t pkt[M1_LINK_PKT_LEN];
    uint32_t waited = 0;

    if ( !link_radio_ready || buf==NULL )
        return false;

    /* Fixed-length packets: pad short payloads, truncate long ones. */
    if ( len > M1_LINK_PKT_LEN )
        len = M1_LINK_PKT_LEN;
    memset(pkt, 0x00, sizeof(pkt));
    memcpy(pkt, buf, len);

    radio_set_antenna_mode(RADIO_ANTENNA_MODE_TX);
    SI446x_Set_Tx_Power(s_tx_power_tbl[s_cfg.tx_power_idx]);
    SI446x_Get_IntStatus(0, 0, 0);

    /* Fill the TX FIFO and start transmission (returns to READY when done).
     * Radio-channel 0: the channel is selected via the absolute frequency. */
    SI446x_Start_Tx(0, pkt, M1_LINK_PKT_LEN);

    /* Poll for the PACKET_SENT packet-handler interrupt. */
    while ( waited < LINK_TX_TIMEOUT_MS )
    {
        SI446x_Get_IntStatus(0, 0, 0);
        if ( SI446x_Get_PH_Pend() & LINK_PH_PACKET_SENT )
            return true;
        HAL_Delay(2);
        waited += 2;
    }
    return false;
}

/*============================================================================*/
void m1_link_rx_arm(void)
{
    if ( !link_radio_ready )
        return;

    radio_set_antenna_mode(RADIO_ANTENNA_MODE_RX);
    SI446x_FiFoInfo(0x02);           /* reset the RX FIFO (RX bit) */
    SI446x_Get_IntStatus(0, 0, 0);   /* clear pending flags */
    SI446x_Start_Rx(0);              /* channel selected via absolute frequency */
}

/* Listen-before-talk: true if the channel currently has significant energy.
 * Assumes the radio is in RX (call after m1_link_rx_arm). */
static bool link_channel_busy(void)
{
    struct si446x_reply_GET_MODEM_STATUS_map *ms = SI446x_Get_ModemStatus(0);
    return (ms->CURR_RSSI >= LINK_LBT_RSSI_BUSY);
}

/*============================================================================*/
uint8_t m1_link_rx_poll(uint8_t *buf, int8_t *rssi)
{
    uint8_t ph;
    uint8_t count;

    if ( !link_radio_ready || buf==NULL )
        return 0;

    SI446x_Get_IntStatus(0, 0, 0);
    ph = SI446x_Get_PH_Pend();

    if ( !(ph & LINK_PH_PACKET_RX) )
        return 0;   /* nothing acquired yet */

    if ( ph & LINK_PH_CRC_ERROR )
    {
        /* Corrupt frame — drop it and re-arm. */
        m1_link_rx_arm();
        return 0;
    }

    if ( rssi != NULL )
    {
        /* Use CURR_RSSI: the config does not latch RSSI at sync detect, so
         * LATCH_RSSI reads 0 here. CURR_RSSI is still valid right after RX. */
        struct si446x_reply_GET_MODEM_STATUS_map *ms = SI446x_Get_ModemStatus(0);
        *rssi = link_rssi_to_dbm(ms->CURR_RSSI);
    }

    count = SI446x_Get_RxFifoCount();
    if ( count == 0 || count > M1_LINK_PKT_LEN )
        count = M1_LINK_PKT_LEN;   /* fixed-length: expect a full packet */

    SI446x_Read_RxFiFo(count, buf);

    m1_link_rx_arm();   /* ready for the next packet */
    return count;
}

/*============================================================================*/
/*  Phase 1: framing + reliable unicast                                       */
/*============================================================================*/

/* This unit's stable 16-bit short address, folded from the 96-bit STM32 UID.
 * 0x0000 and 0xFFFF are reserved (0xFFFF = broadcast), so remap those. */
uint16_t m1_link_my_id(void)
{
    static uint16_t cached = 0;
    if ( cached == 0 )
    {
        uint32_t u = HAL_GetUIDw0() ^ HAL_GetUIDw1() ^ HAL_GetUIDw2();
        uint16_t id = (uint16_t)((u & 0xFFFF) ^ (u >> 16));
        if ( id == 0x0000 || id == 0xFFFF )
            id = 0x0001;
        cached = id;
    }
    return cached;
}

static uint8_t s_tx_seq = 0;

/* Build one radio packet (always M1_LINK_PKT_LEN, zero-padded). */
static void link_build_frame(uint8_t *pkt, uint8_t type_flags, uint16_t dst,
                             uint8_t seq, uint8_t frag,
                             const uint8_t *payload, uint8_t len)
{
    uint16_t src = m1_link_my_id();

    if ( len > M1_LINK_FRAG_PAYLOAD )
        len = M1_LINK_FRAG_PAYLOAD;

    memset(pkt, 0x00, M1_LINK_PKT_LEN);
    pkt[0] = M1_LINK_PROTO_VER;
    pkt[1] = type_flags;
    pkt[2] = (uint8_t)(src & 0xFF);
    pkt[3] = (uint8_t)(src >> 8);
    pkt[4] = (uint8_t)(dst & 0xFF);
    pkt[5] = (uint8_t)(dst >> 8);
    pkt[6] = seq;
    pkt[7] = frag;
    pkt[8] = len;
    if ( payload && len )
        memcpy(&pkt[M1_LINK_HDR_LEN], payload, len);
}

/* Validate + unpack a received packet. Returns false if not a link frame. */
static bool link_parse_frame(const uint8_t *pkt, uint8_t *type_flags,
                             uint16_t *src, uint16_t *dst, uint8_t *seq,
                             uint8_t *frag, uint8_t *len)
{
    if ( pkt[0] != M1_LINK_PROTO_VER )
        return false;
    *type_flags = pkt[1];
    *src = (uint16_t)pkt[2] | ((uint16_t)pkt[3] << 8);
    *dst = (uint16_t)pkt[4] | ((uint16_t)pkt[5] << 8);
    *seq = pkt[6];
    *frag = pkt[7];
    *len = (pkt[8] <= M1_LINK_FRAG_PAYLOAD) ? pkt[8] : M1_LINK_FRAG_PAYLOAD;
    return true;
}

/* Send one fragment. Unicast fragments are ARQ'd (wait for a matching ACK);
 * broadcast fragments are sent once. Returns true on delivery. */
/* Listen-before-talk: arm RX, then wait (bounded) while the channel is busy. */
static void link_lbt_wait(void)
{
    uint32_t w = 0;
    m1_link_rx_arm();
    while ( link_channel_busy() && w < LINK_LBT_MAX_WAIT_MS )
    {
        HAL_Delay(6 + (HAL_GetTick() & 0x07));
        w += 10;
    }
}

static bool link_send_fragment(uint16_t dst, uint8_t seq, uint8_t frag,
                               uint8_t extra_flags, const uint8_t *payload, uint8_t len)
{
    uint8_t pkt[M1_LINK_PKT_LEN];
    uint8_t rxbuf[M1_LINK_PKT_LEN];
    int8_t  rssi;
    bool    unicast = (dst != M1_LINK_ID_BROADCAST);
    uint8_t tf = M1_LINK_TYPE_DATA | extra_flags | (unicast ? M1_LINK_FLAG_ACKREQ : 0);

    link_build_frame(pkt, tf, dst, seq, frag, payload, len);

    if ( !unicast )
    {
        link_lbt_wait();
        return m1_link_tx_raw(pkt, M1_LINK_PKT_LEN);
    }

    for ( uint8_t attempt = 0; attempt <= M1_LINK_MAX_RETRY; attempt++ )
    {
        uint32_t waited = 0;

        link_lbt_wait();
        if ( !m1_link_tx_raw(pkt, M1_LINK_PKT_LEN) )
            continue;   /* TX did not confirm — retry */

        m1_link_rx_arm();

        while ( waited < M1_LINK_ACK_TIMEOUT_MS )
        {
            uint8_t n = m1_link_rx_poll(rxbuf, &rssi);
            if ( n > 0 )
            {
                uint8_t tf2, rlen, rseq, rfrag; uint16_t rsrc, rdst;
                if ( link_parse_frame(rxbuf, &tf2, &rsrc, &rdst, &rseq, &rfrag, &rlen) &&
                     (tf2 & M1_LINK_TYPE_MASK) == M1_LINK_TYPE_ACK &&
                     rdst == m1_link_my_id() && rsrc == dst &&
                     rseq == seq && rfrag == frag )
                {
                    return true;   /* fragment ACKed */
                }
            }
            HAL_Delay(2);
            waited += 2;
        }
        /* Randomized backoff before retransmit. */
        HAL_Delay(5 + (HAL_GetTick() & 0x0F));
    }
    return false;
}

/* Max plaintext that still fits in M1_LINK_MAX_MSG after AES (IV + PKCS7 pad). */
#define M1_LINK_MAX_PLAIN  (M1_LINK_MAX_MSG - 2 * M1_CRYPTO_AES_BLOCK_SIZE)  /* 208 */

bool m1_link_send(uint16_t dst, const uint8_t *data, uint16_t len)
{
    return m1_link_send_flags(dst, data, len, 0);
}

bool m1_link_send_flags(uint16_t dst, const uint8_t *data, uint16_t len, uint8_t extra)
{
    uint8_t  cbuf[M1_LINK_MAX_MSG + M1_CRYPTO_AES_BLOCK_SIZE];
    const uint8_t *payload = data;
    uint16_t plen = len;
    uint8_t  enc_flag = 0;
    uint8_t  seq, total, idx;

    if ( !link_radio_ready || data == NULL )
        return false;

    /* Encrypt the whole message first, then fragment the ciphertext. */
    if ( s_key_valid )
    {
        uint32_t clen;
        if ( plen > M1_LINK_MAX_PLAIN )
            plen = M1_LINK_MAX_PLAIN;
        memcpy(cbuf, data, plen);
        clen = m1_crypto_encrypt_with_key(cbuf, plen, sizeof(cbuf), s_key);
        if ( clen == 0 )
            return false;
        payload  = cbuf;
        plen     = (uint16_t)clen;
        enc_flag = M1_LINK_FLAG_ENC;
    }
    else if ( plen > M1_LINK_MAX_MSG )
    {
        plen = M1_LINK_MAX_MSG;
    }

    seq = ++s_tx_seq;

    total = (uint8_t)((plen + M1_LINK_FRAG_PAYLOAD - 1) / M1_LINK_FRAG_PAYLOAD);
    if ( total == 0 )
        total = 1;   /* zero-length message is still one fragment */

    for ( idx = 0; idx < total; idx++ )
    {
        uint16_t off  = (uint16_t)idx * M1_LINK_FRAG_PAYLOAD;
        uint16_t rem  = plen - off;
        uint8_t  flen = (rem > M1_LINK_FRAG_PAYLOAD) ? M1_LINK_FRAG_PAYLOAD : (uint8_t)rem;
        uint8_t  frag = (uint8_t)((idx << 4) | (total & 0x0F));

        if ( !link_send_fragment(dst, seq, frag, (uint8_t)(enc_flag | extra),
                                 &payload[off], flen) )
            return false;   /* a fragment failed — whole message fails */
    }
    return true;
}

void m1_link_spike_ping(uint16_t dst)
{
    int8_t peer = 0;
    int    rtt;

    if ( !m1_link_radio_bringup() )
    {
        printf("M1 Link: radio bring-up FAILED\r\n");
        return;
    }
    printf("M1 Link: pinging %04X...\r\n", (unsigned)dst);
    rtt = m1_link_ping(dst, &peer);
    if ( rtt >= 0 )
        printf("M1 Link: PONG from %04X  rtt=%d ms  peer-heard-us=%d dBm\r\n",
               (unsigned)dst, rtt, (int)peer);
    else
        printf("M1 Link: no PONG from %04X\r\n", (unsigned)dst);
}

void m1_link_spike_locate(uint16_t dst)
{
    if ( !m1_link_radio_bringup() )
    {
        printf("M1 Link: radio bring-up FAILED\r\n");
        return;
    }
    printf("M1 Link: locating %04X...\r\n", (unsigned)dst);
    printf("M1 Link: %s\r\n",
           m1_link_locate(dst) ? "peer acknowledged (beeping)" : "no response");
}

/*============================================================================*/
/*  Phase 5: ping / locate                                                    */
/*============================================================================*/
int m1_link_ping(uint16_t dst, int8_t *peer_rssi)
{
    uint8_t pkt[M1_LINK_PKT_LEN];
    uint8_t rx[M1_LINK_PKT_LEN];
    int8_t  rr;

    if ( !link_radio_ready || dst == M1_LINK_ID_BROADCAST )
        return -1;

    uint8_t seq = ++s_tx_seq;
    link_build_frame(pkt, M1_LINK_TYPE_PING, dst, seq,
                     (uint8_t)((0 << 4) | 1), NULL, 0);

    for ( uint8_t attempt = 0; attempt <= M1_LINK_MAX_RETRY; attempt++ )
    {
        uint32_t t0, waited = 0;

        link_lbt_wait();
        if ( !m1_link_tx_raw(pkt, M1_LINK_PKT_LEN) )
            continue;
        t0 = HAL_GetTick();
        m1_link_rx_arm();

        while ( waited < 2 * M1_LINK_ACK_TIMEOUT_MS )
        {
            if ( m1_link_rx_poll(rx, &rr) > 0 )
            {
                uint8_t tf2, l2, s2, f2; uint16_t sc, ds;
                if ( link_parse_frame(rx, &tf2, &sc, &ds, &s2, &f2, &l2) &&
                     (tf2 & M1_LINK_TYPE_MASK) == M1_LINK_TYPE_PONG &&
                     ds == m1_link_my_id() && sc == dst && s2 == seq )
                {
                    if ( peer_rssi ) *peer_rssi = (int8_t)rx[M1_LINK_HDR_LEN];
                    return (int)(HAL_GetTick() - t0);
                }
            }
            HAL_Delay(2);
            waited += 2;
        }
    }
    return -1;
}

bool m1_link_locate(uint16_t dst)
{
    uint8_t pkt[M1_LINK_PKT_LEN];
    uint8_t rx[M1_LINK_PKT_LEN];
    int8_t  rr;

    if ( !link_radio_ready || dst == M1_LINK_ID_BROADCAST )
        return false;

    uint8_t seq = ++s_tx_seq;
    uint8_t frag = (uint8_t)((0 << 4) | 1);
    link_build_frame(pkt, M1_LINK_TYPE_LOCATE | M1_LINK_FLAG_ACKREQ, dst, seq,
                     frag, NULL, 0);

    for ( uint8_t attempt = 0; attempt <= M1_LINK_MAX_RETRY; attempt++ )
    {
        uint32_t waited = 0;

        link_lbt_wait();
        if ( !m1_link_tx_raw(pkt, M1_LINK_PKT_LEN) )
            continue;
        m1_link_rx_arm();

        while ( waited < M1_LINK_ACK_TIMEOUT_MS )
        {
            if ( m1_link_rx_poll(rx, &rr) > 0 )
            {
                uint8_t tf2, l2, s2, f2; uint16_t sc, ds;
                if ( link_parse_frame(rx, &tf2, &sc, &ds, &s2, &f2, &l2) &&
                     (tf2 & M1_LINK_TYPE_MASK) == M1_LINK_TYPE_ACK &&
                     ds == m1_link_my_id() && sc == dst && s2 == seq && f2 == frag )
                {
                    return true;
                }
            }
            HAL_Delay(2);
            waited += 2;
        }
    }
    return false;
}

/*============================================================================*/
/*  Phase 5: capture file sharing                                             */
/*============================================================================*/
bool m1_link_send_file(uint16_t dst, const char *path, uint8_t action, uint32_t *sent)
{
    FIL      f;
    UINT     br;
    uint32_t off = 0, total = 0, fsize;
    uint8_t  pdu[6 + M1_LINK_FILE_CHUNK];
    const char *base = path, *s;
    uint16_t nl;

    if ( !link_radio_ready || path == NULL || dst == M1_LINK_ID_BROADCAST )
        return false;
    if ( f_open(&f, path, FA_READ) != FR_OK )
        return false;
    fsize = f_size(&f);

    for ( s = path; *s; s++ )
        if ( *s == '/' || *s == '\\' ) base = s + 1;
    nl = (uint16_t)strlen(base);
    if ( nl > M1_LINK_FILE_NAME_MAX ) nl = M1_LINK_FILE_NAME_MAX;

    /* OFFER: [op][size:4][action:1][name...] */
    pdu[0] = M1_LINK_FILE_OP_OFFER;
    pdu[1] = (uint8_t)(fsize);       pdu[2] = (uint8_t)(fsize >> 8);
    pdu[3] = (uint8_t)(fsize >> 16); pdu[4] = (uint8_t)(fsize >> 24);
    pdu[5] = action;
    memcpy(&pdu[6], base, nl);
    if ( !m1_link_send_flags(dst, pdu, (uint16_t)(6 + nl), M1_LINK_FLAG_FILE) )
    {
        f_close(&f);
        return false;
    }

    /* CHUNKs */
    for ( ;; )
    {
        if ( f_read(&f, &pdu[5], M1_LINK_FILE_CHUNK, &br) != FR_OK ) { f_close(&f); return false; }
        if ( br == 0 )
            break;
        pdu[0] = M1_LINK_FILE_OP_CHUNK;
        pdu[1] = (uint8_t)(off);       pdu[2] = (uint8_t)(off >> 8);
        pdu[3] = (uint8_t)(off >> 16); pdu[4] = (uint8_t)(off >> 24);
        if ( !m1_link_send_flags(dst, pdu, (uint16_t)(5 + br), M1_LINK_FLAG_FILE) )
        {
            f_close(&f);
            return false;
        }
        off   += br;
        total += br;
        if ( br < M1_LINK_FILE_CHUNK )
            break;
    }
    f_close(&f);

    /* DONE */
    pdu[0] = M1_LINK_FILE_OP_DONE;
    if ( !m1_link_send_flags(dst, pdu, 1, M1_LINK_FLAG_FILE) )
        return false;

    if ( sent ) *sent = total;
    return true;
}

void m1_link_spike_sendfile(uint16_t dst, const char *path)
{
    uint32_t sent = 0;

    if ( !m1_link_radio_bringup() )
    {
        printf("M1 Link: radio bring-up FAILED\r\n");
        return;
    }
    printf("M1 Link: sending file '%s' to %04X...\r\n", path, (unsigned)dst);
    if ( m1_link_send_file(dst, path, M1_LINK_FILE_ACT_SAVE, &sent) )
        printf("M1 Link: file sent OK (%lu bytes)\r\n", (unsigned long)sent);
    else
        printf("M1 Link: file send FAILED\r\n");
}

bool m1_link_trigger(uint16_t dst, uint8_t ptype, const char *path)
{
    /* Transfer the payload file to the peer's folder and run it there. */
    return m1_link_send_file(dst, path, ptype, NULL);
}

void m1_link_spike_trigger(uint16_t dst, uint8_t ptype, const char *path)
{
    if ( !m1_link_radio_bringup() )
    {
        printf("M1 Link: radio bring-up FAILED\r\n");
        return;
    }
    if ( !m1_link_encrypted() )
        printf("M1 Link: WARNING no passphrase set — peer saves but won't run it\r\n");
    printf("M1 Link: sending+running type=%u '%s' on %04X...\r\n",
           (unsigned)ptype, path, (unsigned)dst);
    printf("M1 Link: %s\r\n",
           m1_link_trigger(dst, ptype, path) ? "delivered (ACK)" : "failed");
}

/*============================================================================*/
/*  Phase-0 spike entry points                                                */
/*============================================================================*/
void m1_link_spike_info(void)
{
    struct si446x_reply_PART_INFO_map *pi;

    if ( !m1_link_radio_bringup() )
    {
        printf("M1 Link: radio bring-up FAILED\r\n");
        return;
    }

    pi = SI446x_PartInfo();
    printf("M1 Link: radio OK  part=0x%04X rev=0x%02X\r\n",
           (unsigned)pi->PART, (unsigned)pi->CHIPREV);
    printf("  id=%04X callsign=\"%s\"\r\n",
           (unsigned)m1_link_my_id(), s_cfg.callsign);
    printf("  band=915MHz FSK  channel=%u  tx_pwr=%u  enc=%s\r\n",
           (unsigned)s_cfg.channel,
           (unsigned)s_tx_power_tbl[s_cfg.tx_power_idx],
           s_key_valid ? "ON" : "off");
}

/* CLI test helper: set config fields at runtime (mirrors the settings UI).
 * pass/callsign may be NULL to leave unchanged; channel/power < 0 to skip. */
void m1_link_cfg_set(const char *callsign, const char *passphrase,
                     int channel, int tx_power_idx, bool persist)
{
    if ( !s_cfg_loaded ) { m1_link_cfg_load(); s_cfg_loaded = true; }
    if ( callsign )   snprintf(s_cfg.callsign, sizeof(s_cfg.callsign), "%.*s",
                               (int)(sizeof(s_cfg.callsign) - 1), callsign);
    if ( passphrase ) snprintf(s_cfg.passphrase, sizeof(s_cfg.passphrase), "%.*s",
                               (int)(sizeof(s_cfg.passphrase) - 1), passphrase);
    if ( channel >= 0 )      s_cfg.channel = (uint8_t)channel;
    if ( tx_power_idx >= 0 ) s_cfg.tx_power_idx = (uint8_t)tx_power_idx;
    if ( persist ) m1_link_cfg_save();
    else           m1_link_cfg_apply();
}

/*============================================================================*/
void m1_link_spike_beacon(uint32_t count, uint32_t period_ms)
{
    uint8_t pkt[M1_LINK_PKT_LEN];
    uint32_t i;

    if ( period_ms == 0 )
        period_ms = 500;

    if ( !m1_link_radio_bringup() )
    {
        printf("M1 Link: radio bring-up FAILED\r\n");
        return;
    }

    printf("M1 Link: beaconing %lu packet(s), %lu ms apart...\r\n",
           (unsigned long)count, (unsigned long)period_ms);

    /* Payload: "M1" magic + sequence byte + 0xA5 filler. */
    memset(pkt, 0xA5, sizeof(pkt));
    pkt[0] = 'M';
    pkt[1] = '1';

    for ( i = 0; i < count; i++ )
    {
        pkt[2] = (uint8_t)(i & 0xFF);
        if ( m1_link_tx_raw(pkt, M1_LINK_PKT_LEN) )
            printf("  tx #%lu seq=%u  OK\r\n", (unsigned long)i, (unsigned)pkt[2]);
        else
            printf("  tx #%lu seq=%u  TIMEOUT\r\n", (unsigned long)i, (unsigned)pkt[2]);
        HAL_Delay(period_ms);
    }
    printf("M1 Link: beacon done\r\n");
}

/*============================================================================*/
void m1_link_spike_listen(uint32_t seconds)
{
    uint8_t buf[M1_LINK_PKT_LEN];
    int8_t rssi;
    uint32_t elapsed_ms = 0;
    uint32_t budget_ms;
    uint32_t rx_count = 0;

    if ( seconds == 0 )
        seconds = 30;
    budget_ms = seconds * 1000u;

    if ( !m1_link_radio_bringup() )
    {
        printf("M1 Link: radio bring-up FAILED\r\n");
        return;
    }

    printf("M1 Link: listening for %lu s...\r\n", (unsigned long)seconds);
    m1_link_rx_arm();

    while ( elapsed_ms < budget_ms )
    {
        uint8_t n = m1_link_rx_poll(buf, &rssi);
        if ( n > 0 )
        {
            rx_count++;
            printf("  rx seq=%u  rssi=%d dBm  len=%u  [%02X %02X %02X %02X %02X ...]\r\n",
                   (unsigned)buf[2], (int)rssi, (unsigned)n,
                   buf[0], buf[1], buf[2], buf[3], buf[4]);
        }
        HAL_Delay(5);
        elapsed_ms += 5;
    }
    printf("M1 Link: listen done, %lu packet(s) received\r\n",
           (unsigned long)rx_count);
}

/*============================================================================*/
/* RX diagnostic: arm the receiver and periodically report chip state, RSSI and
 * modem preamble/sync-detect flags. Run this on one unit while another beacons
 * to localize where reception breaks:
 *   - RSSI never rises      -> not hearing energy (antenna / frequency)
 *   - RSSI rises, no PREAMBLE-> modem/datarate/deviation mismatch
 *   - PREAMBLE but no SYNC  -> sync-word / preamble-length config
 *   - SYNC but no PACKET_RX -> packet field-length / CRC config
 */
void m1_link_spike_rxdiag(uint32_t seconds)
{
    uint8_t buf[M1_LINK_PKT_LEN];
    int8_t  rssi;
    uint32_t elapsed_ms = 0, budget_ms;
    uint8_t  saw_preamble = 0, saw_sync = 0;
    uint32_t rx_count = 0;

    if ( seconds == 0 )
        seconds = 15;
    budget_ms = seconds * 1000u;

    if ( !m1_link_radio_bringup() )
    {
        printf("M1 Link: radio bring-up FAILED\r\n");
        return;
    }

    printf("M1 Link: rxdiag for %lu s (state/rssi/modem)...\r\n",
           (unsigned long)seconds);
    m1_link_rx_arm();

    while ( elapsed_ms < budget_ms )
    {
        /* Fast packet check */
        uint8_t n = m1_link_rx_poll(buf, &rssi);
        if ( n > 0 )
        {
            rx_count++;
            printf("  PACKET seq=%u rssi=%d len=%u\r\n",
                   (unsigned)buf[2], (int)rssi, (unsigned)n);
        }

        /* Every ~250ms sample the modem + chip state */
        if ( (elapsed_ms % 250u) == 0u )
        {
            struct si446x_reply_GET_MODEM_STATUS_map *ms = SI446x_Get_ModemStatus(0);
            struct si446x_reply_REQUEST_DEVICE_STATE_map *st = SI446x_Request_DeviceState();
            uint8_t modem = ms->MODEM_STATUS;   /* b0 SYNC, b1 PREAMBLE, b2 INVALID_PREAMBLE, b3 RSSI */
            if ( modem & 0x02 ) saw_preamble = 1;
            if ( modem & 0x01 ) saw_sync = 1;
            printf("  t=%lums state=%u rssi=%u/%u modem=0x%02X%s%s\r\n",
                   (unsigned long)elapsed_ms,
                   (unsigned)st->CURR_STATE,
                   (unsigned)ms->CURR_RSSI, (unsigned)ms->LATCH_RSSI,
                   (unsigned)modem,
                   (modem & 0x02) ? " PREAMBLE" : "",
                   (modem & 0x01) ? " SYNC" : "");
        }

        HAL_Delay(5);
        elapsed_ms += 5;
    }
    printf("M1 Link: rxdiag done. packets=%lu preamble_seen=%u sync_seen=%u\r\n",
           (unsigned long)rx_count, (unsigned)saw_preamble, (unsigned)saw_sync);
}

/*============================================================================*/
/*  Phase 1 spike entry points                                                */
/*============================================================================*/
void m1_link_spike_send(uint16_t dst, const char *text)
{
    uint16_t len;

    if ( text == NULL )
        text = "";
    len = (uint16_t)strlen(text);
    if ( len > M1_LINK_MAX_MSG )
        len = M1_LINK_MAX_MSG;

    if ( !m1_link_radio_bringup() )
    {
        printf("M1 Link: radio bring-up FAILED\r\n");
        return;
    }

    printf("M1 Link: id=%04X sending %u byte(s) to %04X: \"%.*s\"\r\n",
           (unsigned)m1_link_my_id(), (unsigned)len, (unsigned)dst,
           (int)len, text);

    if ( dst == M1_LINK_ID_BROADCAST )
    {
        bool ok = m1_link_send(dst, (const uint8_t *)text, len);
        printf("M1 Link: broadcast %s\r\n", ok ? "sent" : "TX FAILED");
        return;
    }

    if ( m1_link_send(dst, (const uint8_t *)text, len) )
        printf("M1 Link: delivered (ACK received)\r\n");
    else
        printf("M1 Link: NO ACK after %u retries\r\n", (unsigned)M1_LINK_MAX_RETRY);
}

/* --- Fragment reassembly --- */
typedef struct {
    uint8_t  used;
    uint16_t src;
    uint8_t  seq;
    uint8_t  total;
    uint16_t got_mask;                 /* one bit per received fragment index */
    uint8_t  delivered;
    uint8_t  enc;                      /* message payload is AES-encrypted */
    uint8_t  is_file;                  /* message is a file-transfer PDU */
    uint8_t  last_len;                 /* payload length of the final fragment */
    uint8_t  buf[M1_LINK_MAX_MSG];
} link_reasm_t;

static link_reasm_t s_reasm[2];

static link_reasm_t *link_reasm_get(uint16_t src, uint8_t seq, uint8_t total)
{
    uint8_t i, slot = 0;

    for ( i = 0; i < 2; i++ )
        if ( s_reasm[i].used && s_reasm[i].src == src && s_reasm[i].seq == seq )
            return &s_reasm[i];

    /* Allocate: prefer a free slot, else evict slot 0. */
    slot = 0;
    for ( i = 0; i < 2; i++ )
        if ( !s_reasm[i].used ) { slot = i; break; }

    memset(&s_reasm[slot], 0, sizeof(s_reasm[slot]));
    s_reasm[slot].used  = 1;
    s_reasm[slot].src   = src;
    s_reasm[slot].seq   = seq;
    s_reasm[slot].total = total;
    return &s_reasm[slot];
}

/*============================================================================*/
/*  Phase 3: reusable receive engine (drives the on-device Chat UI)           */
/*============================================================================*/
void m1_link_rx_reset(void)
{
    memset(s_reasm, 0, sizeof(s_reasm));
}

void m1_link_send_hello(void)
{
    uint8_t hello[M1_LINK_PKT_LEN];
    const char *name = s_cfg.callsign;
    uint8_t nlen;

    if ( !link_radio_ready )
        return;
    if ( name[0] == '\0' )
        name = "M1";
    nlen = (uint8_t)strlen(name);
    if ( nlen > M1_LINK_FRAG_PAYLOAD )
        nlen = M1_LINK_FRAG_PAYLOAD;
    link_build_frame(hello, M1_LINK_TYPE_HELLO, M1_LINK_ID_BROADCAST,
                     ++s_tx_seq, (uint8_t)((0 << 4) | 1),
                     (const uint8_t *)name, nlen);
    m1_link_tx_raw(hello, M1_LINK_PKT_LEN);
    m1_link_rx_arm();
}

/* --- Incoming file transfer state --- */
static FIL      s_frx_file;
static bool     s_frx_open = false;
static uint32_t s_frx_total = 0;
static uint32_t s_frx_done = 0;
static char     s_frx_path[64];
static uint8_t  s_frx_action = 0;   /* action requested by the OFFER */
static bool     s_frx_enc = false;  /* was the transfer encrypted? */

/* Deferred payload execution (serviced after the DONE fragment is ACKed). */
static bool     s_exec_pending = false;
static uint8_t  s_exec_type = 0;
static char     s_exec_path[64];

/* Guard against running the SAME transfer twice: the sender retransmits DONE if
 * an ACK is lost, and if our 2-slot reassembly context was evicted we'd treat
 * the retransmit as new and fire the payload again. Remember the last executed
 * (src,seq) and refuse to re-run it. */
static bool     s_exec_seen = false;
static uint16_t s_exec_last_src = 0;
static uint8_t  s_exec_last_seq = 0;

uint32_t m1_link_file_rx_total(void) { return s_frx_total; }
uint32_t m1_link_file_rx_done(void)  { return s_frx_done; }

/* Case-insensitive suffix test. */
static bool link_ext_is(const char *name, const char *ext)
{
    size_t nl = strlen(name), el = strlen(ext);
    if ( nl < el ) return false;
    const char *s = name + (nl - el);
    for ( size_t i = 0; i < el; i++ )
    {
        char a = s[i], b = ext[i];
        if ( a >= 'A' && a <= 'Z' ) a = (char)(a + 32);
        if ( b >= 'A' && b <= 'Z' ) b = (char)(b + 32);
        if ( a != b ) return false;
    }
    return true;
}

/* Choose a destination path for a received file. Exec actions go to the
 * payload's own folder; a plain SAVE routes by extension. Creates the dir. */
static void link_file_dest(const char *name, uint8_t action, char *out, size_t outsz)
{
    const char *dir;
    const char *base = name, *s;

    switch ( action )
    {
        case M1_LINK_TRIG_SUB:    dir = "0:/SUBGHZ"; break;
        case M1_LINK_TRIG_BADUSB:
        case M1_LINK_TRIG_BADBT:  dir = "0:/BadUSB"; break;
        case M1_LINK_TRIG_IR:     dir = "0:/IR"; break;
        default:  /* SAVE: route by extension */
            if      ( link_ext_is(name, ".sub") ) dir = "0:/SUBGHZ";
            else if ( link_ext_is(name, ".nfc") ) dir = "0:/NFC";
            else if ( link_ext_is(name, ".ir")  ) dir = "0:/IR";
            else                                  dir = "0:/LINK_RX";
            break;
    }
    f_mkdir(dir);   /* ignore FR_EXIST */

    for ( s = name; *s; s++ )
        if ( *s == '/' || *s == '\\' ) base = s + 1;
    snprintf(out, outsz, "%s/%s", dir, base);
}

/* Handle a completed FLAG_FILE message (OFFER / CHUNK / DONE). `enc` = whether
 * the transfer arrived encrypted (required before an exec action will run). */
static m1_link_rx_kind_t link_file_rx(uint16_t src, uint8_t seq, const uint8_t *p,
                                      uint16_t len, bool enc, uint16_t *from,
                                      char *buf, uint16_t buflen, uint16_t *outlen)
{
    if ( len < 1 )
        return M1_LINK_RX_NONE;
    if ( from ) *from = src;

    switch ( p[0] )
    {
        case M1_LINK_FILE_OP_OFFER:
        {
            /* [op][size:4][action:1][name...] */
            char name[M1_LINK_FILE_NAME_MAX + 1];
            uint16_t nl;
            if ( len < 6 ) return M1_LINK_RX_NONE;
            nl = (uint16_t)(len - 6);
            if ( nl > M1_LINK_FILE_NAME_MAX ) nl = M1_LINK_FILE_NAME_MAX;
            memcpy(name, &p[6], nl); name[nl] = '\0';

            if ( s_frx_open ) { f_close(&s_frx_file); s_frx_open = false; }
            s_frx_total = (uint32_t)p[1] | ((uint32_t)p[2] << 8) |
                          ((uint32_t)p[3] << 16) | ((uint32_t)p[4] << 24);
            s_frx_action = p[5];
            s_frx_enc = enc;
            s_frx_done = 0;
            link_file_dest(name, s_frx_action, s_frx_path, sizeof(s_frx_path));
            if ( f_open(&s_frx_file, s_frx_path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK )
                return M1_LINK_RX_NONE;
            s_frx_open = true;
            if ( buf ) { snprintf(buf, buflen, "%s", name); }
            if ( outlen ) *outlen = nl;
            return M1_LINK_RX_FILE_START;
        }

        case M1_LINK_FILE_OP_CHUNK:
        {
            UINT bw;
            uint32_t off;
            if ( !s_frx_open || len < 5 ) return M1_LINK_RX_NONE;
            off = (uint32_t)p[1] | ((uint32_t)p[2] << 8) |
                  ((uint32_t)p[3] << 16) | ((uint32_t)p[4] << 24);
            f_lseek(&s_frx_file, off);
            f_write(&s_frx_file, &p[5], len - 5, &bw);
            s_frx_done = off + (len - 5);
            return M1_LINK_RX_FILE_DATA;
        }

        case M1_LINK_FILE_OP_DONE:
        {
            if ( s_frx_open ) { f_close(&s_frx_file); s_frx_open = false; }
            if ( buf ) { snprintf(buf, buflen, "%s", s_frx_path); }
            /* Exec action + encrypted transfer -> run the payload after ACK,
             * but only once per (src,seq) so a retransmitted DONE can't re-run it. */
            if ( s_frx_action != M1_LINK_FILE_ACT_SAVE && s_frx_enc )
            {
                if ( s_exec_seen && src == s_exec_last_src && seq == s_exec_last_seq )
                    return M1_LINK_RX_FILE_END;   /* already ran this transfer */
                s_exec_seen = true;
                s_exec_last_src = src;
                s_exec_last_seq = seq;
                s_exec_type = s_frx_action;
                snprintf(s_exec_path, sizeof(s_exec_path), "%s", s_frx_path);
                s_exec_pending = true;
                return M1_LINK_RX_TRIGGER;
            }
            return M1_LINK_RX_FILE_END;
        }
    }
    return M1_LINK_RX_NONE;
}

m1_link_rx_kind_t m1_link_rx_process(uint16_t *from, char *buf, uint16_t buflen,
                                     uint16_t *outlen, int8_t *rssi)
{
    uint8_t  pkt[M1_LINK_PKT_LEN];
    int8_t   r;
    uint16_t my_id = m1_link_my_id();
    uint8_t  tf, len, seq, frag, type;
    uint16_t src, dst;

    if ( !link_radio_ready )
        return M1_LINK_RX_NONE;
    if ( m1_link_rx_poll(pkt, &r) == 0 )
        return M1_LINK_RX_NONE;
    if ( !link_parse_frame(pkt, &tf, &src, &dst, &seq, &frag, &len) )
        return M1_LINK_RX_NONE;

    type = tf & M1_LINK_TYPE_MASK;

    if ( type == M1_LINK_TYPE_HELLO && src != my_id )
    {
        uint16_t cl = (len < buflen) ? len : (uint16_t)(buflen - 1);
        if ( from ) *from = src;
        if ( rssi ) *rssi = r;
        if ( buf ) { memcpy(buf, &pkt[M1_LINK_HDR_LEN], cl); buf[cl] = '\0'; }
        if ( outlen ) *outlen = cl;
        return M1_LINK_RX_HELLO;
    }

    /* PING: reply with a PONG carrying the dBm at which we heard the ping. */
    if ( type == M1_LINK_TYPE_PING && dst == my_id )
    {
        uint8_t pong[M1_LINK_PKT_LEN];
        uint8_t heard = (uint8_t)r;
        link_build_frame(pong, M1_LINK_TYPE_PONG, src, seq,
                         (uint8_t)((0 << 4) | 1), &heard, 1);
        m1_link_tx_raw(pong, M1_LINK_PKT_LEN);
        m1_link_rx_arm();
        return M1_LINK_RX_NONE;   /* handled internally */
    }

    /* PONG addressed to us (usually caught inside m1_link_ping directly). */
    if ( type == M1_LINK_TYPE_PONG && dst == my_id )
    {
        if ( from ) *from = src;
        if ( rssi ) *rssi = r;
        return M1_LINK_RX_PONG;
    }

    /* LOCATE: beep + flash the LED so this unit can be physically found. */
    if ( type == M1_LINK_TYPE_LOCATE && dst == my_id )
    {
        if ( tf & M1_LINK_FLAG_ACKREQ )
        {
            uint8_t ack[M1_LINK_PKT_LEN];
            link_build_frame(ack, M1_LINK_TYPE_ACK, src, seq, frag, NULL, 0);
            m1_link_tx_raw(ack, M1_LINK_PKT_LEN);
            m1_link_rx_arm();
        }
        m1_led_fast_blink(0x07, 0xFF, 100);   /* white fast blink */
        m1_buzzer_set(2500, 150);
        HAL_Delay(120);
        m1_buzzer_set(2500, 150);
        if ( from ) *from = src;
        if ( rssi ) *rssi = r;
        return M1_LINK_RX_LOCATE;
    }

    if ( type == M1_LINK_TYPE_DATA && (dst == my_id || dst == M1_LINK_ID_BROADCAST) )
    {
        uint8_t idx = frag >> 4;
        uint8_t total = frag & 0x0F;
        m1_link_rx_kind_t result = M1_LINK_RX_NONE;
        if ( total == 0 ) total = 1;

        if ( idx < total )
        {
            link_reasm_t *c = link_reasm_get(src, seq, total);
            uint16_t off = (uint16_t)idx * M1_LINK_FRAG_PAYLOAD;
            c->total   = total;
            c->enc     = (tf & M1_LINK_FLAG_ENC)  ? 1 : 0;
            c->is_file = (tf & M1_LINK_FLAG_FILE) ? 1 : 0;
            if ( off + len <= M1_LINK_MAX_MSG )
                memcpy(&c->buf[off], &pkt[M1_LINK_HDR_LEN], len);
            c->got_mask |= (uint16_t)(1u << idx);
            if ( idx == total - 1 )
                c->last_len = len;

            uint16_t full = (total >= 16) ? 0xFFFF : (uint16_t)((1u << total) - 1);
            if ( c->got_mask == full && !c->delivered )
            {
                uint16_t msglen = (uint16_t)(total - 1) * M1_LINK_FRAG_PAYLOAD + c->last_len;
                c->delivered = 1;

                if ( c->enc )
                {
                    /* Decrypt in place. Drop if we have no key or it doesn't fit. */
                    uint32_t plen;
                    if ( !s_key_valid )
                        return M1_LINK_RX_NONE;
                    plen = m1_crypto_decrypt_with_key(c->buf, msglen, s_key);
                    if ( plen == 0 )
                        return M1_LINK_RX_NONE;   /* wrong key / corrupt */
                    msglen = (uint16_t)plen;
                }

                if ( c->is_file )
                {
                    /* File-transfer PDU: write to SD (and maybe run on DONE). */
                    if ( rssi ) *rssi = r;
                    result = link_file_rx(src, seq, c->buf, msglen, c->enc ? true : false,
                                          from, buf, buflen, outlen);
                }
                else
                {
                    if ( msglen >= buflen ) msglen = (uint16_t)(buflen - 1);
                    if ( buf ) { memcpy(buf, c->buf, msglen); buf[msglen] = '\0'; }
                    if ( outlen ) *outlen = msglen;
                    if ( from ) *from = src;
                    if ( rssi ) *rssi = r;
                    result = M1_LINK_RX_MESSAGE;
                }
            }
        }

        if ( (tf & M1_LINK_FLAG_ACKREQ) && dst == my_id )
        {
            uint8_t ack[M1_LINK_PKT_LEN];
            link_build_frame(ack, M1_LINK_TYPE_ACK, src, seq, frag, NULL, 0);
            m1_link_tx_raw(ack, M1_LINK_PKT_LEN);
            m1_link_rx_arm();
        }

        /* Once the DONE fragment is ACKed, run a pending received payload.
         * Sub-GHz replay takes over the radio, so it needs a full re-bring-up
         * afterwards; the HID/IR payloads don't touch the Si4463 (a simple
         * re-arm suffices). s_exec_path was already resolved by link_file_dest. */
        if ( s_exec_pending )
        {
            s_exec_pending = false;
            switch ( s_exec_type )
            {
                case M1_LINK_TRIG_SUB:
                    (void)sub_ghz_replay_flipper_file(s_exec_path);
                    m1_link_radio_bringup();
                    break;
                case M1_LINK_TRIG_BADUSB:
                    (void)badusb_execute_file(s_exec_path);
                    break;
                case M1_LINK_TRIG_BADBT:
                    (void)badbt_execute_file(s_exec_path);
                    break;
                case M1_LINK_TRIG_IR:
                    ir_universal_play_file(s_exec_path);
                    break;
                default:
                    break;
            }
            m1_link_rx_arm();
        }
        return result;
    }

    return M1_LINK_RX_NONE;
}

void m1_link_spike_recv(uint32_t seconds)
{
    char     msg[M1_LINK_MAX_MSG + 1];
    uint16_t from, mlen;
    int8_t   rssi;
    uint32_t elapsed_ms = 0, budget_ms;
    uint32_t rx_count = 0;

    if ( seconds == 0 )
        seconds = 30;
    budget_ms = seconds * 1000u;

    if ( !m1_link_radio_bringup() )
    {
        printf("M1 Link: radio bring-up FAILED\r\n");
        return;
    }

    m1_link_rx_reset();
    printf("M1 Link: id=%04X listening for %lu s (enc=%s)...\r\n",
           (unsigned)m1_link_my_id(), (unsigned long)seconds,
           s_key_valid ? "ON" : "off");
    m1_link_rx_arm();

    while ( elapsed_ms < budget_ms )
    {
        m1_link_rx_kind_t k = m1_link_rx_process(&from, msg, sizeof(msg), &mlen, &rssi);
        if ( k == M1_LINK_RX_MESSAGE )
        {
            rx_count++;
            printf("M1 Link: RX from %04X rssi=%d len=%u: \"%.*s\"\r\n",
                   (unsigned)from, (int)rssi, (unsigned)mlen,
                   (int)mlen, msg);
        }
        else if ( k == M1_LINK_RX_FILE_START )
        {
            printf("M1 Link: file from %04X: '%s' (%lu bytes)...\r\n",
                   (unsigned)from, msg, (unsigned long)m1_link_file_rx_total());
        }
        else if ( k == M1_LINK_RX_FILE_END )
        {
            printf("M1 Link: file saved -> %s (%lu bytes)\r\n",
                   msg, (unsigned long)m1_link_file_rx_done());
        }
        HAL_Delay(2);
        elapsed_ms += 2;
    }
    printf("M1 Link: recv done, %lu message(s) received\r\n",
           (unsigned long)rx_count);
}

/*============================================================================*/
/* Discovery: periodically broadcast a HELLO beacon (our id + name) and listen
 * for others', building a nearby-peer list. Run on both units to find each
 * other without typing hex ids. */
void m1_link_spike_scan(uint32_t seconds)
{
    uint8_t  buf[M1_LINK_PKT_LEN];
    uint8_t  hello[M1_LINK_PKT_LEN];
    int8_t   rssi;
    uint32_t elapsed_ms = 0, budget_ms, next_hello_ms = 0;
    uint16_t my_id;
    char     name[16];
    uint16_t peer_id[8];
    uint8_t  peer_n = 0;

    if ( seconds == 0 )
        seconds = 20;
    budget_ms = seconds * 1000u;

    if ( !m1_link_radio_bringup() )
    {
        printf("M1 Link: radio bring-up FAILED\r\n");
        return;
    }

    my_id = m1_link_my_id();
    snprintf(name, sizeof(name), "M1-%04X", (unsigned)my_id);
    printf("M1 Link: scanning %lu s as %04X \"%s\"...\r\n",
           (unsigned long)seconds, (unsigned)my_id, name);
    m1_link_rx_arm();

    while ( elapsed_ms < budget_ms )
    {
        uint8_t n = m1_link_rx_poll(buf, &rssi);
        if ( n > 0 )
        {
            uint8_t tf, len, seq, frag; uint16_t src, dst;
            if ( link_parse_frame(buf, &tf, &src, &dst, &seq, &frag, &len) &&
                 (tf & M1_LINK_TYPE_MASK) == M1_LINK_TYPE_HELLO &&
                 src != my_id )
            {
                uint8_t i, known = 0;
                for ( i = 0; i < peer_n; i++ )
                    if ( peer_id[i] == src ) { known = 1; break; }
                if ( !known && peer_n < 8 )
                {
                    char pname[M1_LINK_FRAG_PAYLOAD + 1];
                    if ( len > M1_LINK_FRAG_PAYLOAD ) len = M1_LINK_FRAG_PAYLOAD;
                    memcpy(pname, &buf[M1_LINK_HDR_LEN], len);
                    pname[len] = '\0';
                    peer_id[peer_n++] = src;
                    printf("M1 Link: discovered %04X \"%s\" rssi=%d\r\n",
                           (unsigned)src, pname, (int)rssi);
                }
            }
        }

        if ( elapsed_ms >= next_hello_ms )
        {
            link_build_frame(hello, M1_LINK_TYPE_HELLO, M1_LINK_ID_BROADCAST,
                             ++s_tx_seq, (uint8_t)((0 << 4) | 1),
                             (const uint8_t *)name, (uint8_t)strlen(name));
            m1_link_tx_raw(hello, M1_LINK_PKT_LEN);
            m1_link_rx_arm();
            next_hello_ms = elapsed_ms + 1000u;
        }

        HAL_Delay(2);
        elapsed_ms += 2;
    }
    printf("M1 Link: scan done, %u peer(s) discovered\r\n", (unsigned)peer_n);
}

#endif /* M1_APP_LINK_ENABLE */
