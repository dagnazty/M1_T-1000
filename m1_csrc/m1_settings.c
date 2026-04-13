/* See COPYING.txt for license details. */

/*
*
*  m1_settings.c
*
*  M1 RFID functions
*
* M1 Project
*
*/

/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "stm32h5xx_hal.h"
#include "main.h"
#include "m1_settings.h"
#include "m1_branding.h"
#include "m1_buzzer.h"
#include "m1_lcd.h"
#include "m1_lp5814.h"
#include "m1_display.h"
#include "ff.h"
#include "m1_log_debug.h"
#include "m1_fw_update_bl.h"
#include "m1_t1000_version.h"
#include "m1_system.h"
#include "m1_file_util.h"

/*************************** D E F I N E S ************************************/

#define SETTINGS_TAG              "SETT"
#define SETTINGS_FILE_PATH        "0:/System/settings.cfg"
#define SETTINGS_FILE_MAX_SIZE    512

#define SETTING_ABOUT_CHOICES_MAX		2 //5

#define ABOUT_BOX_Y_POS_ROW_1			10
#define ABOUT_BOX_Y_POS_ROW_2			20
#define ABOUT_BOX_Y_POS_ROW_3			30
#define ABOUT_BOX_Y_POS_ROW_4			40
#define ABOUT_BOX_Y_POS_ROW_5			50

/* LCD & Notifications menu items */
#define LCD_SETTINGS_ITEMS   5
#define LCD_SET_BRIGHTNESS   0
#define LCD_SET_BUZZER       1
#define LCD_SET_LED          2
#define LCD_SET_ORIENT       3
#define LCD_SET_SLEEP        4

//************************** S T R U C T U R E S *******************************

/***************************** V A R I A B L E S ******************************/

static const uint8_t s_brightness_values[] = { 0, 64, 128, 192, 255 };
static const char *s_brightness_text[] = { "Off", "Low", "Med", "High", "Max" };
static const char *s_orient_text[] = { "Normal", "Southpaw", "Remote" };
static const char *s_sleep_text[] = { "30s", "1 min", "5 min", "10 min", "15 min", "Never" };

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

void settings_system(void);
void settings_about(void);
static void settings_about_display_choice(uint8_t choice);
static void settings_apply_orientation(uint8_t orient);
static const char *settings_lcd_item_label(uint8_t item);
static const char *settings_lcd_item_value(uint8_t item);
static void settings_lcd_draw(uint8_t sel);
void settings_save_to_sd(void);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
/**
  * @brief  Apply screen orientation and sync m1_southpaw_mode
  */
/*============================================================================*/
static void settings_apply_orientation(uint8_t orient)
{
    m1_screen_orientation = orient;
    m1_southpaw_mode = (orient == M1_ORIENT_SOUTHPAW) ? 1 : 0;

    if (orient == M1_ORIENT_SOUTHPAW)
        u8g2_SetDisplayRotation(&m1_u8g2, U8G2_R0);
    else if (orient == M1_ORIENT_REMOTE)
        u8g2_SetDisplayRotation(&m1_u8g2, U8G2_R1);
    else
        u8g2_SetDisplayRotation(&m1_u8g2, U8G2_R2);
}

static const char *settings_lcd_item_label(uint8_t item)
{
    switch (item)
    {
        case LCD_SET_BRIGHTNESS: return "Brightness";
        case LCD_SET_BUZZER:     return "Buzzer";
        case LCD_SET_LED:        return "LED Notify";
        case LCD_SET_ORIENT:     return "Orientation";
        case LCD_SET_SLEEP:      return "Sleep After";
        default:                 return "";
    }
}

static const char *settings_lcd_item_value(uint8_t item)
{
    switch (item)
    {
        case LCD_SET_BRIGHTNESS: return s_brightness_text[m1_brightness_level];
        case LCD_SET_BUZZER:     return m1_buzzer_on ? "On" : "Off";
        case LCD_SET_LED:        return m1_led_notify_on ? "On" : "Off";
        case LCD_SET_ORIENT:     return s_orient_text[m1_screen_orientation];
        case LCD_SET_SLEEP:      return s_sleep_text[m1_sleep_timeout_idx];
        default:                 return "";
    }
}

static void settings_lcd_draw(uint8_t sel)
{
    char badge[12];
    uint8_t visible_start = 0;

    if (sel > 1 && LCD_SETTINGS_ITEMS > 3)
        visible_start = (sel - 1 > LCD_SETTINGS_ITEMS - 3) ? LCD_SETTINGS_ITEMS - 3 : sel - 1;

    snprintf(badge, sizeof(badge), "%u/%u", (unsigned)(sel + 1), (unsigned)LCD_SETTINGS_ITEMS);

    m1_u8g2_firstpage();
    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
    m1_draw_header_bar(&m1_u8g2, "Settings", badge);
    m1_draw_content_frame(&m1_u8g2, 2, 14, 124, 35);
    u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);

    for (uint8_t vi = 0; vi < 3 && (visible_start + vi) < LCD_SETTINGS_ITEMS; vi++)
    {
        uint8_t item = visible_start + vi;
        uint8_t y = 24 + vi * 10;
        const char *label = settings_lcd_item_label(item);
        const char *value = settings_lcd_item_value(item);

        if (item == sel)
        {
            u8g2_DrawBox(&m1_u8g2, 6, y - 7, 114, 9);
            u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
            u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_B);
            m1_draw_text(&m1_u8g2, 10, y, 64, label, TEXT_ALIGN_LEFT);
            m1_draw_text(&m1_u8g2, 78, y, 38, value, TEXT_ALIGN_RIGHT);
            u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
            u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
        }
        else
        {
            u8g2_DrawFrame(&m1_u8g2, 6, y - 7, 114, 9);
            m1_draw_text(&m1_u8g2, 10, y, 64, label, TEXT_ALIGN_LEFT);
            m1_draw_text(&m1_u8g2, 78, y, 38, value, TEXT_ALIGN_RIGHT);
        }
    }

    m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Back", "Change", arrowright_8x8);
    m1_u8g2_nextpage();
}


/*============================================================================*/
/**
  * @brief  LCD & Notifications settings — scrollable 5-item menu
  *         Brightness, Buzzer, LED Notify, Orientation, Sleep After
  */
/*============================================================================*/
void settings_lcd_and_notifications(void)
{
    S_M1_Buttons_Status this_button_status;
    S_M1_Main_Q_t q_item;
    BaseType_t ret;

    uint8_t sel = 0;
    uint8_t needs_redraw = 1;

    while (1)
    {
        if (needs_redraw)
        {
            needs_redraw = 0;
            settings_lcd_draw(sel);
        }

        ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
        if (ret != pdTRUE) continue;
        if (q_item.q_evt_type != Q_EVENT_KEYPAD) continue;

        ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
        if (ret != pdTRUE) continue;

        /* Back — save and exit */
        if (this_button_status.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK)
        {
            settings_save_to_sd();
            xQueueReset(main_q_hdl);
            break;
        }

        /* Up/Down — navigate */
        if (this_button_status.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK)
        {
            sel = (sel == 0) ? (LCD_SETTINGS_ITEMS - 1) : (sel - 1);
            needs_redraw = 1;
        }
        if (this_button_status.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK)
        {
            sel = (sel + 1) % LCD_SETTINGS_ITEMS;
            needs_redraw = 1;
        }

        /* Left — decrement selected setting */
        if (this_button_status.event[BUTTON_LEFT_KP_ID] == BUTTON_EVENT_CLICK)
        {
            if (sel == LCD_SET_BRIGHTNESS)
            {
                m1_brightness_level = (m1_brightness_level == 0) ? 4 : (m1_brightness_level - 1);
                lp5814_backlight_on(s_brightness_values[m1_brightness_level]);
            }
            else if (sel == LCD_SET_BUZZER)
                m1_buzzer_on = !m1_buzzer_on;
            else if (sel == LCD_SET_LED)
                m1_led_notify_on = !m1_led_notify_on;
            else if (sel == LCD_SET_ORIENT)
                settings_apply_orientation((m1_screen_orientation == 0) ? 2 : (m1_screen_orientation - 1));
            else if (sel == LCD_SET_SLEEP)
                m1_sleep_timeout_idx = (m1_sleep_timeout_idx == 0) ? 5 : (m1_sleep_timeout_idx - 1);
            needs_redraw = 1;
        }

        /* Right — increment selected setting */
        if (this_button_status.event[BUTTON_RIGHT_KP_ID] == BUTTON_EVENT_CLICK)
        {
            if (sel == LCD_SET_BRIGHTNESS)
            {
                m1_brightness_level = (m1_brightness_level >= 4) ? 0 : (m1_brightness_level + 1);
                lp5814_backlight_on(s_brightness_values[m1_brightness_level]);
            }
            else if (sel == LCD_SET_BUZZER)
            {
                m1_buzzer_on = !m1_buzzer_on;
                if (m1_buzzer_on) m1_buzzer_notification();
            }
            else if (sel == LCD_SET_LED)
                m1_led_notify_on = !m1_led_notify_on;
            else if (sel == LCD_SET_ORIENT)
                settings_apply_orientation((m1_screen_orientation + 1) % 3);
            else if (sel == LCD_SET_SLEEP)
                m1_sleep_timeout_idx = (m1_sleep_timeout_idx >= 5) ? 0 : (m1_sleep_timeout_idx + 1);
            needs_redraw = 1;
        }
    }
}



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void settings_buzzer(void)
{
	//buzzer_demo_play();
} // void settings_sound(void)



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void settings_power(void)
{
	;
} // void settings_power(void)



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static void settings_system_draw(void)
{
    char detail[32];

    snprintf(detail, sizeof(detail), "ESP32 at boot: %s", m1_esp32_auto_init ? "ON" : "OFF");
    m1_u8g2_firstpage();
    m1_draw_status_panel(&m1_u8g2, "Settings", "System",
                      NULL, 0, 0,
                      detail,
                      "Controls WiFi/BT auto init",
                      "Toggle with OK or LEFT/RIGHT");
    m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Back", "Toggle", arrowright_8x8);
    m1_u8g2_nextpage();
}

void settings_system(void)
{
    S_M1_Buttons_Status this_button_status;
    S_M1_Main_Q_t q_item;
    BaseType_t ret;

    settings_system_draw();

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

        if (this_button_status.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK)
        {
            xQueueReset(main_q_hdl);
            break;
        }
        else if (this_button_status.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK ||
                 this_button_status.event[BUTTON_RIGHT_KP_ID] == BUTTON_EVENT_CLICK ||
                 this_button_status.event[BUTTON_LEFT_KP_ID] == BUTTON_EVENT_CLICK)
        {
            m1_esp32_auto_init = m1_esp32_auto_init ? 0 : 1;
            settings_save_to_sd();
            settings_system_draw();
        }
    }
}



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void settings_about(void)
{
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	uint8_t choice;

	choice = 0;
	settings_about_display_choice(choice);

	while (1 ) // Main loop of this task
	{
		;
		; // Do other parts of this task here
		;

		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
		if (ret==pdTRUE)
		{
			if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
			{
				// Notification is only sent to this task when there's any button activity,
				// so it doesn't need to wait when reading the event from the queue
				ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
				if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK ) // user wants to exit?
				{
					; // Do extra tasks here if needed
					xQueueReset(main_q_hdl); // Reset main q before return
					break; // Exit and return to the calling task (subfunc_handler_task)
				} // if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK )
				else if ( this_button_status.event[BUTTON_LEFT_KP_ID]==BUTTON_EVENT_CLICK ) // Previous?
				{
					choice--;
					if ( choice > SETTING_ABOUT_CHOICES_MAX )
						choice = SETTING_ABOUT_CHOICES_MAX;
					settings_about_display_choice(choice);
				} // else if ( this_button_status.event[BUTTON_LEFT_KP_ID]==BUTTON_EVENT_CLICK )
				else if ( this_button_status.event[BUTTON_RIGHT_KP_ID]==BUTTON_EVENT_CLICK ) // Next?
				{
					choice++;
					if ( choice > SETTING_ABOUT_CHOICES_MAX )
						choice = 0;
					settings_about_display_choice(choice);
				} // else if ( this_button_status.event[BUTTON_RIGHT_KP_ID]==BUTTON_EVENT_CLICK )
			} // if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
			else
			{
				; // Do other things for this task
			}
		} // if (ret==pdTRUE)
	} // while (1 ) // Main loop of this task

} // void settings_about(void)



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static void settings_about_display_choice(uint8_t choice)
{
	char badge[8];
	char prn_name[24];

	snprintf(badge, sizeof(badge), "%u/%u", (unsigned)(choice + 1), (unsigned)(SETTING_ABOUT_CHOICES_MAX + 1));

	m1_u8g2_firstpage();
	m1_draw_header_bar(&m1_u8g2, "Settings", badge);
	m1_draw_content_frame(&m1_u8g2, 2, 14, 124, 35);

	switch (choice)
	{
		case 0: // FW info
			u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_B);
			m1_draw_text(&m1_u8g2, 8, 24, 114, M1_PRODUCT_NAME, TEXT_ALIGN_LEFT);
			u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
			m1_draw_text(&m1_u8g2, 8, 33, 114, T1000_VERSION_STRING, TEXT_ALIGN_LEFT);
			sprintf(prn_name, "Active bank: %d", (m1_device_stat.active_bank==BANK1_ACTIVE)?1:2);
			m1_draw_text(&m1_u8g2, 8, 42, 114, prn_name, TEXT_ALIGN_LEFT);
			m1_draw_text(&m1_u8g2, 8, 51, 114, T1000_COMPAT_VERSION_STRING, TEXT_ALIGN_LEFT);
			break;

		case 1: // Company info
			u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_B);
			m1_draw_text(&m1_u8g2, 8, 24, 114, "MonstaTek Inc.", TEXT_ALIGN_LEFT);
			u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
			m1_draw_text(&m1_u8g2, 8, 34, 114, "San Jose, CA, USA", TEXT_ALIGN_LEFT);
			m1_draw_text(&m1_u8g2, 8, 44, 114, "Base firmware lineage", TEXT_ALIGN_LEFT);
			break;

		default:
			u8g2_DrawXBMP(&m1_u8g2, 23, 16, 82, 36, m1_device_82x36);
			break;
	} // switch (choice)

	m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Prev", "Next", arrowright_8x8);
	m1_u8g2_nextpage(); // Update display RAM
} // static void settings_about_display_choice(uint8_t choice)


/*============================================================================*/
/**
  * @brief  Save user settings to SD card (0:/System/settings.cfg)
  */
/*============================================================================*/
void settings_save_to_sd(void)
{
    FIL fp;
    FRESULT fres;
    UINT bw;
    char buf[64];

    /* Ensure System directory exists */
    f_mkdir("0:/System");

    fres = f_open(&fp, SETTINGS_FILE_PATH, FA_WRITE | FA_CREATE_ALWAYS);
    if (fres != FR_OK)
    {
        M1_LOG_W(SETTINGS_TAG, "Save failed (err=%d)\r\n", fres);
        return;
    }

    snprintf(buf, sizeof(buf), "brightness=%d\n", m1_brightness_level);
    f_write(&fp, buf, strlen(buf), &bw);

    snprintf(buf, sizeof(buf), "buzzer=%d\n", m1_buzzer_on);
    f_write(&fp, buf, strlen(buf), &bw);

    snprintf(buf, sizeof(buf), "led_notify=%d\n", m1_led_notify_on);
    f_write(&fp, buf, strlen(buf), &bw);

    snprintf(buf, sizeof(buf), "orientation=%d\n", m1_screen_orientation);
    f_write(&fp, buf, strlen(buf), &bw);

    snprintf(buf, sizeof(buf), "sleep_timeout=%d\n", m1_sleep_timeout_idx);
    f_write(&fp, buf, strlen(buf), &bw);

    snprintf(buf, sizeof(buf), "esp32_auto_init=%d\n", m1_esp32_auto_init);
    f_write(&fp, buf, strlen(buf), &bw);

    snprintf(buf, sizeof(buf), "ism_region=%d\n", m1_device_stat.config.ism_band_region);
    f_write(&fp, buf, strlen(buf), &bw);

#ifdef M1_APP_BADBT_ENABLE
    snprintf(buf, sizeof(buf), "badbt_name=%s\n", m1_badbt_name);
    f_write(&fp, buf, strlen(buf), &bw);
#endif

    f_close(&fp);
}


/*============================================================================*/
/**
  * @brief  Load user settings from SD card (0:/System/settings.cfg)
  *         Sets m1_southpaw_mode and applies display rotation.
  */
/*============================================================================*/
void settings_load_from_sd(void)
{
    FIL fp;
    FRESULT fres;
    UINT br;
    char buf[SETTINGS_FILE_MAX_SIZE];
    char *p;
    int val;

    fres = f_open(&fp, SETTINGS_FILE_PATH, FA_READ);
    if (fres != FR_OK)
        goto apply;  /* No settings file yet — apply defaults */

    fres = f_read(&fp, buf, sizeof(buf) - 1, &br);
    f_close(&fp);

    if (fres != FR_OK || br == 0)
        goto apply;

    buf[br] = '\0';

    /* Parse "brightness=X" */
    p = strstr(buf, "brightness=");
    if (p != NULL)
    {
        val = (int)(*(p + 11) - '0');
        if (val >= 0 && val <= 4)
            m1_brightness_level = (uint8_t)val;
    }

    /* Parse "buzzer=X" */
    p = strstr(buf, "buzzer=");
    if (p != NULL)
    {
        val = (int)(*(p + 7) - '0');
        if (val == 0 || val == 1)
            m1_buzzer_on = (uint8_t)val;
    }

    /* Parse "led_notify=X" */
    p = strstr(buf, "led_notify=");
    if (p != NULL)
    {
        val = (int)(*(p + 11) - '0');
        if (val == 0 || val == 1)
            m1_led_notify_on = (uint8_t)val;
    }

    /* Parse "orientation=X" */
    p = strstr(buf, "orientation=");
    if (p != NULL)
    {
        val = (int)(*(p + 12) - '0');
        if (val >= 0 && val <= 2)
            m1_screen_orientation = (uint8_t)val;
    }

    /* Parse "sleep_timeout=X" */
    p = strstr(buf, "sleep_timeout=");
    if (p != NULL)
    {
        val = (int)(*(p + 14) - '0');
        if (val >= 0 && val <= 5)
            m1_sleep_timeout_idx = (uint8_t)val;
    }

    /* Parse "esp32_auto_init=X" */
    p = strstr(buf, "esp32_auto_init=");
    if (p != NULL)
    {
        val = (int)(*(p + 16) - '0');
        if (val == 0 || val == 1)
            m1_esp32_auto_init = (uint8_t)val;
    }

    /* Parse "ism_region=X" */
    p = strstr(buf, "ism_region=");
    if (p != NULL)
    {
        val = (int)(*(p + 11) - '0');
        if (val >= 0 && val <= 3)
            m1_device_stat.config.ism_band_region = (uint8_t)val;
    }

    /* Legacy: migrate "southpaw=1" if no orientation key found */
    if (strstr(buf, "orientation=") == NULL)
    {
        p = strstr(buf, "southpaw=");
        if (p != NULL && *(p + 9) == '1')
            m1_screen_orientation = M1_ORIENT_SOUTHPAW;
    }

#ifdef M1_APP_BADBT_ENABLE
    /* Parse "badbt_name=XYZ" */
    p = strstr(buf, "badbt_name=");
    if (p != NULL)
    {
        p += 11;  /* skip "badbt_name=" */
        char *end = strchr(p, '\n');
        if (!end) end = p + strlen(p);
        uint8_t len = end - p;
        if (len > BADBT_NAME_MAX_LEN) len = BADBT_NAME_MAX_LEN;
        if (len > 0)
        {
            memcpy(m1_badbt_name, p, len);
            m1_badbt_name[len] = '\0';
        }
    }
#endif

apply:
    /* Apply brightness */
    lp5814_backlight_on(s_brightness_values[m1_brightness_level]);

    /* Apply orientation */
    settings_apply_orientation(m1_screen_orientation);
}


/*============================================================================*/
/**
  * @brief  Ensure all module SD card directories exist
  *         Call once after SD card is mounted (e.g. after settings_load_from_sd)
  */
/*============================================================================*/
void settings_ensure_sd_folders(void)
{
    static const char * const dirs[] = {
        "0:/NFC",
        "0:/RFID",
        "0:/SUBGHZ",
        "0:/IR",
        "0:/BadUSB",
        "0:/BT",
        "0:/System",
        "0:/apps"
    };

    for (uint8_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++)
    {
        fs_directory_ensure(dirs[i]);
    }
}
