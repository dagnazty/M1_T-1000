/* See COPYING.txt for license details. */

/*
 * m1_badusb.c
 *
 * BadUSB — USB HID keyboard injection with DuckyScript parser.
 * Reads script files from SD card and types keystrokes via USB HID.
 */

/*************************** I N C L U D E S **********************************/
#include "app_freertos.h"
#include "cmsis_os.h"
#include "main.h"
#include "m1_lcd.h"
#include "m1_display.h"
#include "m1_file_browser.h"
#include "m1_usb_cdc_msc.h"
#include "m1_menu.h"
#include "m1_tasks.h"
#include "m1_system.h"
#include "m1_sdcard.h"
#include "m1_compile_cfg.h"
#include "m1_badusb.h"
#include "m1_badusb_fingerprint.h"
#include "m1_kb_layout.h"
#include "m1_settings.h"
#include "usbd_hid.h"
#include "m1_log_debug.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#ifdef M1_APP_BADUSB_ENABLE

/*************************** D E F I N E S ************************************/

#define M1_LOGDB_TAG  "BUSB"

/* Timing */
#define BADUSB_ENUM_TIMEOUT_MS    5000  /* Max wait for host to enumerate HID */
#define BADUSB_ENUM_POLL_MS       50    /* Poll interval for enumeration check */
#define BADUSB_KEY_PRESS_MS       6     /* Hold key down (must exceed bInterval=2ms) */
#define BADUSB_KEY_RELEASE_MS     6     /* Delay after release */
#define BADUSB_INTER_CHAR_MS      2     /* Between characters in STRING */
#define BADUSB_TX_WAIT_MS         20    /* Max wait for HID TX complete */

/***************************** V A R I A B L E S ******************************/

static badusb_state_t badusb_state;

/* USB HID report buffer: [modifier, reserved, key1..key6] */
static uint8_t hid_report[8];

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

static void badusb_wait_tx_idle(void);
void badusb_send_key(uint8_t modifier, uint8_t keycode);
static void badusb_release_all(void);
void badusb_type_char(char c);
void badusb_type_string(const char *str);
static bool badusb_parse_line(const char *line);
static uint8_t badusb_parse_key_name(const char *name);
static uint8_t badusb_parse_modifier(const char *name, const char **remainder);
static uint16_t badusb_count_lines(const char *buf, uint32_t len);
static void badusb_show_progress(const char *filename);
static bool badusb_check_abort(void);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/*============================================================================*/
/**
  * @brief  Show a crash breadcrumb on the LCD (survives until reboot splash)
  */
/*============================================================================*/
static void badusb_breadcrumb(uint8_t phase)
{
    char buf[24];
    snprintf(buf, sizeof(buf), "BadUSB Phase: %d", phase);
    m1_u8g2_firstpage();
    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
    u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
    m1_draw_text(&m1_u8g2, 2, 30, 124, buf, TEXT_ALIGN_CENTER);
    m1_u8g2_nextpage();
}

/*============================================================================*/
/**
  * @brief  Wait for previous HID report transfer to complete (IDLE state)
  */
/*============================================================================*/
static void badusb_wait_tx_idle(void)
{
    USBD_HID_HandleTypeDef *hhid = (USBD_HID_HandleTypeDef *)hUsbDeviceFS.pClassData;
    if (hhid == NULL) return;

    uint32_t start = osKernelGetTickCount();
    while (hhid->state != HID_IDLE)
    {
        if ((osKernelGetTickCount() - start) > BADUSB_TX_WAIT_MS)
            break;
        osDelay(1);
    }
}

/*============================================================================*/
/**
  * @brief  Send a key press (modifier + keycode), wait, then release
  */
/*============================================================================*/
void badusb_send_key(uint8_t modifier, uint8_t keycode)
{
    badusb_wait_tx_idle();
    memset(hid_report, 0, sizeof(hid_report));
    hid_report[0] = modifier;
    hid_report[2] = keycode;
    USBD_HID_SendReport(&hUsbDeviceFS, hid_report, sizeof(hid_report));
    osDelay(BADUSB_KEY_PRESS_MS);

    badusb_release_all();
    osDelay(BADUSB_KEY_RELEASE_MS);
}


/*============================================================================*/
/**
  * @brief  Release all keys (send empty report)
  */
/*============================================================================*/
static void badusb_release_all(void)
{
    badusb_wait_tx_idle();
    memset(hid_report, 0, sizeof(hid_report));
    USBD_HID_SendReport(&hUsbDeviceFS, hid_report, sizeof(hid_report));
}


/*============================================================================*/
/**
  * @brief  Type a single ASCII character via HID keyboard
  */
/*============================================================================*/
void badusb_type_char(char c)
{
    if (c < 0x20 || c > 0x7E)
    {
        if (c == '\n' || c == '\r')
        {
            badusb_send_key(0, KEY_ENTER);
        }
        else if (c == '\t')
        {
            badusb_send_key(0, KEY_TAB);
        }
        return;
    }

    uint8_t keycode, mod;
    if (kb_layout_lookup(c, &keycode, &mod))
    {
        badusb_send_key(mod, keycode);
    }
}


/*============================================================================*/
/**
  * @brief  Type a string character by character
  */
/*============================================================================*/
void badusb_type_string(const char *str)
{
    while (*str && badusb_state.running)
    {
        badusb_type_char(*str++);
        osDelay(BADUSB_INTER_CHAR_MS);
    }
}

/**
 * @brief  Type a string directly from an app.
 * Switches to HID, waits for enumeration, types string, and switches back to normal.
 */
void badusb_type_string_forced(const char *str)
{
    if (str == NULL || *str == '\0') return;

    /* If not already in HID mode, switch now */
    if (m1_usb_get_current_mode() != M1_USB_MODE_HID) {
        m1_usb_switch_to_hid();
        /* Phase 3: Wait for enumeration (same as badusb_execute_file) */
        uint32_t t0 = osKernelGetTickCount();
        while (hUsbDeviceFS.dev_state != USBD_STATE_CONFIGURED)
        {
            if ((osKernelGetTickCount() - t0) > 5000) break;
            osDelay(50);
        }
        /* Extra settle delay for OS to load HID drivers */
        osDelay(3000);
    }

    /* Set running flag for this direct call */
    badusb_state.running = 1;
    
    while (*str && badusb_state.running)
    {
        badusb_type_char(*str++);
        osDelay(BADUSB_INTER_CHAR_MS);
    }

    badusb_state.running = 0;
}


/*============================================================================*/
/**
  * @brief  Parse a named key to HID scancode
  * @retval HID keycode or KEY_NONE if not recognized
  */
/*============================================================================*/
static uint8_t badusb_parse_key_name(const char *name)
{
    if (strcmp(name, "ENTER") == 0 || strcmp(name, "RETURN") == 0)  return KEY_ENTER;
    if (strcmp(name, "TAB") == 0)           return KEY_TAB;
    if (strcmp(name, "ESCAPE") == 0 || strcmp(name, "ESC") == 0)    return KEY_ESCAPE;
    if (strcmp(name, "SPACE") == 0)         return KEY_SPACE;
    if (strcmp(name, "BACKSPACE") == 0)     return KEY_BACKSPACE;
    if (strcmp(name, "DELETE") == 0 || strcmp(name, "DEL") == 0)    return KEY_DELETE;
    if (strcmp(name, "INSERT") == 0)        return KEY_INSERT;
    if (strcmp(name, "HOME") == 0)          return KEY_HOME;
    if (strcmp(name, "END") == 0)           return KEY_END;
    if (strcmp(name, "PAGEUP") == 0)        return KEY_PAGEUP;
    if (strcmp(name, "PAGEDOWN") == 0)      return KEY_PAGEDOWN;
    if (strcmp(name, "UP") == 0 || strcmp(name, "UPARROW") == 0)    return KEY_UP;
    if (strcmp(name, "DOWN") == 0 || strcmp(name, "DOWNARROW") == 0) return KEY_DOWN;
    if (strcmp(name, "LEFT") == 0 || strcmp(name, "LEFTARROW") == 0) return KEY_LEFT;
    if (strcmp(name, "RIGHT") == 0 || strcmp(name, "RIGHTARROW") == 0) return KEY_RIGHT;
    if (strcmp(name, "CAPSLOCK") == 0)      return KEY_CAPSLOCK;
    if (strcmp(name, "NUMLOCK") == 0)       return KEY_NUMLOCK;
    if (strcmp(name, "SCROLLLOCK") == 0)    return KEY_SCROLLLOCK;
    if (strcmp(name, "PRINTSCREEN") == 0)   return KEY_PRINTSCREEN;
    if (strcmp(name, "PAUSE") == 0 || strcmp(name, "BREAK") == 0)   return KEY_PAUSE;
    if (strcmp(name, "MENU") == 0 || strcmp(name, "APP") == 0)      return KEY_MENU;
    if (strcmp(name, "F1") == 0)            return KEY_F1;
    if (strcmp(name, "F2") == 0)            return KEY_F2;
    if (strcmp(name, "F3") == 0)            return KEY_F3;
    if (strcmp(name, "F4") == 0)            return KEY_F4;
    if (strcmp(name, "F5") == 0)            return KEY_F5;
    if (strcmp(name, "F6") == 0)            return KEY_F6;
    if (strcmp(name, "F7") == 0)            return KEY_F7;
    if (strcmp(name, "F8") == 0)            return KEY_F8;
    if (strcmp(name, "F9") == 0)            return KEY_F9;
    if (strcmp(name, "F10") == 0)           return KEY_F10;
    if (strcmp(name, "F11") == 0)           return KEY_F11;
    if (strcmp(name, "F12") == 0)           return KEY_F12;

    /* Single printable character */
    if (strlen(name) == 1 && name[0] >= 0x20 && name[0] <= 0x7E)
    {
        uint8_t keycode, mod;
        if (kb_layout_lookup(name[0], &keycode, &mod))
            return keycode;
    }

    return KEY_NONE;
}


/*============================================================================*/
/**
  * @brief  Parse a modifier keyword and return its HID modifier bit
  * @param  name: input string (may contain "CTRL", "ALT", "SHIFT", "GUI", etc.)
  * @param  remainder: output pointer to the rest of the string after the modifier
  * @retval modifier bitmask or 0 if not a modifier
  */
/*============================================================================*/
static uint8_t badusb_parse_modifier(const char *name, const char **remainder)
{
    *remainder = NULL;

    /* Check for modifier with space or dash separator */
    struct {
        const char *keyword;
        uint8_t     mod;
    } modifiers[] = {
        {"CTRL",    HID_MOD_LCTRL},
        {"CONTROL", HID_MOD_LCTRL},
        {"ALT",     HID_MOD_LALT},
        {"SHIFT",   HID_MOD_LSHIFT},
        {"GUI",     HID_MOD_LGUI},
        {"WINDOWS", HID_MOD_LGUI},
        {"COMMAND", HID_MOD_LGUI},
    };

    for (int i = 0; i < (int)(sizeof(modifiers) / sizeof(modifiers[0])); i++)
    {
        size_t klen = strlen(modifiers[i].keyword);
        if (strncmp(name, modifiers[i].keyword, klen) == 0)
        {
            char sep = name[klen];
            if (sep == ' ' || sep == '-' || sep == '+' || sep == '\0')
            {
                if (sep != '\0')
                    *remainder = name + klen + 1;
                else
                    *remainder = NULL;
                return modifiers[i].mod;
            }
        }
    }

    return 0;
}


/*============================================================================*/
/**
  * @brief  Parse and execute a single DuckyScript line
  * @retval true if line was valid, false on error
  */
/*============================================================================*/
static bool badusb_parse_line(const char *line)
{
    /* Skip leading whitespace */
    while (*line == ' ' || *line == '\t') line++;

    /* Skip empty lines */
    if (*line == '\0' || *line == '\n' || *line == '\r')
        return true;

    /* REM — comment */
    if (strncmp(line, "REM ", 4) == 0 || strncmp(line, "REM\r", 4) == 0 ||
        strncmp(line, "REM\n", 4) == 0 || strcmp(line, "REM") == 0)
        return true;

    /* DELAY <ms> */
    if (strncmp(line, "DELAY ", 6) == 0)
    {
        uint32_t ms = (uint32_t)atoi(line + 6);
        if (ms > 0)
            osDelay(ms);
        return true;
    }

    /* DEFAULT_DELAY <ms> */
    if (strncmp(line, "DEFAULT_DELAY ", 14) == 0 ||
        strncmp(line, "DEFAULTDELAY ", 13) == 0)
    {
        const char *p = line + (line[7] == '_' ? 14 : 13);
        badusb_state.default_delay_ms = (uint16_t)atoi(p);
        return true;
    }

    /* STRING <text> */
    if (strncmp(line, "STRING ", 7) == 0)
    {
        badusb_type_string(line + 7);
        return true;
    }

    /* REPEAT <n> */
    if (strncmp(line, "REPEAT ", 7) == 0)
    {
        int count = atoi(line + 7);
        if (count > 0 && badusb_state.last_line[0] != '\0')
        {
            for (int i = 0; i < count && badusb_state.running; i++)
            {
                badusb_parse_line(badusb_state.last_line);
                if (badusb_state.default_delay_ms > 0)
                    osDelay(badusb_state.default_delay_ms);
            }
        }
        return true;  /* Don't update last_line for REPEAT */
    }

    /* Try as modifier combo: CTRL x, GUI r, ALT F4, SHIFT INSERT, etc. */
    {
        uint8_t mod_accum = 0;
        const char *cur = line;
        const char *rest = NULL;

        /* Accumulate all modifiers (supports CTRL-ALT x, CTRL SHIFT ESC, etc.) */
        while (cur && *cur)
        {
            uint8_t m = badusb_parse_modifier(cur, &rest);
            if (m == 0)
                break;
            mod_accum |= m;
            cur = rest;
        }

        if (mod_accum != 0)
        {
            if (cur != NULL && *cur != '\0')
            {
                /* There's a key after the modifier(s) */
                /* Remove trailing whitespace/newline */
                char keybuf[32];
                strncpy(keybuf, cur, sizeof(keybuf) - 1);
                keybuf[sizeof(keybuf) - 1] = '\0';
                /* Trim trailing whitespace */
                int kl = (int)strlen(keybuf);
                while (kl > 0 && (keybuf[kl-1] == '\r' || keybuf[kl-1] == '\n' ||
                       keybuf[kl-1] == ' '))
                    keybuf[--kl] = '\0';

                uint8_t keycode = badusb_parse_key_name(keybuf);
                if (keycode != KEY_NONE)
                {
                    /* Fold in any modifier the key itself needs on this layout
                     * (shift for uppercase, AltGr for symbols on non-US maps) */
                    if (strlen(keybuf) == 1 && keybuf[0] >= 0x20 && keybuf[0] <= 0x7E)
                    {
                        uint8_t char_key, char_mod;
                        if (kb_layout_lookup(keybuf[0], &char_key, &char_mod))
                            mod_accum |= char_mod;
                    }
                    badusb_send_key(mod_accum, keycode);
                }
                else
                {
                    /* Modifier only, no recognized key */
                    badusb_send_key(mod_accum, KEY_NONE);
                }
            }
            else
            {
                /* Modifier alone (e.g., just "GUI") */
                badusb_send_key(mod_accum, KEY_NONE);
            }
            return true;
        }
    }

    /* Try as standalone key name */
    {
        /* Trim trailing CR/LF */
        char keybuf[32];
        strncpy(keybuf, line, sizeof(keybuf) - 1);
        keybuf[sizeof(keybuf) - 1] = '\0';
        int kl = (int)strlen(keybuf);
        while (kl > 0 && (keybuf[kl-1] == '\r' || keybuf[kl-1] == '\n' ||
               keybuf[kl-1] == ' '))
            keybuf[--kl] = '\0';

        uint8_t keycode = badusb_parse_key_name(keybuf);
        if (keycode != KEY_NONE)
        {
            badusb_send_key(0, keycode);
            return true;
        }
    }

    M1_LOG_W(M1_LOGDB_TAG, "Unknown cmd: %s\r\n", line);
    return true;  /* Skip unrecognized lines rather than aborting */
}


/*============================================================================*/
/**
  * @brief  Count lines in a text buffer
  */
/*============================================================================*/
static uint16_t badusb_count_lines(const char *buf, uint32_t len)
{
    uint16_t count = 0;
    for (uint32_t i = 0; i < len; i++)
    {
        if (buf[i] == '\n')
            count++;
    }
    /* Count last line if no trailing newline */
    if (len > 0 && buf[len - 1] != '\n')
        count++;
    return count;
}


/*============================================================================*/
/**
  * @brief  Show execution progress on LCD
  */
/*============================================================================*/
static void badusb_show_progress(const char *filename)
{
    char buf[32];
    char name_buf[24];

    m1_u8g2_firstpage();
    strncpy(name_buf, filename, sizeof(name_buf) - 1);
    name_buf[sizeof(name_buf) - 1] = '\0';
    snprintf(buf, sizeof(buf), "Line %d / %d",
             badusb_state.current_line, badusb_state.total_lines);
    m1_draw_status_panel(&m1_u8g2, "BadUSB", "Run",
                      NULL, 0, 0,
                      name_buf,
                      buf,
                      badusb_state.running ? "Running..." : "Done");
    m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Stop", "", NULL);

    m1_u8g2_nextpage();
}


/*============================================================================*/
/**
  * @brief  Check if user pressed BACK to abort
  * @retval true if abort requested
  */
/*============================================================================*/
static bool badusb_check_abort(void)
{
    S_M1_Main_Q_t q_item;
    S_M1_Buttons_Status btn;

    /* Non-blocking check */
    if (xQueueReceive(main_q_hdl, &q_item, 0) == pdTRUE)
    {
        if (q_item.q_evt_type == Q_EVENT_KEYPAD)
        {
            if (xQueueReceive(button_events_q_hdl, &btn, 0) == pdTRUE)
            {
                if (btn.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK
                 || btn.event[BUTTON_LEFT_KP_ID] == BUTTON_EVENT_CLICK
                 || btn.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK
                 || btn.event[BUTTON_RIGHT_KP_ID] == BUTTON_EVENT_CLICK)
                {
                    return true;
                }
            }
        }
    }
    return false;
}


/*============================================================================*/
/**
  * @brief  Execute a BadUSB script file
  * @param  filepath: full path to script on SD card (e.g., "0:/BadUSB/test.txt")
  * @retval true on success, false on error
  */
/*============================================================================*/
bool badusb_execute_file(const char *filepath)
{
    FIL fp;
    FRESULT fres;
    UINT bytes_read;
    /* Static, not on-stack: this is ~4.3 KB and badusb is single-instance
     * (guarded by the global badusb_state). Keeping it off the stack lets
     * badusb_execute_file be safely called from deeper call chains such as
     * the M1 Link remote-trigger path, where a 4 KB stack frame would overflow
     * the caller's task stack and hang right after the Phase 1 breadcrumb. */
    static char script_buf[BADUSB_MAX_SCRIPT_SIZE];
    static char line_buf[BADUSB_MAX_LINE_LEN];

    /* Phase 1: File read */
    badusb_breadcrumb(1);
    fres = f_open(&fp, filepath, FA_READ);
    if (fres != FR_OK)
    {
        M1_LOG_E(M1_LOGDB_TAG, "Failed to open: %s (err=%d)\r\n", filepath, fres);
        return false;
    }

    /* Read entire file */
    fres = f_read(&fp, script_buf, BADUSB_MAX_SCRIPT_SIZE - 1, &bytes_read);
    f_close(&fp);

    if (fres != FR_OK || bytes_read == 0)
    {
        M1_LOG_E(M1_LOGDB_TAG, "Read failed (err=%d, bytes=%lu)\r\n", fres, (unsigned long)bytes_read);
        return false;
    }
    script_buf[bytes_read] = '\0';

    /* Init state */
    memset(&badusb_state, 0, sizeof(badusb_state));
    badusb_state.running = 1;
    badusb_state.total_lines = badusb_count_lines(script_buf, bytes_read);

    /* Extract just the filename for display */
    const char *fname = strrchr(filepath, '/');
    if (fname) fname++; else fname = filepath;

    /* Reset HID debug counters before switching */
    USBD_HID_ResetDbgCounters();

    /* Phase 2: USB switch to HID */
    badusb_breadcrumb(2);
    m1_usb_switch_to_hid();

    /* Phase 3: Wait for enumeration */
    badusb_breadcrumb(3);
    {
        uint32_t t0 = osKernelGetTickCount();
        while (hUsbDeviceFS.dev_state != USBD_STATE_CONFIGURED)
        {
            if ((osKernelGetTickCount() - t0) > BADUSB_ENUM_TIMEOUT_MS)
                break;
            osDelay(BADUSB_ENUM_POLL_MS);
        }

        if (hUsbDeviceFS.dev_state != USBD_STATE_CONFIGURED)
        {
            badusb_state.running = 0;
        }

        /* Phase 4: Settle delay (3s) */
        badusb_breadcrumb(4);
        osDelay(3000);

        /* Phase 5: Prime endpoint */
        badusb_breadcrumb(5);
        if (badusb_state.running)
        {
            memset(hid_report, 0, sizeof(hid_report));
            USBD_HID_SendReport(&hUsbDeviceFS, hid_report, sizeof(hid_report));
            osDelay(50);
        }
    }

    /* Phase 6: Execute script lines */
    badusb_breadcrumb(6);

    if (badusb_state.running)
    {
        const char *p = script_buf;
        const char *end = script_buf + bytes_read;

        while (p < end && badusb_state.running)
        {
            /* Extract one line */
            const char *line_end = p;
            while (line_end < end && *line_end != '\n')
                line_end++;

            size_t line_len = (size_t)(line_end - p);
            if (line_len >= BADUSB_MAX_LINE_LEN)
                line_len = BADUSB_MAX_LINE_LEN - 1;

            memcpy(line_buf, p, line_len);
            line_buf[line_len] = '\0';

            /* Trim trailing CR */
            if (line_len > 0 && line_buf[line_len - 1] == '\r')
                line_buf[line_len - 1] = '\0';

            badusb_state.current_line++;

            /* Execute the line */
            badusb_parse_line(line_buf);

            /* Save for REPEAT (unless it was a REPEAT command itself) */
            if (strncmp(line_buf, "REPEAT ", 7) != 0)
            {
                strncpy(badusb_state.last_line, line_buf,
                        sizeof(badusb_state.last_line) - 1);
                badusb_state.last_line[sizeof(badusb_state.last_line) - 1] = '\0';
            }

            /* Default delay between commands */
            if (badusb_state.default_delay_ms > 0)
                osDelay(badusb_state.default_delay_ms);

            /* Update progress display every 4 lines */
            if ((badusb_state.current_line & 3) == 0)
                badusb_show_progress(fname);

            /* Check for abort */
            if (badusb_check_abort())
            {
                badusb_state.running = 0;
                break;
            }

            /* Advance past newline */
            p = line_end;
            if (p < end && *p == '\n')
                p++;
        }
    }

    /* Ensure all keys released */
    badusb_release_all();

    /* Show final progress */
    badusb_state.running = 0;
    badusb_show_progress(fname);
    osDelay(500);

    /* Switch USB back to CDC+MSC */
    m1_usb_switch_to_normal();

    return true;
}


/*============================================================================*/
/**
  * @brief  Stop a running BadUSB script
  */
/*============================================================================*/
void badusb_stop(void)
{
    badusb_state.running = 0;
}


/*============================================================================*/
/**
  * @brief  BadUSB menu entry point — browse files, select, execute
  */
/*============================================================================*/
void badusb_run(void)
{
    S_M1_Buttons_Status this_button_status;
    S_M1_Main_Q_t q_item;
    S_M1_file_info *f_info;
    BaseType_t ret;
    char filepath[128];

    /* Check SD card */
    if (m1_sdcard_get_status() != SD_access_OK)
    {
        m1_message_box(&m1_u8g2, "BadUSB", "SD card not", "available", " OK ");
        return;
    }

    /* Ensure BadUSB directory exists */
    if (!m1_fb_check_existence(BADUSB_DIR))
    {
        m1_fb_make_dir(BADUSB_DIR);
    }

    /* Init file browser */
    m1_fb_init(&m1_u8g2);

    /* Setup display */
    m1_u8g2_firstpage();
    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
    u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
    m1_u8g2_nextpage();

    m1_fb_display(NULL);

    /* File browser event loop */
    while (1)
    {
        ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
        if (ret != pdTRUE)
            continue;

        if (q_item.q_evt_type != Q_EVENT_KEYPAD)
            continue;

        ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
        if (ret != pdTRUE)
            continue;

        /* BACK exits to menu */
        if (this_button_status.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK)
        {
            m1_fb_deinit();
            break;
        }

        /* Pass button events to file browser */
        f_info = m1_fb_display(&this_button_status);
        if (f_info->status == FB_OK && f_info->file_is_selected)
        {
            /* Build full file path */
            snprintf(filepath, sizeof(filepath), "%s/%s",
                     f_info->dir_name, f_info->file_name);

            /* Show confirmation */
            char msg_line[28];
            strncpy(msg_line, f_info->file_name, sizeof(msg_line) - 1);
            msg_line[sizeof(msg_line) - 1] = '\0';

            /* Confirmation screen */
            m1_u8g2_firstpage();
            m1_draw_status_panel(&m1_u8g2, "BadUSB", "Run",
                              NULL, 0, 0,
                              "Run selected script?",
                              msg_line,
                              "OK launch  BACK cancel");
            m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Back", "Run", arrowright_8x8);
            m1_u8g2_nextpage();

            /* Wait for OK or BACK */
            bool confirmed = false;
            while (1)
            {
                ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
                if (ret != pdTRUE) continue;
                if (q_item.q_evt_type != Q_EVENT_KEYPAD) continue;

                ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
                if (ret != pdTRUE) continue;

                if (this_button_status.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK ||
                    this_button_status.event[BUTTON_RIGHT_KP_ID] == BUTTON_EVENT_CLICK)
                {
                    confirmed = true;
                    break;
                }
                if (this_button_status.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK ||
                    this_button_status.event[BUTTON_LEFT_KP_ID] == BUTTON_EVENT_CLICK)
                {
                    break;
                }
            }

            if (confirmed)
            {
                /* Execute the script */
                bool ok = badusb_execute_file(filepath);

                /* Show result */
                if (ok)
                {
                    m1_message_box(&m1_u8g2, "BadUSB", "Script", "complete", " OK ");
                }
                else
                {
                    m1_message_box(&m1_u8g2, "BadUSB", "Script", "error", " OK ");
                }
            }

            /* Return to file browser */
            m1_fb_display(NULL);
        }
    }
}

/*============================================================================*/
/**
  * @brief  OS Detect — switch to HID, capture enumeration, analyze, show result
  */
/*============================================================================*/
void badusb_os_detect(void)
{
    S_M1_Buttons_Status btn;
    S_M1_Main_Q_t q_item;
    BaseType_t ret;

    /* Show "connecting" screen */
    m1_u8g2_firstpage();
    m1_draw_status_panel(&m1_u8g2, "BadUSB", "Detect",
                      NULL, 0, 0,
                      "Switching to HID",
                      "Waiting for host",
                      "BACK cancels");
    m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Back", "", NULL);
    m1_u8g2_nextpage();

    /* Switch USB to HID — this triggers USBD_HID_Init which starts capture */
    USBD_HID_ResetDbgCounters();
    m1_usb_switch_to_hid();

    /* Wait for enumeration */
    uint32_t t0 = osKernelGetTickCount();
    bool enumerated = false;

    while ((osKernelGetTickCount() - t0) < BADUSB_ENUM_TIMEOUT_MS)
    {
        if (hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED)
        {
            enumerated = true;
            break;
        }

        /* Check for abort */
        if (xQueueReceive(main_q_hdl, &q_item, 0) == pdTRUE)
        {
            if (q_item.q_evt_type == Q_EVENT_KEYPAD)
            {
                if (xQueueReceive(button_events_q_hdl, &btn, 0) == pdTRUE)
                {
                    if (btn.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK ||
                        btn.event[BUTTON_LEFT_KP_ID] == BUTTON_EVENT_CLICK)
                    {
                        m1_usb_switch_to_normal();
                        return;
                    }
                }
            }
        }

        osDelay(BADUSB_ENUM_POLL_MS);
    }

    if (!enumerated)
    {
        m1_usb_switch_to_normal();
        m1_message_box(&m1_u8g2, "OS Detect", "No host", "detected", " OK ");
        return;
    }

    /* Wait a bit for the host to send all its Setup requests */
    osDelay(2000);

    /* Analyze the captured fingerprint */
    badusb_os_t os = hid_fp_analyze();

    /* Switch back to normal USB before showing result */
    m1_usb_switch_to_normal();

    /* Display result */
    char detail[48];
    snprintf(detail, sizeof(detail), "Events: %d", g_hid_fingerprint.count);

    m1_u8g2_firstpage();
    m1_draw_status_panel(&m1_u8g2, "BadUSB", "Detect",
                      NULL, 0, 0,
                      "Detected host",
                      hid_fp_os_name(os),
                      detail);
    m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Back", "", NULL);
    m1_u8g2_nextpage();

    /* Wait for user to dismiss */
    while (1)
    {
        ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
        if (ret != pdTRUE) continue;
        if (q_item.q_evt_type != Q_EVENT_KEYPAD) continue;

        ret = xQueueReceive(button_events_q_hdl, &btn, 0);
        if (ret != pdTRUE) continue;

        if (btn.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK ||
            btn.event[BUTTON_LEFT_KP_ID] == BUTTON_EVENT_CLICK ||
            btn.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK)
        {
            break;
        }
    }
}


/*============================================================================*/
/**
  * @brief  Draw a simple list menu on screen (self-contained, no shared state)
  * @param  title: header text
  * @param  items: array of item strings
  * @param  count: number of items
  * @param  sel: currently selected index
  */
/*============================================================================*/
static void badusb_draw_list(const char *title, const char *items[],
                             uint8_t count, uint8_t sel)
{
    char badge[12];
    uint8_t y = 30;
    uint8_t max_visible = 2;
    uint8_t top = 0;

    snprintf(badge, sizeof(badge), "%u/%u", (unsigned)(sel + 1), (unsigned)count);

    m1_u8g2_firstpage();
    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
    m1_draw_header_bar(&m1_u8g2, title, badge);
    m1_draw_content_frame(&m1_u8g2, 2, 14, 124, 35);
    u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);

    if (sel >= max_visible)
        top = sel - max_visible + 1;
    if (top + max_visible > count)
    {
        if (count > max_visible)
            top = count - max_visible;
        else
            top = 0;
    }

    for (uint8_t i = top; i < count && (i - top) < max_visible; i++)
    {
        if (i == sel)
        {
            u8g2_DrawBox(&m1_u8g2, 6, y - 7, 114, 11);
            u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
            u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_B);
            m1_draw_text(&m1_u8g2, 10, y, 108, items[i], TEXT_ALIGN_LEFT);
            u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
            u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
        }
        else
        {
            u8g2_DrawFrame(&m1_u8g2, 6, y - 7, 114, 11);
            m1_draw_text(&m1_u8g2, 10, y, 108, items[i], TEXT_ALIGN_LEFT);
        }
        y += 12;
    }

    m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Back", "OK", arrowright_8x8);
    m1_u8g2_nextpage();
}


/*============================================================================*/
/**
  * @brief  Payload Library — browse categorized payloads on SD card
  *
  * Directory structure:
  *   0:/BadUSB/Payloads/Windows/
  *   0:/BadUSB/Payloads/MacOS/
  *   0:/BadUSB/Payloads/Linux/
  *   0:/BadUSB/Payloads/Universal/
  *
  * Each .txt script can have a .desc sidecar with a one-line description.
  */
/*============================================================================*/

#define PAYLOAD_BASE_DIR      "0:/BadUSB/Payloads"
#define PAYLOAD_NUM_CATS      4
#define PAYLOAD_DESC_MAXLEN   64
#define PAYLOAD_MAX_FILES     16
#define PAYLOAD_NAME_MAX      32

static const char *payload_categories[PAYLOAD_NUM_CATS] = {
    "Windows", "MacOS", "Linux", "Universal"
};

static const char *payload_cat_dirs[PAYLOAD_NUM_CATS] = {
    PAYLOAD_BASE_DIR "/Windows",
    PAYLOAD_BASE_DIR "/MacOS",
    PAYLOAD_BASE_DIR "/Linux",
    PAYLOAD_BASE_DIR "/Universal"
};

/* Ensure all payload directories exist (creates parents first) */
static void payload_ensure_dirs(void)
{
    f_mkdir(BADUSB_DIR);
    f_mkdir(PAYLOAD_BASE_DIR);
    for (int i = 0; i < PAYLOAD_NUM_CATS; i++)
        f_mkdir(payload_cat_dirs[i]);
}

/* Scan a directory for .txt files, return count.
   Names stored in names[], full paths in paths[]. */
static uint8_t payload_scan_dir(const char *dir,
                                char names[][PAYLOAD_NAME_MAX],
                                char paths[][128],
                                uint8_t max_files)
{
    DIR d;
    FILINFO fno;
    FRESULT res;
    uint8_t count = 0;

    res = f_opendir(&d, dir);
    if (res != FR_OK)
        return 0;

    while (count < max_files)
    {
        res = f_readdir(&d, &fno);
        if (res != FR_OK || fno.fname[0] == '\0')
            break;

        if (fno.fattrib & AM_DIR)
            continue;

        uint16_t nlen = (uint16_t)strlen(fno.fname);
        if (nlen < 5)
            continue;

        /* Check for .txt extension */
        const char *ext = &fno.fname[nlen - 4];
        if (strcmp(ext, ".txt") != 0 && strcmp(ext, ".TXT") != 0)
            continue;

        /* Store display name (without .txt) */
        uint16_t copy_len = nlen - 4;
        if (copy_len >= PAYLOAD_NAME_MAX)
            copy_len = PAYLOAD_NAME_MAX - 1;
        strncpy(names[count], fno.fname, copy_len);
        names[count][copy_len] = '\0';

        /* Store full path */
        snprintf(paths[count], 128, "%s/%s", dir, fno.fname);

        count++;
    }
    f_closedir(&d);
    return count;
}

/* Read a .desc sidecar file (replace .txt → .desc). Returns empty string on failure. */
static void payload_read_desc(const char *txt_path, char *desc_out, uint8_t max_len)
{
    char desc_path[132];
    desc_out[0] = '\0';

    strncpy(desc_path, txt_path, sizeof(desc_path) - 1);
    desc_path[sizeof(desc_path) - 1] = '\0';

    char *dot = strrchr(desc_path, '.');
    if (!dot)
        return;
    if (strcmp(dot, ".txt") != 0 && strcmp(dot, ".TXT") != 0)
        return;

    strcpy(dot, ".desc");

    FIL fp;
    if (f_open(&fp, desc_path, FA_READ) != FR_OK)
        return;

    UINT br;
    f_read(&fp, desc_out, max_len - 1, &br);
    f_close(&fp);
    desc_out[br] = '\0';

    char *nl = strchr(desc_out, '\n');
    if (nl) *nl = '\0';
    nl = strchr(desc_out, '\r');
    if (nl) *nl = '\0';
}

void badusb_payload_library(void)
{
    S_M1_Buttons_Status btn;
    S_M1_Main_Q_t q_item;
    BaseType_t ret;
    uint8_t sel = 0;

    /* Create directory structure (parents first) */
    payload_ensure_dirs();

    /* Drain stale events */
    while (xQueueReceive(main_q_hdl, &q_item, 0) == pdTRUE)
    {
        if (q_item.q_evt_type == Q_EVENT_KEYPAD)
            xQueueReceive(button_events_q_hdl, &btn, 0);
    }

    badusb_draw_list("Payloads", payload_categories, PAYLOAD_NUM_CATS, sel);

    while (1)
    {
        ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
        if (ret != pdTRUE) continue;
        if (q_item.q_evt_type != Q_EVENT_KEYPAD) continue;

        ret = xQueueReceive(button_events_q_hdl, &btn, 0);
        if (ret != pdTRUE) continue;

        if (btn.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK ||
            btn.event[BUTTON_LEFT_KP_ID] == BUTTON_EVENT_CLICK)
        {
            break;
        }
        else if (btn.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK ||
                 btn.event[BUTTON_RIGHT_KP_ID] == BUTTON_EVENT_CLICK)
        {
            /* Scan the selected category directory for .txt files */
            static char pl_names[PAYLOAD_MAX_FILES][PAYLOAD_NAME_MAX];
            static char pl_paths[PAYLOAD_MAX_FILES][128];
            uint8_t pl_count = payload_scan_dir(payload_cat_dirs[sel],
                                                 pl_names, pl_paths,
                                                 PAYLOAD_MAX_FILES);

            if (pl_count == 0)
            {
                m1_message_box(&m1_u8g2, payload_categories[sel],
                               "No payloads", "found", " OK ");
                badusb_draw_list("Payloads", payload_categories, PAYLOAD_NUM_CATS, sel);
                continue;
            }

            /* Build a string pointer array for badusb_draw_list */
            const char *pl_display[PAYLOAD_MAX_FILES];
            for (uint8_t i = 0; i < pl_count; i++)
                pl_display[i] = pl_names[i];

            uint8_t pl_sel = 0;
            badusb_draw_list(payload_categories[sel], pl_display, pl_count, pl_sel);

            bool in_cat = true;
            while (in_cat)
            {
                ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
                if (ret != pdTRUE) continue;
                if (q_item.q_evt_type != Q_EVENT_KEYPAD) continue;

                ret = xQueueReceive(button_events_q_hdl, &btn, 0);
                if (ret != pdTRUE) continue;

                if (btn.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK ||
                    btn.event[BUTTON_LEFT_KP_ID] == BUTTON_EVENT_CLICK)
                {
                    in_cat = false;
                }
                else if (btn.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK)
                {
                    if (pl_sel == 0) pl_sel = pl_count - 1; else pl_sel--;
                    badusb_draw_list(payload_categories[sel], pl_display, pl_count, pl_sel);
                }
                else if (btn.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK)
                {
                    pl_sel++;
                    if (pl_sel >= pl_count) pl_sel = 0;
                    badusb_draw_list(payload_categories[sel], pl_display, pl_count, pl_sel);
                }
                else if (btn.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK ||
                         btn.event[BUTTON_RIGHT_KP_ID] == BUTTON_EVENT_CLICK)
                {
                    /* Read .desc sidecar if available */
                    char desc_line[PAYLOAD_DESC_MAXLEN];
                    payload_read_desc(pl_paths[pl_sel], desc_line, PAYLOAD_DESC_MAXLEN);

                    /* Show payload info + confirm */
                    m1_u8g2_firstpage();
                    if (desc_line[0] != '\0')
                    {
                        char disp_desc[28];
                        strncpy(disp_desc, desc_line, sizeof(disp_desc) - 1);
                        disp_desc[sizeof(disp_desc) - 1] = '\0';
                        m1_draw_status_panel(&m1_u8g2, "BadUSB", "Payload",
                                          NULL, 0, 0,
                                          pl_names[pl_sel],
                                          disp_desc,
                                          "OK run  BACK cancel");
                    }
                    else
                    {
                        m1_draw_status_panel(&m1_u8g2, "BadUSB", "Payload",
                                          NULL, 0, 0,
                                          pl_names[pl_sel],
                                          "Run selected payload?",
                                          "OK run  BACK cancel");
                    }
                    m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Back", "Run", arrowright_8x8);
                    m1_u8g2_nextpage();

                    bool confirmed = false;
                    while (1)
                    {
                        ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
                        if (ret != pdTRUE) continue;
                        if (q_item.q_evt_type != Q_EVENT_KEYPAD) continue;

                        ret = xQueueReceive(button_events_q_hdl, &btn, 0);
                        if (ret != pdTRUE) continue;

                        if (btn.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK ||
                            btn.event[BUTTON_RIGHT_KP_ID] == BUTTON_EVENT_CLICK)
                        {
                            confirmed = true;
                            break;
                        }
                        if (btn.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK ||
                            btn.event[BUTTON_LEFT_KP_ID] == BUTTON_EVENT_CLICK)
                        {
                            break;
                        }
                    }

                    if (confirmed)
                    {
                        bool ok = badusb_execute_file(pl_paths[pl_sel]);
                        if (ok)
                            m1_message_box(&m1_u8g2, "Payload", "Script", "complete", " OK ");
                        else
                            m1_message_box(&m1_u8g2, "Payload", "Script", "error", " OK ");
                    }

                    /* Redraw file list */
                    badusb_draw_list(payload_categories[sel], pl_display, pl_count, pl_sel);
                }
            }

            /* Return to category picker */
            badusb_draw_list("Payloads", payload_categories, PAYLOAD_NUM_CATS, sel);
        }
        else if (btn.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK)
        {
            if (sel == 0) sel = PAYLOAD_NUM_CATS - 1; else sel--;
            badusb_draw_list("Payloads", payload_categories, PAYLOAD_NUM_CATS, sel);
        }
        else if (btn.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK)
        {
            sel++;
            if (sel >= PAYLOAD_NUM_CATS) sel = 0;
            badusb_draw_list("Payloads", payload_categories, PAYLOAD_NUM_CATS, sel);
        }
    }
}


/*============================================================================*/
/**
  * @brief  Keyboard layout picker
  * @note   The layout is a global setting — Bad-BT types with the same one.
  */
/*============================================================================*/
static void badusb_layout_menu(void)
{
    S_M1_Buttons_Status btn;
    S_M1_Main_Q_t q_item;
    S_M1_file_info *f_info;
    BaseType_t ret;
    char filepath[128];
    bool browsing = false;

    if (m1_sdcard_get_status() != SD_access_OK)
    {
        m1_message_box(&m1_u8g2, "Layout", "SD card not", "available", " OK ");
        return;
    }

    while (1)
    {
        if (!browsing)
        {
            m1_u8g2_firstpage();
            u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
            m1_draw_header_bar(&m1_u8g2, "Layout", "Kbd");
            m1_draw_content_frame(&m1_u8g2, 2, 14, 124, 35);
            u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
            m1_draw_text(&m1_u8g2, 8, 26, 114, kb_layout_name(), TEXT_ALIGN_LEFT);
            m1_draw_text(&m1_u8g2, 8, 38, 114, "DOWN = built-in US", TEXT_ALIGN_LEFT);
            m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Back", "Load", NULL);
            m1_u8g2_nextpage();
        }

        ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
        if (ret != pdTRUE) continue;
        if (q_item.q_evt_type != Q_EVENT_KEYPAD) continue;

        ret = xQueueReceive(button_events_q_hdl, &btn, 0);
        if (ret != pdTRUE) continue;

        if (!browsing)
        {
            if (btn.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK)
            {
                break;
            }
            else if (btn.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK)
            {
                kb_layout_reset();
                settings_save_to_sd();
                m1_message_box(&m1_u8g2, "Layout", "Built-in US", "selected", " OK ");
            }
            else if (btn.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK)
            {
                m1_fb_init(&m1_u8g2);
                m1_fb_set_dir(KB_LAYOUT_DIR);

                m1_u8g2_firstpage();
                u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
                u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
                m1_u8g2_nextpage();

                m1_fb_display(NULL);
                browsing = true;
            }
            continue;
        }

        /* File browser active */
        if (btn.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK)
        {
            m1_fb_deinit();
            browsing = false;
            continue;
        }

        f_info = m1_fb_display(&btn);
        if (f_info->status == FB_OK && f_info->file_is_selected)
        {
            snprintf(filepath, sizeof(filepath), "%s/%s",
                     f_info->dir_name, f_info->file_name);
            m1_fb_deinit();
            browsing = false;

            if (kb_layout_load(filepath))
            {
                settings_save_to_sd();
                m1_message_box(&m1_u8g2, "Layout loaded", kb_layout_name(),
                               NULL, " OK ");
            }
            else
            {
                m1_message_box(&m1_u8g2, "Invalid layout",
                               "Need a 256-byte", ".kl file", " OK ");
            }
        }
    }
}


/*============================================================================*/
/**
  * @brief  BadUSB main menu — 4-item submenu (self-contained display)
  */
/*============================================================================*/

#define BADUSB_MENU_COUNT  4

static const char *badusb_menu_items[BADUSB_MENU_COUNT] = {
    "Run Script", "Payload Library", "OS Detect", "Keyboard Layout"
};

void badusb_main_menu(void)
{
    S_M1_Buttons_Status btn;
    S_M1_Main_Q_t q_item;
    BaseType_t ret;
    uint8_t sel = 0;

    /* Drain stale events */
    while (xQueueReceive(main_q_hdl, &q_item, 0) == pdTRUE)
    {
        if (q_item.q_evt_type == Q_EVENT_KEYPAD)
            xQueueReceive(button_events_q_hdl, &btn, 0);
    }

    badusb_draw_list("BadUSB", badusb_menu_items, BADUSB_MENU_COUNT, sel);

    while (1)
    {
        ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
        if (ret != pdTRUE) continue;
        if (q_item.q_evt_type != Q_EVENT_KEYPAD) continue;

        ret = xQueueReceive(button_events_q_hdl, &btn, 0);
        if (ret != pdTRUE) continue;

        if (btn.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK ||
            btn.event[BUTTON_LEFT_KP_ID] == BUTTON_EVENT_CLICK)
        {
            break;
        }
        else if (btn.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK ||
                 btn.event[BUTTON_RIGHT_KP_ID] == BUTTON_EVENT_CLICK)
        {
            switch (sel)
            {
                case 0: badusb_run();             break;
                case 1: badusb_payload_library();  break;
                case 2: badusb_os_detect();        break;
                case 3: badusb_layout_menu();      break;
                default: break;
            }
            /* Redraw our menu after returning */
            badusb_draw_list("BadUSB", badusb_menu_items, BADUSB_MENU_COUNT, sel);
        }
        else if (btn.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK)
        {
            if (sel == 0) sel = BADUSB_MENU_COUNT - 1; else sel--;
            badusb_draw_list("BadUSB", badusb_menu_items, BADUSB_MENU_COUNT, sel);
        }
        else if (btn.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK)
        {
            sel++;
            if (sel >= BADUSB_MENU_COUNT) sel = 0;
            badusb_draw_list("BadUSB", badusb_menu_items, BADUSB_MENU_COUNT, sel);
        }
    }
}

#endif /* M1_APP_BADUSB_ENABLE */
