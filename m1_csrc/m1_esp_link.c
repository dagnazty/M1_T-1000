/* See COPYING.txt for license details. */

/*
 *
 * m1_esp_link.c
 *
 * M1 Link over ESP32 (ESP-NOW) — host (T-1000) side of the remote-trigger PoC.
 *
 * Drives AT+M1ESPNOW* on the ESP32-C6 over SPI and parses the +ESPNOWRX: /
 * +ESPNOWPEER: unsolicited events. The receiver polls the ESP (like the
 * +ZIGFRAME reader in m1_802154.c) and, on a BADUSB trigger, runs the named
 * DuckyScript from 0:/BadUSB via badusb_execute_file().
 *
 * Design + wire format: esp32-at-monstatek-m1/docs/ESPNOW_LINK_DESIGN.md
 *
 * M1 Project
 *
 */

/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "stm32h5xx_hal.h"
#include "main.h"
#include "m1_esp_link.h"
#include "m1_esp32_hal.h"      /* m1_esp32_init/status, app_freertos (vTaskDelay) */
#include "esp_app_main.h"      /* spi_AT_send_recv, esp32_main_init, SUCCESS      */
#include "m1_compile_cfg.h"
#include "m1_badusb.h"         /* badusb_execute_file()                           */
#include "m1_ir_universal.h"   /* ir_universal_play_file()                        */
#include "ff.h"                /* FatFs: read the payload / write the received one */

/* Sub-GHz .sub replay (declared extern like m1_link.c does). */
extern uint8_t sub_ghz_replay_flipper_file(const char *sub_path);

/*************************** D E F I N E S ************************************/

#define ESPNOW_AT_RESP_SIZE     1024
#define ESPNOW_POLL_INTERVAL_MS 50
#define ESPNOW_BADUSB_DIR       "0:/BadUSB"
#define ESPNOW_IR_DIR           "0:/IR"
#define ESPNOW_SUB_DIR          "0:/SUBGHZ"
#define ESPNOW_MAX_NAME_LEN     64
#define ESPNOW_FILE_CHUNK       32         /* MUST match ENOW_FILE_CHUNK on the ESP.
                                            * Bounded by the 250B ESP-NOW frame, not
                                            * the AT cap (8KB SPI buffer). ~4x fewer
                                            * fragments than the old 32 -> big files
                                            * (raw IR) transfer far faster/reliably. */
#define ESPNOW_MAX_FILE         4096       /* biggest payload we transfer */
#define ESPNOW_MAX_FRAGS        (ESPNOW_MAX_FILE / ESPNOW_FILE_CHUNK + 1)

/********************* H E L P E R S ****************************************/

/* Ensure the ESP32 link + AT main are up. Returns true if ready to talk. */
static bool espnow_esp_ready(void)
{
    if ( !m1_esp32_get_init_status() )
    {
        m1_esp32_init();
    }
    if ( !m1_esp32_get_init_status() )
    {
        printf("ESP-NOW: ESP32 not initialised\r\n");
        return false;
    }
    if ( !get_esp32_main_init_status() )
    {
        esp32_main_init();
    }
    return true;
}

/* Send one AT command, print the response. Returns true on SUCCESS + no ERROR. */
/* Per-command timeout. spi_AT_send_recv's timeout is in whole seconds and it
 * always waits the full timeout for our custom commands (the SPI-AT layer never
 * matches their OK terminator back to the caller), so this is effectively the
 * fixed per-command latency. 1s is the API minimum; the ESP finishes each
 * command (incl. a fragment's send+ACK) well within it. Was 3s. */
#define ESPNOW_AT_TIMEOUT_SEC   1

static bool espnow_at(const char *cmd)
{
    static char resp[ESPNOW_AT_RESP_SIZE];
    resp[0] = '\0';

    uint8_t rc = spi_AT_send_recv((char *)cmd, resp, sizeof(resp), ESPNOW_AT_TIMEOUT_SEC);
    printf("%s", resp);
    if ( rc != SUCCESS || strstr(resp, "ERROR") != NULL )
    {
        return false;
    }
    return true;
}

/* ---- hex helpers (file bytes travel as hex through the AT layer) ---------- */
static int espnow_hexval(char c)
{
    if ( c >= '0' && c <= '9' ) return c - '0';
    if ( c >= 'a' && c <= 'f' ) return c - 'a' + 10;
    if ( c >= 'A' && c <= 'F' ) return c - 'A' + 10;
    return -1;
}
static uint8_t espnow_hex_decode(const char *hex, uint8_t *out, uint8_t cap)
{
    uint8_t n = 0;
    while ( hex[0] && hex[1] && n < cap )
    {
        int hi = espnow_hexval(hex[0]), lo = espnow_hexval(hex[1]);
        if ( hi < 0 || lo < 0 ) break;
        out[n++] = (uint8_t)((hi << 4) | lo);
        hex += 2;
    }
    return n;
}
static void espnow_hex_encode(const uint8_t *in, uint8_t n, char *out /* >=2n+1 */)
{
    static const char H[] = "0123456789ABCDEF";
    for ( uint8_t i = 0; i < n; i++ ) { out[2*i] = H[in[i] >> 4]; out[2*i+1] = H[in[i] & 0xF]; }
    out[2*n] = '\0';
}

/* ---- inbound file reassembly --------------------------------------------- */
static char    s_asm_name[ESPNOW_MAX_NAME_LEN + 1];
static uint8_t s_asm_buf[ESPNOW_MAX_FILE];
static size_t  s_asm_len;
static uint8_t s_asm_total;
static uint8_t s_asm_got;
static bool    s_asm_seen[ESPNOW_MAX_FRAGS];

static void asm_reset(const char *name, uint8_t total)
{
    strncpy(s_asm_name, name, sizeof(s_asm_name) - 1);
    s_asm_name[sizeof(s_asm_name) - 1] = '\0';
    s_asm_total = total;
    s_asm_got   = 0;
    s_asm_len   = 0;
    memset(s_asm_seen, 0, sizeof(s_asm_seen));
}

/* Destination folder for a payload type (matches the sub-GHz link trigger). */
static const char *dir_for_ptype(unsigned int ptype)
{
    switch ( ptype )
    {
        case M1_ESPNOW_PTYPE_SUB:    return ESPNOW_SUB_DIR;
        case M1_ESPNOW_PTYPE_IR:     return ESPNOW_IR_DIR;
        case M1_ESPNOW_PTYPE_BADUSB: return ESPNOW_BADUSB_DIR;
        default:                     return NULL;   /* Bad-BT excluded on ESP-NOW */
    }
}

/* Run a payload of `ptype` from its folder. BadUSB switches USB->HID internally;
 * IR transmits; SUB replays the .sub over the Si4463. */
static void run_payload(unsigned int ptype, const char *name)
{
    const char *dir = dir_for_ptype(ptype);
    if ( dir == NULL ) { printf("ESP-NOW: ptype %u unsupported\r\n", ptype); return; }

    char path[16 + ESPNOW_MAX_NAME_LEN];
    snprintf(path, sizeof(path), "%s/%s", dir, name);

    switch ( ptype )
    {
        case M1_ESPNOW_PTYPE_BADUSB:
            printf("ESP-NOW: running BadUSB %s\r\n", path);
            printf("ESP-NOW: BadUSB %s\r\n", badusb_execute_file(path) ? "done" : "FAILED");
            break;
        case M1_ESPNOW_PTYPE_IR:
            printf("ESP-NOW: transmitting IR %s\r\n", path);
            ir_universal_play_file(path);
            break;
        case M1_ESPNOW_PTYPE_SUB:
            printf("ESP-NOW: replaying Sub-GHz %s\r\n", path);
            printf("ESP-NOW: Sub-GHz %s\r\n", sub_ghz_replay_flipper_file(path) ? "done" : "FAILED");
            break;
        default: break;
    }
}

/* Write the reassembled buffer to the type's folder, then run it. */
static void write_and_run(unsigned int ptype, const char *name)
{
    const char *dir = dir_for_ptype(ptype);
    if ( dir == NULL ) { printf("ESP-NOW: ptype %u unsupported\r\n", ptype); return; }

    char path[16 + ESPNOW_MAX_NAME_LEN];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    f_mkdir(dir);          /* ensure 0:/IR (etc.) exists — FR_EXIST is fine */
    FIL  fp;
    UINT bw = 0;
    if ( f_open(&fp, path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK )
    {
        printf("ESP-NOW: write open FAILED %s\r\n", path);
        return;
    }
    f_write(&fp, s_asm_buf, (UINT)s_asm_len, &bw);
    f_close(&fp);
    printf("ESP-NOW: saved %s (%u bytes)\r\n", path, (unsigned)bw);
    run_payload(ptype, name);
}

/*
 * Parse an AT+M1ESPNOWRX? response and act on each pending fragment/trigger.
 * Format: +M1ESPNOWRX:<mac>,<ptype>,<frag>,<total>,<enc>,<name>,<hexdata>
 * datalen 0 + total<=1 => name-only trigger (run existing file); otherwise a
 * file fragment that we reassemble, then write+run when complete. Encrypted-only
 * (enc must be 1). Returns the number of payloads executed.
 */
static int espnow_process_rx(const char *buf)
{
    const char *p = buf;
    int ran = 0;
    while ( (p = strstr(p, "+M1ESPNOWRX:")) != NULL )
    {
        p += 12; /* skip "+M1ESPNOWRX:" */

        char mac[18]  = {0};
        char name[ESPNOW_MAX_NAME_LEN + 1] = {0};
        static char hex[2 * ESPNOW_FILE_CHUNK + 1];
        hex[0] = '\0';
        unsigned int ptype = 0, frag = 0, total = 1, enc = 0;

        int n = sscanf(p, "%17[^,],%u,%u,%u,%u,%64[^,],%320[^\r\n]",
                       mac, &ptype, &frag, &total, &enc, name, hex);
        if ( n < 6 ) continue;

        if ( enc == 0 )
        {
            printf("ESP-NOW: DROPPED unencrypted trigger (need shared passphrase)\r\n");
            continue;
        }
        if ( dir_for_ptype(ptype) == NULL )
        {
            printf("ESP-NOW: ptype %u not supported (sub/badusb/ir only)\r\n", ptype);
            continue;
        }

        uint8_t data[ESPNOW_FILE_CHUNK];
        uint8_t datalen = (n >= 7) ? espnow_hex_decode(hex, data, sizeof(data)) : 0;

        /* Name-only trigger: run an existing file. */
        if ( total <= 1 && datalen == 0 )
        {
            run_payload(ptype, name);
            ran++;
            continue;
        }

        /* File fragment: reassemble by name. */
        if ( strncmp(name, s_asm_name, sizeof(s_asm_name)) != 0 || s_asm_total != total )
            asm_reset(name, (uint8_t)total);

        if ( frag < ESPNOW_MAX_FRAGS )
        {
            size_t off = (size_t)frag * ESPNOW_FILE_CHUNK;
            if ( off + datalen <= sizeof(s_asm_buf) )
            {
                memcpy(s_asm_buf + off, data, datalen);
                if ( off + datalen > s_asm_len ) s_asm_len = off + datalen;
            }
            if ( !s_asm_seen[frag] ) { s_asm_seen[frag] = true; s_asm_got++; }
        }
        printf("ESP-NOW: frag %u/%u name=%s (%u B)\r\n", frag + 1, total, name, datalen);

        if ( s_asm_got >= s_asm_total )
        {
            write_and_run(ptype, name);
            asm_reset("", 0);
            ran++;
        }
    }
    return ran;
}

/* One receive poll: query the ESP's trigger queue and run any pending trigger.
 * Returns true if a trigger was dispatched. Runs badusb_execute_file() in the
 * CALLER's task context — call from the UI/main task so the USB HID switch
 * works (the RPC task cannot switch USB out from under its own CDC channel). */
bool m1_esp_link_rx_poll(void)
{
    if ( !espnow_esp_ready() ) return false;
    static char resp[ESPNOW_AT_RESP_SIZE];
    bool ran = false;

    /* Drain the ESP's queue fully each call: each AT+M1ESPNOWRX? pops one
     * fragment, and a small script is many 32-byte frags. Draining keeps up with
     * the sender so the queue doesn't overflow (and completes the reassembly). */
    for ( int i = 0; i < ESPNOW_MAX_FRAGS + 2; i++ )
    {
        resp[0] = '\0';
        spi_AT_send_recv("AT+M1ESPNOWRX?\r\n", resp, sizeof(resp), 1);
        if ( strstr(resp, "+M1ESPNOWRX:") == NULL ) break;   /* queue empty */
        if ( espnow_process_rx(resp) > 0 ) ran = true;        /* a payload ran */
    }
    return ran;
}

/********************* P U B L I C   A P I ****************************************/

void m1_esp_link_enable(uint8_t channel)
{
    if ( !espnow_esp_ready() ) return;
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+M1ESPNOW=1,%u\r\n", channel);
    printf("ESP-NOW: enable ch %u -> %s\r\n", channel, espnow_at(cmd) ? "OK" : "ERROR");
}

void m1_esp_link_disable(void)
{
    if ( !espnow_esp_ready() ) return;
    printf("ESP-NOW: disable -> %s\r\n", espnow_at("AT+M1ESPNOW=0\r\n") ? "OK" : "ERROR");
}

void m1_esp_link_info(void)
{
    if ( !espnow_esp_ready() ) return;
    (void)espnow_at("AT+M1ESPNOW?\r\n");
}

void m1_esp_link_key(const char *passphrase)
{
    if ( !espnow_esp_ready() ) return;
    if ( passphrase == NULL ) passphrase = "";
    char cmd[32 + 64];
    snprintf(cmd, sizeof(cmd), "AT+M1ESPNOWKEY=\"%s\"\r\n", passphrase);
    printf("ESP-NOW: set key -> %s\r\n", espnow_at(cmd) ? "OK" : "ERROR");
}

void m1_esp_link_scan(uint32_t seconds)
{
    if ( !espnow_esp_ready() ) return;
    if ( seconds < 1 )  seconds = 3;
    if ( seconds > 30 ) seconds = 30;

    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+M1ESPNOWSCAN=%lu\r\n", (unsigned long)seconds);
    printf("ESP-NOW: scanning %lus...\r\n", (unsigned long)seconds);
    /* The ESP beacons for `seconds` and returns +ESPNOWPEER lines inline. */
    (void)espnow_at(cmd);
}

void m1_esp_link_trigger(const char *mac, uint8_t ptype, const char *name)
{
    if ( !espnow_esp_ready() ) return;
    if ( mac == NULL || name == NULL )
    {
        printf("ESP-NOW: usage trigger <mac> <ptype> <name>\r\n");
        return;
    }
    /* ESP-AT string params must be double-quoted (like AT+CWJAP="ssid","pass").
     * Unquoted values are rejected by esp_at_get_para_as_str -> ERROR. */
    char cmd[52 + ESPNOW_MAX_NAME_LEN];
    snprintf(cmd, sizeof(cmd), "AT+M1ESPNOWTRIG=\"%s\",%u,\"%s\"\r\n", mac, ptype, name);
    printf("ESP-NOW: trigger %s ptype=%u name=%s -> %s\r\n",
           mac, ptype, name, espnow_at(cmd) ? "ACK OK" : "NO ACK / ERROR");
}

void m1_esp_link_pair(const char *mac)
{
    if ( !espnow_esp_ready() ) return;
    if ( mac == NULL ) { printf("ESP-NOW: usage pair <mac>\r\n"); return; }
    char cmd[40];
    snprintf(cmd, sizeof(cmd), "AT+M1ESPNOWPAIR=\"%s\"\r\n", mac);
    printf("ESP-NOW: pair %s -> %s\r\n", mac, espnow_at(cmd) ? "OK" : "ERROR");
}

void m1_esp_link_pairs(void)
{
    if ( !espnow_esp_ready() ) return;
    (void)espnow_at("AT+M1ESPNOWPAIR?\r\n");
}

void m1_esp_link_listen(uint32_t seconds)
{
    if ( !espnow_esp_ready() ) return;
    if ( seconds < 1 )  seconds = 30;
    if ( seconds > 300 ) seconds = 300;

    printf("ESP-NOW: listening %lus for triggers...\r\n", (unsigned long)seconds);

    uint32_t start = HAL_GetTick();
    while ( (HAL_GetTick() - start) < (seconds * 1000u) )
    {
        m1_esp_link_rx_poll();   /* query queue + dispatch any pending trigger */
        vTaskDelay(pdMS_TO_TICKS(ESPNOW_POLL_INTERVAL_MS));
    }
    printf("ESP-NOW: listen done\r\n");
}

/* ---- UI-facing API -------------------------------------------------------- */

bool m1_esp_link_enable_ok(uint8_t channel)
{
    if ( !espnow_esp_ready() ) return false;
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+M1ESPNOW=1,%u\r\n", channel);
    return espnow_at(cmd);
}

bool m1_esp_link_key_ok(const char *pass)
{
    if ( !espnow_esp_ready() ) return false;
    if ( pass == NULL ) pass = "";
    char cmd[32 + 64];
    snprintf(cmd, sizeof(cmd), "AT+M1ESPNOWKEY=\"%s\"\r\n", pass);
    return espnow_at(cmd);
}

bool m1_esp_link_pair_ok(const char *mac)
{
    if ( !espnow_esp_ready() || mac == NULL ) return false;
    char cmd[40];
    snprintf(cmd, sizeof(cmd), "AT+M1ESPNOWPAIR=\"%s\"\r\n", mac);
    return espnow_at(cmd);
}

bool m1_esp_link_trigger_ok(const char *mac, uint8_t ptype, const char *name)
{
    if ( !espnow_esp_ready() || mac == NULL || name == NULL ) return false;
    char cmd[52 + ESPNOW_MAX_NAME_LEN];
    snprintf(cmd, sizeof(cmd), "AT+M1ESPNOWTRIG=\"%s\",%u,\"%s\"\r\n", mac, ptype, name);
    return espnow_at(cmd);
}

bool m1_esp_link_send_file_ok(const char *mac, uint8_t ptype,
                              const char *path, const char *remote_name)
{
    if ( !espnow_esp_ready() || !mac || !path || !remote_name ) return false;

    static uint8_t buf[ESPNOW_MAX_FILE];
    FIL  fp;
    UINT br = 0;
    if ( f_open(&fp, path, FA_READ) != FR_OK )
    {
        printf("ESP-NOW: open FAILED %s\r\n", path);
        return false;
    }
    f_read(&fp, buf, sizeof(buf), &br);
    f_close(&fp);
    if ( br == 0 )
    {
        printf("ESP-NOW: empty/oversize file %s\r\n", path);
        return false;
    }

    uint8_t total = (uint8_t)((br + ESPNOW_FILE_CHUNK - 1) / ESPNOW_FILE_CHUNK);
    static char cmd[64 + 2 * ESPNOW_FILE_CHUNK + ESPNOW_MAX_NAME_LEN];
    static char hex[2 * ESPNOW_FILE_CHUNK + 1];

    for ( uint8_t frag = 0; frag < total; frag++ )
    {
        size_t off  = (size_t)frag * ESPNOW_FILE_CHUNK;
        size_t clen = br - off;
        if ( clen > ESPNOW_FILE_CHUNK ) clen = ESPNOW_FILE_CHUNK;
        espnow_hex_encode(buf + off, (uint8_t)clen, hex);
        snprintf(cmd, sizeof(cmd),
                 "AT+M1ESPNOWSEND=\"%s\",%u,\"%s\",%u,%u,\"%s\"\r\n",
                 mac, ptype, remote_name, frag, total, hex);
        if ( !espnow_at(cmd) )
        {
            printf("ESP-NOW: frag %u/%u FAILED\r\n", frag + 1, total);
            return false;
        }
    }
    printf("ESP-NOW: sent %s (%u bytes, %u frags)\r\n", remote_name, (unsigned)br, total);
    return true;
}

/* Add/refresh a peer (by MAC) in `out`; returns index. */
static int espnow_peer_upsert(m1_esp_peer_t *out, int count, int max_peers,
                              const char *mac, const char *name, int rssi)
{
    for ( int i = 0; i < count; i++ )
    {
        if ( strncmp(out[i].mac, mac, sizeof(out[i].mac)) == 0 )
        {
            out[i].rssi = (int8_t)rssi;              /* refresh RSSI */
            return i;
        }
    }
    if ( count >= max_peers ) return -1;
    strncpy(out[count].mac,  mac,  sizeof(out[count].mac) - 1);
    out[count].mac[sizeof(out[count].mac) - 1] = '\0';
    strncpy(out[count].name, name, sizeof(out[count].name) - 1);
    out[count].name[sizeof(out[count].name) - 1] = '\0';
    out[count].rssi = (int8_t)rssi;
    return count;
}

int m1_esp_link_scan_peers(m1_esp_peer_t *out, int max_peers, uint32_t seconds)
{
    if ( !espnow_esp_ready() || out == NULL || max_peers <= 0 ) return 0;
    if ( seconds < 1 )  seconds = 3;
    if ( seconds > 30 ) seconds = 30;

    /* Kick a HELLO beacon; peers reply/beacon and surface as +ESPNOWPEER lines
     * in the response buffer. Poll a bit longer than the scan to collect them. */
    static char resp[ESPNOW_AT_RESP_SIZE];
    char cmd[32];
    int count = 0;

    snprintf(cmd, sizeof(cmd), "AT+M1ESPNOWSCAN=%lu\r\n", (unsigned long)seconds);
    resp[0] = '\0';
    spi_AT_send_recv(cmd, resp, sizeof(resp), (int)seconds + 2);

    /* Parse every +ESPNOWPEER:<mac>,<name>,<rssi> line in the buffer. */
    const char *p = resp;
    while ( (p = strstr(p, "+ESPNOWPEER:")) != NULL )
    {
        p += 12;
        char mac[18] = {0}, name[16] = {0};
        int  rssi = 0;
        if ( sscanf(p, "%17[^,],%15[^,],%d", mac, name, &rssi) >= 1 )
        {
            int idx = espnow_peer_upsert(out, count, max_peers, mac, name, rssi);
            if ( idx == count && idx >= 0 ) count++;
        }
    }
    return count;
}
