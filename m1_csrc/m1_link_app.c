/* See COPYING.txt for license details. */

/*
 * m1_link_app.c
 *
 * M1 Link — on-device Chat UI (Phase 3).
 *
 * Menu app: discovers peers via HELLO beacons (a live device list), then opens a
 * conversation where the user composes messages with the virtual keyboard and
 * they are delivered with the Phase 1/2 reliable-unicast + fragmentation stack.
 * Incoming messages are shown live with a buzzer/LED notification.
 *
 * Runs in the main UI task: it owns the radio (FSK packet mode) while active,
 * mutually exclusive with the sub-GHz scan/replay app.
 *
 * M1 Project
 */

#include "m1_link.h"

#ifdef M1_APP_LINK_ENABLE

#include <stdio.h>
#include <string.h>
#include "main.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "m1_lcd.h"          /* m1_u8g2, firstpage/nextpage */
#include "m1_display.h"      /* fonts, colours, m1_draw_bottom_bar, bitmaps */
#include "m1_system.h"       /* BUTTON_*_KP_ID, BUTTON_EVENT_CLICK, button_events_q_hdl */
#include "m1_tasks.h"        /* main_q_hdl, Q_EVENT_KEYPAD, S_M1_Main_Q_t */
#include "m1_virtual_kb.h"   /* m1_vkbs_get_data */
#include "m1_buzzer.h"       /* m1_buzzer_notification */
#include "m1_sub_ghz_api.h"  /* radio_set_antenna_mode */
#include "m1_storage.h"      /* storage_browse (send-file picker) */
#include "m1_file_browser.h" /* S_M1_file_info */
#include "flipper_file.h"    /* ff_open_write, ff_close (IR button extract) */
#include "flipper_ir.h"      /* flipper_ir_* (IR button picker) */

/*--------------------------------- state -----------------------------------*/

#define LINK_PEER_MAX      8
#define LINK_LOG_MAX       16
#define LINK_MSG_TEXT_MAX  41         /* stored/displayed message chars + NUL */
#define LINK_COMPOSE_MAX   40         /* compose buffer (full kb caps msg at 20) */
#define LINK_HELLO_PERIOD  1500u      /* ms between our HELLO beacons */

typedef struct {
    uint16_t id;
    char     name[14];
    int8_t   rssi;
    uint8_t  used;
} link_peer_t;

typedef struct {
    uint16_t peer;      /* remote id (0xFFFF for broadcast) */
    uint8_t  dir;       /* 0 = received, 1 = sent */
    char     text[LINK_MSG_TEXT_MAX];
} link_logent_t;

static link_peer_t   s_peers[LINK_PEER_MAX];
static uint8_t       s_peer_count;
static link_logent_t s_log[LINK_LOG_MAX];
static uint8_t       s_log_head, s_log_count;

static void link_log_add(uint16_t peer, uint8_t dir, const char *text)
{
    link_logent_t *e = &s_log[s_log_head];
    e->peer = peer;
    e->dir  = dir;
    snprintf(e->text, sizeof(e->text), "%.*s", (int)(sizeof(e->text) - 1), text);
    s_log_head = (uint8_t)((s_log_head + 1) % LINK_LOG_MAX);
    if ( s_log_count < LINK_LOG_MAX )
        s_log_count++;
}

static void link_peer_seen(uint16_t id, const char *name, int8_t rssi)
{
    uint8_t i;
    for ( i = 0; i < s_peer_count; i++ )
    {
        if ( s_peers[i].used && s_peers[i].id == id )
        {
            s_peers[i].rssi = rssi;
            if ( name )
                snprintf(s_peers[i].name, sizeof(s_peers[i].name), "%.*s",
                         (int)(sizeof(s_peers[i].name) - 1), name);
            return;
        }
    }
    if ( s_peer_count < LINK_PEER_MAX )
    {
        link_peer_t *p = &s_peers[s_peer_count++];
        p->used = 1; p->id = id; p->rssi = rssi;
        if ( name ) snprintf(p->name, sizeof(p->name), "%.*s",
                             (int)(sizeof(p->name) - 1), name);
        else        snprintf(p->name, sizeof(p->name), "M1-%04X", (unsigned)id);
    }
}

/*--------------------------------- drawing ---------------------------------*/

static void link_title(const char *title)
{
    u8g2_DrawXBMP(&m1_u8g2, 0, 0, 128, 14, m1_frame_128_14);
    u8g2_DrawStr(&m1_u8g2, 2, 11, title);
}

/* Read one button click (non-blocking-ish, `timeout_ms`). Returns the
 * BUTTON_*_KP_ID that was clicked, or 0xFF if none within the timeout. */
static uint8_t link_get_button(uint32_t timeout_ms)
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

/*--------------------------- conversation screen ---------------------------*/

static void link_compose_and_send(uint16_t peer)
{
    char msg[LINK_COMPOSE_MAX + 1];
    int  n;

    /* Full alphanumeric/symbol keyboard (lowercase/UPPERCASE/symbols pages),
     * starting empty. Returns 0 if cancelled. */
    msg[0] = '\0';
    if ( m1_vkb_get_filename("Message:", "", msg) == 0 )
        return;   /* cancelled */

    /* Trim trailing spaces. */
    for ( n = (int)strlen(msg) - 1; n >= 0 && msg[n] == ' '; n-- )
        msg[n] = '\0';
    if ( msg[0] == '\0' )
        return;   /* empty */

    link_log_add(peer, 1, msg);
    m1_link_rx_reset();
    (void)m1_link_send(peer, (const uint8_t *)msg, (uint16_t)strlen(msg));
}

static void link_conversation(uint16_t peer, const char *peer_name)
{
    char     rxbuf[M1_LINK_MAX_MSG + 1];
    uint16_t from, mlen;
    int8_t   rssi;
    bool     redraw = true;
    uint8_t  btn;

    m1_link_rx_reset();
    m1_link_rx_arm();

    for ( ;; )
    {
        if ( redraw )
        {
            char title[22];
            uint8_t shown = 0, i;
            int idx;

            redraw = false;
            snprintf(title, sizeof(title), "%.20s", peer_name);

            m1_u8g2_firstpage();
            u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
            u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
            link_title(title);

            /* Walk the log newest-first, collect up to 4 lines for this peer,
             * then print them oldest-at-top. */
            {
                char lines[4][24];
                for ( i = 0; i < s_log_count && shown < 4; i++ )
                {
                    idx = (int)s_log_head - 1 - (int)i;
                    while ( idx < 0 ) idx += LINK_LOG_MAX;
                    if ( s_log[idx].peer == peer )
                    {
                        snprintf(lines[shown], sizeof(lines[shown]), "%c%.20s",
                                 s_log[idx].dir ? '>' : '<', s_log[idx].text);
                        shown++;
                    }
                }
                for ( i = 0; i < shown; i++ )
                    u8g2_DrawStr(&m1_u8g2, 2, 24 + (shown - 1 - i) * 9,
                                 lines[i]);
            }

            m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Back", "Send", NULL);
            m1_u8g2_nextpage();
        }

        /* Pump the radio a few times, then check for a button. */
        {
            uint8_t k;
            for ( k = 0; k < 8; k++ )
            {
                if ( m1_link_rx_process(&from, rxbuf, sizeof(rxbuf), &mlen, &rssi)
                     == M1_LINK_RX_MESSAGE )
                {
                    if ( from == peer || peer == M1_LINK_ID_BROADCAST )
                    {
                        link_log_add(from, 0, rxbuf);
                        m1_buzzer_notification();
                        redraw = true;
                    }
                    else
                    {
                        /* message from a different peer — still note who */
                        link_peer_seen(from, NULL, rssi);
                    }
                }
            }
        }

        btn = link_get_button(10);
        if ( btn == BUTTON_BACK_KP_ID || btn == BUTTON_LEFT_KP_ID )
            break;
        if ( btn == BUTTON_OK_KP_ID || btn == BUTTON_RIGHT_KP_ID )
        {
            link_compose_and_send(peer);
            m1_link_rx_arm();
            redraw = true;
        }
    }
}

/*------------------------------ settings screen ----------------------------*/

static void link_settings(void)
{
    m1_link_cfg_t *cfg = m1_link_cfg();
    const uint8_t  pwr_dbm[4] = { 10, 40, 80, 127 };
    int  sel = 0;
    bool redraw = true;
    uint8_t btn;

    for ( ;; )
    {
        if ( redraw )
        {
            char l[26]; int i, first;
            redraw = false;
            m1_u8g2_firstpage();
            u8g2_SetDrawColor(&m1_u8g2, 1);
            u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
            link_title("M1 Link Settings");

            /* 3 visible rows, scrolled to keep the selection on-screen. */
            first = sel - 2; if ( first < 0 ) first = 0;
            if ( first > 1 ) first = 1;   /* 4 fields, 3 visible -> max offset 1 */
            for ( i = 0; i < 3; i++ )
            {
                int field = first + i;
                uint8_t y = (uint8_t)(24 + i * 10);
                if ( field > 3 ) break;
                switch ( field )
                {
                    case 0: snprintf(l, sizeof(l), "Name: %s", cfg->callsign); break;
                    case 1: snprintf(l, sizeof(l), "Key:  %s",
                                     cfg->passphrase[0] ? "(set)" : "(none)"); break;
                    case 2: snprintf(l, sizeof(l), "Chan: %u", (unsigned)cfg->channel); break;
                    default:snprintf(l, sizeof(l), "Power: %u", (unsigned)pwr_dbm[cfg->tx_power_idx]); break;
                }
                if ( field == sel )
                {
                    u8g2_DrawBox(&m1_u8g2, 0, y - 8, 128, 10);
                    u8g2_SetDrawColor(&m1_u8g2, 0);
                    u8g2_DrawStr(&m1_u8g2, 2, y, l);
                    u8g2_SetDrawColor(&m1_u8g2, 1);
                }
                else u8g2_DrawStr(&m1_u8g2, 2, y, l);
            }
            m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Save", "Edit", NULL);
            m1_u8g2_nextpage();
        }

        btn = link_get_button(200);
        if ( btn == BUTTON_BACK_KP_ID )
            break;
        else if ( btn == BUTTON_UP_KP_ID )   { if ( sel > 0 ) sel--; redraw = true; }
        else if ( btn == BUTTON_DOWN_KP_ID ) { if ( sel < 3 ) sel++; redraw = true; }
        else if ( btn == BUTTON_LEFT_KP_ID )
        {
            if ( sel == 2 && cfg->channel > 0 ) cfg->channel--;
            else if ( sel == 3 && cfg->tx_power_idx > 0 ) cfg->tx_power_idx--;
            redraw = true;
        }
        else if ( btn == BUTTON_RIGHT_KP_ID )
        {
            if ( sel == 2 && cfg->channel < M1_LINK_CHAN_MAX ) cfg->channel++;
            else if ( sel == 3 && cfg->tx_power_idx < 3 ) cfg->tx_power_idx++;
            redraw = true;
        }
        else if ( btn == BUTTON_OK_KP_ID )
        {
            char t[24];
            if ( sel == 0 )
            {
                snprintf(t, sizeof(t), "%.*s", (int)(sizeof(t) - 1), cfg->callsign);
                if ( m1_vkb_get_filename("Callsign:", t, t) )
                    snprintf(cfg->callsign, sizeof(cfg->callsign), "%.*s",
                             (int)(sizeof(cfg->callsign) - 1), t);
            }
            else if ( sel == 1 )
            {
                t[0] = '\0';
                if ( m1_vkb_get_filename("Passphrase:", "", t) )
                    snprintf(cfg->passphrase, sizeof(cfg->passphrase), "%.*s",
                             (int)(sizeof(cfg->passphrase) - 1), t);
            }
            redraw = true;
        }
    }

    m1_link_cfg_save();   /* persist to SD + re-derive AES key */
}

/*---------------------------- peer actions menu ----------------------------*/

/* Draw a two-line status card and wait for any button (or `hold_ms` timeout). */
static void link_status_card(const char *title, const char *l1, const char *l2,
                             uint32_t hold_ms)
{
    uint32_t waited = 0;
    m1_u8g2_firstpage();
    u8g2_SetDrawColor(&m1_u8g2, 1);
    u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
    link_title(title);
    if ( l1 ) u8g2_DrawStr(&m1_u8g2, 2, 30, l1);
    if ( l2 ) u8g2_DrawStr(&m1_u8g2, 2, 44, l2);
    m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Back", NULL, NULL);
    m1_u8g2_nextpage();
    while ( waited < hold_ms )
    {
        if ( link_get_button(100) != 0 ) break;
        waited += 100;
    }
}

/* Pick a capture from SD and send it to `id`, with a result card. */
static void link_send_file_flow(uint16_t id)
{
    const char *dirs[3]   = { "0:/SUBGHZ", "0:/NFC", "0:/IR" };
    const char *labels[3] = { "SubGHz", "NFC", "IR" };
    int  sel = 0;
    bool redraw = true;
    uint8_t btn;

    /* Pick which capture folder to browse. */
    for ( ;; )
    {
        if ( redraw )
        {
            int i;
            redraw = false;
            m1_u8g2_firstpage();
            u8g2_SetDrawColor(&m1_u8g2, 1);
            u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
            link_title("Send File: type");
            for ( i = 0; i < 3; i++ )
            {
                uint8_t y = (uint8_t)(24 + i * 10);
                if ( i == sel )
                {
                    u8g2_DrawBox(&m1_u8g2, 0, y - 8, 128, 10);
                    u8g2_SetDrawColor(&m1_u8g2, 0);
                    u8g2_DrawStr(&m1_u8g2, 2, y, labels[i]);
                    u8g2_SetDrawColor(&m1_u8g2, 1);
                }
                else u8g2_DrawStr(&m1_u8g2, 2, y, labels[i]);
            }
            m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Back", "OK", NULL);
            m1_u8g2_nextpage();
        }
        btn = link_get_button(200);
        if ( btn == BUTTON_BACK_KP_ID )
            return;
        else if ( btn == BUTTON_UP_KP_ID )   { if ( sel > 0 ) sel--; redraw = true; }
        else if ( btn == BUTTON_DOWN_KP_ID ) { if ( sel < 2 ) sel++; redraw = true; }
        else if ( btn == BUTTON_OK_KP_ID || btn == BUTTON_RIGHT_KP_ID )
            break;
    }

    /* Interactive file picker for the chosen folder. */
    {
        S_M1_file_info *fi = storage_browse(dirs[sel]);
        if ( fi && fi->file_is_selected )
        {
            char path[256]; uint32_t sent = 0; bool ok;
            snprintf(path, sizeof(path), "%s/%s", fi->dir_name, fi->file_name);
            link_status_card("Send File", fi->file_name, "Sending...", 0);
            ok = m1_link_send_file(id, path, M1_LINK_FILE_ACT_SAVE, &sent);
            if ( ok )
            {
                char l2[24];
                snprintf(l2, sizeof(l2), "%lu bytes", (unsigned long)sent);
                link_status_card("Send File", "Sent OK", l2, 4000);
            }
            else
                link_status_card("Send File", "FAILED / no ACK", NULL, 3000);
        }
    }
}

/* ---- IR: pick a single button from a multi-button .ir remote ---- */
#define LINK_IR_MAX_BTNS   64
#define LINK_IR_NAME_LEN   18
static char s_ir_btn_names[LINK_IR_MAX_BTNS][LINK_IR_NAME_LEN];

/* Scan the .ir file's button names. Returns the count (0 on failure). */
static int link_ir_scan(const char *path)
{
    flipper_file_t ff;
    flipper_ir_signal_t sig;
    int n = 0;

    if ( !flipper_ir_open(&ff, path) )
        return 0;
    while ( n < LINK_IR_MAX_BTNS && flipper_ir_read_signal(&ff, &sig) )
    {
        snprintf(s_ir_btn_names[n], LINK_IR_NAME_LEN, "%.*s",
                 LINK_IR_NAME_LEN - 1, sig.name);
        n++;
    }
    ff_close(&ff);
    return n;
}

/* Extract signal #index from `path` into a fresh single-signal .ir at out_path. */
static bool link_ir_extract(const char *path, int index, const char *out_path)
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

/* Pick one button from a .ir remote (scrolling list), resuming at `start_sel`.
 * Returns the chosen index, or -1 on Back. */
static int link_ir_pick_button(const char *path, int start_sel)
{
    int count = link_ir_scan(path);
    int sel;
    bool redraw = true;
    uint8_t btn;

    if ( count <= 0 )
    {
        link_status_card("Trigger", "No IR buttons", NULL, 3000);
        return -1;
    }
    sel = start_sel;
    if ( sel < 0 ) sel = 0;
    if ( sel >= count ) sel = count - 1;

    for ( ;; )
    {
        if ( redraw )
        {
            int first, i;
            redraw = false;
            first = sel - 2; if ( first < 0 ) first = 0;
            if ( first > count - 3 ) first = (count > 3) ? count - 3 : 0;
            m1_u8g2_firstpage();
            u8g2_SetDrawColor(&m1_u8g2, 1);
            u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
            link_title("Pick a button");
            for ( i = 0; i < 3; i++ )
            {
                int it = first + i;
                uint8_t y = (uint8_t)(24 + i * 10);
                if ( it >= count ) break;
                if ( it == sel )
                {
                    u8g2_DrawBox(&m1_u8g2, 0, y - 8, 128, 10);
                    u8g2_SetDrawColor(&m1_u8g2, 0);
                    u8g2_DrawStr(&m1_u8g2, 2, y, s_ir_btn_names[it]);
                    u8g2_SetDrawColor(&m1_u8g2, 1);
                }
                else u8g2_DrawStr(&m1_u8g2, 2, y, s_ir_btn_names[it]);
            }
            m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Back", "Send", NULL);
            m1_u8g2_nextpage();
        }
        btn = link_get_button(200);
        if ( btn == BUTTON_BACK_KP_ID )
            return -1;
        else if ( btn == BUTTON_UP_KP_ID )   { if ( sel > 0 ) sel--; redraw = true; }
        else if ( btn == BUTTON_DOWN_KP_ID ) { if ( sel < count - 1 ) sel++; redraw = true; }
        else if ( btn == BUTTON_OK_KP_ID || btn == BUTTON_RIGHT_KP_ID )
            return sel;
    }
}

/* Choose a payload type, pick one of OUR files of that type, and ask the peer
 * to run its own copy. Requires a shared passphrase (peer ignores otherwise). */
static void link_trigger_flow(uint16_t id)
{
    const char *labels[4] = { "Sub-GHz", "BadUSB", "Bad-BT", "IR" };
    const uint8_t types[4] = { M1_LINK_TRIG_SUB, M1_LINK_TRIG_BADUSB,
                               M1_LINK_TRIG_BADBT, M1_LINK_TRIG_IR };
    const char *dirs[4] = { "0:/SUBGHZ", "0:/BadUSB", "0:/BadUSB", "0:/IR" };
    int  sel = 0;
    bool redraw = true;
    uint8_t btn;

    if ( !m1_link_encrypted() )
    {
        link_status_card("Trigger", "Set a passphrase", "(peer will ignore)", 3500);
        return;
    }

    for ( ;; )   /* pick payload type */
    {
        if ( redraw )
        {
            int i;
            redraw = false;
            m1_u8g2_firstpage();
            u8g2_SetDrawColor(&m1_u8g2, 1);
            u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
            link_title("Trigger: type");
            /* 4 items, 3 visible rows, scrolled to keep the selection on screen. */
            int first = sel - 1; if ( first < 0 ) first = 0;
            if ( first > 1 ) first = 1;   /* 4 items - 3 visible */
            for ( i = 0; i < 3; i++ )
            {
                int it = first + i;
                uint8_t y = (uint8_t)(24 + i * 10);
                if ( it > 3 ) break;
                if ( it == sel )
                {
                    u8g2_DrawBox(&m1_u8g2, 0, y - 8, 128, 10);
                    u8g2_SetDrawColor(&m1_u8g2, 0);
                    u8g2_DrawStr(&m1_u8g2, 2, y, labels[it]);
                    u8g2_SetDrawColor(&m1_u8g2, 1);
                }
                else u8g2_DrawStr(&m1_u8g2, 2, y, labels[it]);
            }
            m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Back", "OK", NULL);
            m1_u8g2_nextpage();
        }
        btn = link_get_button(200);
        if ( btn == BUTTON_BACK_KP_ID )
            return;
        else if ( btn == BUTTON_UP_KP_ID )   { if ( sel > 0 ) sel--; redraw = true; }
        else if ( btn == BUTTON_DOWN_KP_ID ) { if ( sel < 3 ) sel++; redraw = true; }
        else if ( btn == BUTTON_OK_KP_ID || btn == BUTTON_RIGHT_KP_ID )
        {
            S_M1_file_info *fi = storage_browse(dirs[sel]);
            if ( fi && fi->file_is_selected )
            {
                bool ok = false;
                char path[256];
                snprintf(path, sizeof(path), "%s/%s", fi->dir_name, fi->file_name);

                if ( types[sel] == M1_LINK_TRIG_IR )
                {
                    /* An .ir file is a whole remote. Stay in the button list and
                     * fire one button per OK (e.g. Vol+ repeatedly); Back exits
                     * the list. Resume selection so repeats are one keypress. */
                    int bsel = 0;
                    for ( ;; )
                    {
                        int idx = link_ir_pick_button(path, bsel);
                        if ( idx < 0 )
                            break;   /* Back — leave the button list */
                        bsel = idx;
                        {
                            const char *tmp = "0:/IR/_link_ir_tx.ir";
                            if ( link_ir_extract(path, idx, tmp) )
                            {
                                link_status_card("Trigger", s_ir_btn_names[idx],
                                                 "Sending...", 0);
                                ok = m1_link_trigger(id, M1_LINK_TRIG_IR, tmp);
                                /* Brief confirmation, then back to the list. */
                                link_status_card("Trigger", ok ? "Sent" : "Failed",
                                                 NULL, ok ? 600 : 2500);
                            }
                            else
                                link_status_card("Trigger", "Extract failed", NULL, 3000);
                        }
                    }
                }
                else
                {
                    link_status_card("Trigger", fi->file_name, "Sending + running...", 0);
                    ok = m1_link_trigger(id, types[sel], path);
                    link_status_card("Trigger", ok ? "Sent (peer runs it)" : "Failed",
                                     NULL, 3500);
                }
            }
            redraw = true;
        }
    }
}

static void link_peer_actions(uint16_t id, const char *name)
{
    const char *items[5] = { "Message", "Ping", "Locate", "Send File", "Trigger" };
    int  sel = 0;
    bool redraw = true;
    uint8_t btn;

    for ( ;; )
    {
        if ( redraw )
        {
            char t[24]; int i;
            redraw = false;
            m1_u8g2_firstpage();
            u8g2_SetDrawColor(&m1_u8g2, 1);
            u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
            snprintf(t, sizeof(t), "%.12s %04X", name, (unsigned)id);
            link_title(t);
            /* 5 items, 3 visible rows, scrolled to keep selection on screen. */
            {
                int first = sel - 2; if ( first < 0 ) first = 0;
                if ( first > 2 ) first = 2;
                for ( i = 0; i < 3; i++ )
                {
                    int it = first + i;
                    uint8_t y = (uint8_t)(24 + i * 10);
                    if ( it > 4 ) break;
                    if ( it == sel )
                    {
                        u8g2_DrawBox(&m1_u8g2, 0, y - 8, 128, 10);
                        u8g2_SetDrawColor(&m1_u8g2, 0);
                        u8g2_DrawStr(&m1_u8g2, 2, y, items[it]);
                        u8g2_SetDrawColor(&m1_u8g2, 1);
                    }
                    else u8g2_DrawStr(&m1_u8g2, 2, y, items[it]);
                }
            }
            m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Back", "OK", NULL);
            m1_u8g2_nextpage();
        }

        btn = link_get_button(200);
        if ( btn == BUTTON_BACK_KP_ID )
            break;
        else if ( btn == BUTTON_UP_KP_ID )   { if ( sel > 0 ) sel--; redraw = true; }
        else if ( btn == BUTTON_DOWN_KP_ID ) { if ( sel < 4 ) sel++; redraw = true; }
        else if ( btn == BUTTON_OK_KP_ID || btn == BUTTON_RIGHT_KP_ID )
        {
            if ( sel == 0 )
            {
                link_conversation(id, name);
            }
            else if ( sel == 3 )   /* Send File */
            {
                link_send_file_flow(id);
            }
            else if ( sel == 4 )   /* Trigger */
            {
                link_trigger_flow(id);
            }
            else if ( sel == 1 )   /* Ping */
            {
                int8_t peer = 0; int rtt;
                link_status_card("Ping", "Pinging...", NULL, 0);
                rtt = m1_link_ping(id, &peer);
                if ( rtt >= 0 )
                {
                    char l1[24], l2[24];
                    snprintf(l1, sizeof(l1), "rtt: %d ms", rtt);
                    snprintf(l2, sizeof(l2), "peer heard: %d dBm", (int)peer);
                    link_status_card("Ping OK", l1, l2, 4000);
                }
                else
                    link_status_card("Ping", "No response", NULL, 3000);
            }
            else                   /* Locate */
            {
                bool ok;
                link_status_card("Locate", "Locating...", NULL, 0);
                ok = m1_link_locate(id);
                link_status_card("Locate", ok ? "Peer is beeping" : "No response",
                                 NULL, 3000);
            }
            m1_link_rx_reset();
            m1_link_rx_arm();
            redraw = true;
        }
    }
}

/*------------------------------- peer list ---------------------------------*/

void m1_link_app_run(void)
{
    char     rxbuf[M1_LINK_MAX_MSG + 1];
    uint16_t from, mlen;
    int8_t   rssi;
    int16_t  sel = 0;
    bool     redraw = true;
    uint32_t last_hello = 0, last_refresh = 0;
    uint8_t  btn;

    /* Reset state + radio. */
    s_peer_count = 0; s_log_head = 0; s_log_count = 0;
    memset(s_peers, 0, sizeof(s_peers));
    memset(s_log, 0, sizeof(s_log));
    xQueueReset(main_q_hdl);

    u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
    m1_u8g2_firstpage();
    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
    link_title("M1 Link");
    u8g2_DrawStr(&m1_u8g2, 2, 30, "Starting radio...");
    m1_u8g2_nextpage();

    if ( !m1_link_radio_bringup() )
    {
        m1_u8g2_firstpage();
        link_title("M1 Link");
        u8g2_DrawStr(&m1_u8g2, 2, 30, "Radio init FAILED");
        m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Back", "", NULL);
        m1_u8g2_nextpage();
        while ( link_get_button(200) == 0xFF ) { }
        return;
    }

    m1_link_rx_reset();
    m1_link_rx_arm();

    for ( ;; )
    {
        uint32_t now = HAL_GetTick();

        /* Periodically announce ourselves so peers discover us too. */
        if ( now - last_hello >= LINK_HELLO_PERIOD )
        {
            last_hello = now;
            m1_link_send_hello();
        }

        /* Pump the radio: collect HELLOs (peers) and any messages. */
        {
            uint8_t k;
            for ( k = 0; k < 8; k++ )
            {
                m1_link_rx_kind_t kind =
                    m1_link_rx_process(&from, rxbuf, sizeof(rxbuf), &mlen, &rssi);
                if ( kind == M1_LINK_RX_HELLO )
                {
                    uint8_t before = s_peer_count;
                    link_peer_seen(from, rxbuf, rssi);
                    if ( s_peer_count != before ) redraw = true;
                }
                else if ( kind == M1_LINK_RX_MESSAGE )
                {
                    link_peer_seen(from, NULL, rssi);
                    link_log_add(from, 0, rxbuf);
                    m1_buzzer_notification();
                    redraw = true;
                }
                else if ( kind == M1_LINK_RX_FILE_START )
                {
                    char note[40];
                    link_peer_seen(from, NULL, rssi);
                    snprintf(note, sizeof(note), "File: %.28s", rxbuf);
                    link_log_add(from, 0, note);
                    m1_buzzer_notification();
                    redraw = true;
                }
                else if ( kind == M1_LINK_RX_FILE_END )
                {
                    char note[48];
                    snprintf(note, sizeof(note), "Saved %.34s", rxbuf);
                    link_log_add(from, 0, note);
                    m1_buzzer_notification();
                    redraw = true;
                }
                else if ( kind == M1_LINK_RX_TRIGGER )
                {
                    char note[48];
                    snprintf(note, sizeof(note), "Triggered %.30s", rxbuf);
                    link_log_add(from, 0, note);
                    m1_buzzer_notification();
                    redraw = true;
                }
            }
        }

        /* Refresh at least once a second to update RSSI. */
        if ( now - last_refresh >= 1000 )
        {
            last_refresh = now;
            redraw = true;
        }

        if ( redraw )
        {
            uint8_t total = (uint8_t)(2 + s_peer_count); /* [Broadcast] [Settings] + peers */
            uint8_t i;
            redraw = false;

            if ( sel < 0 ) sel = 0;
            if ( sel >= total ) sel = total - 1;

            m1_u8g2_firstpage();
            u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
            u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
            {
                char t[22];
                snprintf(t, sizeof(t), "M1 Link  %04X", (unsigned)m1_link_my_id());
                link_title(t);
            }

            /* Draw up to 3 rows (y=24/34/44, clear of the bottom bar at y=51),
             * scrolled to keep the selection visible. */
            {
                int16_t first = sel - 1; if ( first < 0 ) first = 0;
                if ( first > total - 3 ) first = (total > 3) ? total - 3 : 0;
                for ( i = 0; i < 3 && (first + i) < total; i++ )
                {
                    uint8_t row = (uint8_t)(first + i);
                    uint8_t y = (uint8_t)(24 + i * 10);
                    char line[26];
                    if ( row == 0 )
                        snprintf(line, sizeof(line), "[Broadcast]");
                    else if ( row == 1 )
                        snprintf(line, sizeof(line), "[Settings]%s",
                                 m1_link_encrypted() ? "  (enc)" : "");
                    else
                        snprintf(line, sizeof(line), "%s  %ddBm",
                                 s_peers[row - 2].name, s_peers[row - 2].rssi);

                    if ( row == sel )
                    {
                        u8g2_DrawBox(&m1_u8g2, 0, y - 8, 128, 10);
                        u8g2_SetDrawColor(&m1_u8g2, 0);
                        u8g2_DrawStr(&m1_u8g2, 2, y, line);
                        u8g2_SetDrawColor(&m1_u8g2, 1);
                    }
                    else
                    {
                        u8g2_DrawStr(&m1_u8g2, 2, y, line);
                    }
                }
                if ( s_peer_count == 0 )
                    u8g2_DrawStr(&m1_u8g2, 2, 44, "scanning...");
            }

            m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Back", "Open", NULL);
            m1_u8g2_nextpage();
        }

        btn = link_get_button(10);
        if ( btn == BUTTON_BACK_KP_ID )
            break;
        else if ( btn == BUTTON_UP_KP_ID )   { sel--; redraw = true; }
        else if ( btn == BUTTON_DOWN_KP_ID ) { sel++; redraw = true; }
        else if ( btn == BUTTON_OK_KP_ID || btn == BUTTON_RIGHT_KP_ID )
        {
            if ( sel == 0 )
                link_conversation(M1_LINK_ID_BROADCAST, "Broadcast");
            else if ( sel == 1 )
                link_settings();
            else if ( sel - 2 < s_peer_count )
                link_peer_actions(s_peers[sel - 2].id, s_peers[sel - 2].name);
            /* returning from sub-screen — re-arm + full refresh */
            m1_link_rx_reset();
            m1_link_rx_arm();
            last_hello = 0;
            redraw = true;
        }
    }

    /* Leave the radio idle on exit. */
    radio_set_antenna_mode(RADIO_ANTENNA_MODE_ISOLATED);
}

#endif /* M1_APP_LINK_ENABLE */
