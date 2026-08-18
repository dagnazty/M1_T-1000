/* See COPYING.txt for license details. */

/*
 * m1_esp_link_app.c
 *
 * M1 Link over ESP32 (ESP-NOW) — on-device UI (Phase 3).
 *
 * Menu app: brings ESP-NOW up, discovers peers via HELLO beacons, and lets the
 * user pick a peer and remote-trigger a BadUSB DuckyScript on it (encrypted +
 * paired). Talks to the ESP32-C6 over SPI/AT via the m1_esp_link_* API.
 *
 * Design: esp32-at-monstatek-m1/docs/ESPNOW_LINK_DESIGN.md
 *
 * M1 Project
 */

#include "m1_esp_link.h"
#include "m1_compile_cfg.h"

#ifdef M1_APP_ESPNOW_LINK_ENABLE

#include <stdio.h>
#include <string.h>
#include "main.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "m1_lcd.h"          /* m1_u8g2, firstpage/nextpage */
#include "m1_display.h"      /* fonts, colours, m1_draw_bottom_bar, bitmaps */
#include "m1_system.h"       /* BUTTON_*_KP_ID, BUTTON_EVENT_CLICK, button_events_q_hdl */
#include "m1_tasks.h"        /* main_q_hdl, Q_EVENT_KEYPAD, S_M1_Main_Q_t */
#include "m1_virtual_kb.h"   /* m1_vkb_get_filename (passphrase entry) */
#include "m1_storage.h"      /* storage_browse (file picker) */
#include "m1_file_browser.h" /* S_M1_file_info */
#include "flipper_file.h"    /* ff_open_write, ff_close */
#include "flipper_ir.h"      /* flipper_ir_* (IR button extract) */

/*--------------------------------- state -----------------------------------*/

#define ESPLINK_PEER_MAX   8
#define ESPLINK_CHANNEL    1
#define ESPLINK_IR_MAX_BTNS 64
#define ESPLINK_IR_NAME_LEN 18
#define ESPLINK_IR_TMP      "0:/IR/_esplink_tx.ir"

static m1_esp_peer_t s_peers[ESPLINK_PEER_MAX];
static int           s_peer_count = 0;

static char        s_ir_btn_names[ESPLINK_IR_MAX_BTNS][ESPLINK_IR_NAME_LEN];
static const char *s_ir_btn_ptrs[ESPLINK_IR_MAX_BTNS];

/* Scan a .ir remote's button names into s_ir_btn_names. Returns the count. */
static int esplink_ir_scan(const char *path)
{
    flipper_file_t ff;
    flipper_ir_signal_t sig;
    int n = 0;
    if ( !flipper_ir_open(&ff, path) )
        return 0;
    while ( n < ESPLINK_IR_MAX_BTNS && flipper_ir_read_signal(&ff, &sig) )
    {
        snprintf(s_ir_btn_names[n], ESPLINK_IR_NAME_LEN, "%.*s",
                 ESPLINK_IR_NAME_LEN - 1, sig.name);
        s_ir_btn_ptrs[n] = s_ir_btn_names[n];
        n++;
    }
    ff_close(&ff);
    return n;
}

/* Extract signal #index from `path` into a fresh single-signal .ir at out_path. */
static bool esplink_ir_extract(const char *path, int index, const char *out_path)
{
    flipper_file_t in, out;
    flipper_ir_signal_t sig;
    int i = 0;
    bool ok = false;
    if ( !flipper_ir_open(&in, path) )
        return false;
    while ( flipper_ir_read_signal(&in, &sig) )
    {
        if ( i == index )
        {
            if ( ff_open_write(&out, out_path) )
            {
                if ( flipper_ir_write_header(&out) )
                    ok = flipper_ir_write_signal(&out, &sig);
                ff_close(&out);
            }
            break;
        }
        i++;
    }
    ff_close(&in);
    return ok;
}

/*--------------------------------- drawing ---------------------------------*/

static void esplink_title(const char *title)
{
    u8g2_DrawXBMP(&m1_u8g2, 0, 0, 128, 14, m1_frame_128_14);
    u8g2_DrawStr(&m1_u8g2, 2, 11, title);
}

/* Read one button click within `timeout_ms`. Returns BUTTON_*_KP_ID or 0xFF. */
static uint8_t esplink_get_button(uint32_t timeout_ms)
{
    S_M1_Main_Q_t q_item;
    S_M1_Buttons_Status bs;
    uint8_t i;

    if ( xQueueReceive(main_q_hdl, &q_item, pdMS_TO_TICKS(timeout_ms)) == pdTRUE &&
         q_item.q_evt_type == Q_EVENT_KEYPAD )
    {
        if ( xQueueReceive(button_events_q_hdl, &bs, 0) == pdTRUE )
        {
            for ( i = 0; i < 6; i++ )
                if ( bs.event[i] == BUTTON_EVENT_CLICK )
                    return i;
        }
    }
    return 0xFF;
}

/* Two-line status card; waits for any button or `hold_ms` (0 = draw + return). */
static void esplink_status_card(const char *title, const char *l1, const char *l2,
                                uint32_t hold_ms)
{
    uint32_t waited = 0;
    m1_u8g2_firstpage();
    u8g2_SetDrawColor(&m1_u8g2, 1);
    u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
    esplink_title(title);
    if ( l1 ) u8g2_DrawStr(&m1_u8g2, 2, 30, l1);
    if ( l2 ) u8g2_DrawStr(&m1_u8g2, 2, 44, l2);
    m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Back", NULL, NULL);
    m1_u8g2_nextpage();
    while ( waited < hold_ms )
    {
        if ( esplink_get_button(100) != 0xFF ) break;
        waited += 100;
    }
}

/* Generic single-select list. Returns the chosen index, or -1 on Back. */
static int esplink_menu(const char *title, const char *const *items, int n)
{
    int sel = 0;
    bool redraw = true;
    for ( ;; )
    {
        if ( redraw )
        {
            int first = sel - 2; if ( first < 0 ) first = 0;
            if ( first > n - 3 ) first = (n > 3) ? n - 3 : 0;
            redraw = false;
            m1_u8g2_firstpage();
            u8g2_SetDrawColor(&m1_u8g2, 1);
            u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
            esplink_title(title);
            for ( int i = 0; i < 3; i++ )
            {
                int it = first + i;
                uint8_t y = (uint8_t)(24 + i * 10);
                if ( it >= n ) break;
                if ( it == sel )
                {
                    u8g2_DrawBox(&m1_u8g2, 0, y - 8, 128, 10);
                    u8g2_SetDrawColor(&m1_u8g2, 0);
                    u8g2_DrawStr(&m1_u8g2, 2, y, items[it]);
                    u8g2_SetDrawColor(&m1_u8g2, 1);
                }
                else u8g2_DrawStr(&m1_u8g2, 2, y, items[it]);
            }
            m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Back", "OK", NULL);
            m1_u8g2_nextpage();
        }
        uint8_t btn = esplink_get_button(200);
        if ( btn == BUTTON_BACK_KP_ID )       return -1;
        else if ( btn == BUTTON_UP_KP_ID )    { if ( sel > 0 )     { sel--; redraw = true; } }
        else if ( btn == BUTTON_DOWN_KP_ID )  { if ( sel < n - 1 ) { sel++; redraw = true; } }
        else if ( btn == BUTTON_OK_KP_ID || btn == BUTTON_RIGHT_KP_ID )
            return sel;
    }
}

/*------------------------------- flows -------------------------------------*/

/* Enter/replace the shared passphrase via the on-screen keyboard. */
static void esplink_set_passphrase(void)
{
    char pass[41] = {0};
    if ( m1_vkb_get_filename("Passphrase:", "", pass) )
    {
        bool ok = m1_esp_link_key_ok(pass);
        esplink_status_card("Passphrase", ok ? "Set" : "Failed", NULL, ok ? 1200 : 2500);
    }
}

/* Pick a payload type + file and send it (payload + all) to `mac` to run. */
static void esplink_trigger_flow(const char *mac)
{
    /* Sub-GHz removed for now: sub_ghz_replay_flipper_file() runs its own
     * blocking replay UI loop (waits for BACK), which doesn't fit the ESP-NOW
     * receive path. BadUSB + IR only. */
    const char *types[2] = { "BadUSB", "IR" };
    const uint8_t ptypes[2] = { M1_ESPNOW_PTYPE_BADUSB, M1_ESPNOW_PTYPE_IR };
    const char *dirs[2]  = { "0:/BadUSB", "0:/IR" };

    int t = esplink_menu("Trigger: type", types, 2);
    if ( t < 0 ) return;

    S_M1_file_info *fi = storage_browse(dirs[t]);
    if ( !fi || !fi->file_is_selected ) return;

    char path[256];
    snprintf(path, sizeof(path), "%s/%s", fi->dir_name, fi->file_name);

    /* IR: a .ir is a whole remote — open it, pick ONE button, and transfer just
     * that single extracted signal (the peer plays the one button). */
    if ( ptypes[t] == M1_ESPNOW_PTYPE_IR )
    {
        int nb = esplink_ir_scan(path);
        if ( nb <= 0 ) { esplink_status_card("IR", "No buttons in file", NULL, 2500); return; }
        int b = esplink_menu("IR: button", s_ir_btn_ptrs, nb);
        if ( b < 0 ) return;
        if ( !esplink_ir_extract(path, b, ESPLINK_IR_TMP) )
        { esplink_status_card("IR", "Extract failed", NULL, 2500); return; }

        esplink_status_card("Trigger", s_ir_btn_names[b], "Sending IR...", 0);
        bool ok = m1_esp_link_send_file_ok(mac, ptypes[t], ESPLINK_IR_TMP, fi->file_name);
        esplink_status_card("Trigger", ok ? "Sent (peer plays it)" : "Send failed",
                            NULL, 3500);
        return;
    }

    /* BadUSB / Sub-GHz: transfer the file as-is, peer writes + runs it. */
    esplink_status_card("Trigger", fi->file_name, "Sending payload...", 0);
    bool ok = m1_esp_link_send_file_ok(mac, ptypes[t], path, fi->file_name);
    esplink_status_card("Trigger", ok ? "Sent (peer runs it)" : "Send failed",
                        NULL, 3500);
}

/* Actions for a selected peer: Trigger BadUSB, or Pair. */
static void esplink_peer_actions(const m1_esp_peer_t *peer)
{
    const char *items[] = { "Trigger Code", "Pair peer" };
    char title[24];
    snprintf(title, sizeof(title), "%.6s %s", peer->name, peer->mac + 12); /* name + mac tail */

    for ( ;; )
    {
        int sel = esplink_menu(title, items, 2);
        if ( sel < 0 ) return;
        if ( sel == 0 )
        {
            esplink_trigger_flow(peer->mac);
        }
        else
        {
            bool ok = m1_esp_link_pair_ok(peer->mac);
            esplink_status_card("Pair", ok ? peer->mac : "Failed", ok ? "paired" : NULL,
                                ok ? 1500 : 2500);
        }
    }
}

/* Receiver mode: sit and run inbound triggers. badusb_execute_file() runs in
 * THIS (main UI) task, so the USB->HID switch works — unlike the RPC CLI path. */
static void esplink_listen_mode(void)
{
    bool redraw = true;
    for ( ;; )
    {
        if ( redraw )
        {
            redraw = false;
            m1_u8g2_firstpage();
            u8g2_SetDrawColor(&m1_u8g2, 1);
            u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
            esplink_title("ESP Link: listen");
            u8g2_DrawStr(&m1_u8g2, 2, 30, "Waiting for triggers");
            u8g2_DrawStr(&m1_u8g2, 2, 42, "(same passphrase)");
            m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Back", NULL, NULL);
            m1_u8g2_nextpage();
        }
        /* Poll the ESP's trigger queue; a pending BadUSB runs here (HID switch
         * happens in this task). It re-enumerates USB, so on return redraw. */
        if ( m1_esp_link_rx_poll() )
            redraw = true;

        if ( esplink_get_button(120) == BUTTON_BACK_KP_ID )
            return;
    }
}

/*------------------------------- app entry ---------------------------------*/

void m1_esp_link_app_run(void)
{
    xQueueReset(main_q_hdl);

    esplink_status_card("ESP Link", "Starting ESP-NOW...", NULL, 0);
    if ( !m1_esp_link_enable_ok(ESPLINK_CHANNEL) )
    {
        esplink_status_card("ESP Link", "ESP-NOW start", "FAILED", 3000);
        return;
    }

    bool rescan = true;
    for ( ;; )
    {
        if ( rescan )
        {
            rescan = false;
            esplink_status_card("ESP Link", "Scanning...", NULL, 0);
            s_peer_count = m1_esp_link_scan_peers(s_peers, ESPLINK_PEER_MAX, 3);
        }

        /* Build the list: peers + [Listen] + [Rescan] + [Passphrase]. */
        const char *labels[ESPLINK_PEER_MAX + 3];
        char        rows[ESPLINK_PEER_MAX][24];
        int n = 0;
        for ( int i = 0; i < s_peer_count; i++ )
        {
            snprintf(rows[i], sizeof(rows[i]), "%.5s %s %d",
                     s_peers[i].name, s_peers[i].mac + 12, (int)s_peers[i].rssi);
            labels[n++] = rows[i];
        }
        int idx_listen = n; labels[n++] = "[Listen for triggers]";
        int idx_rescan = n; labels[n++] = "[Rescan]";
        int idx_key    = n; labels[n++] = "[Set passphrase]";

        int sel = esplink_menu("ESP Link: peers", labels, n);
        if ( sel < 0 )                 return;              /* Back exits app */
        else if ( sel == idx_listen )  esplink_listen_mode();
        else if ( sel == idx_rescan )  rescan = true;
        else if ( sel == idx_key )     esplink_set_passphrase();
        else                           esplink_peer_actions(&s_peers[sel]);
    }
}

#endif /* M1_APP_ESPNOW_LINK_ENABLE */
