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
#include <stdlib.h>
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
#include "m1_rgb_backlight.h"
#include "m1_builtin_apps.h"
#include "m1_kb_layout.h"

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

/* LCD & Notifications menu items (backlight moved to System > Backlight) */
#define LCD_SETTINGS_ITEMS   5
#define LCD_SET_BUZZER       0
#define LCD_SET_LED          1
#define LCD_SET_ORIENT       2
#define LCD_SET_SLEEP        3
#define LCD_SET_TIMEZONE     4

//************************** S T R U C T U R E S *******************************

/***************************** V A R I A B L E S ******************************/

static const uint8_t s_brightness_values[] = { 0, 64, 128, 192, 255 };
static const char *s_brightness_text[] = { "Off", "Low", "Med", "High", "Max" };
static const char *s_orient_text[] = { "Normal", "Southpaw", "Remote" };
static const char *s_sleep_text[] = { "30s", "1 min", "5 min", "10 min", "15 min", "Never" };
static const char *s_bl_type_text[] = { "Stock", "RGB" };

/* Backlight type: 0=Stock (LP5814), 1=RGB (SK6805 mod) — saved to settings.cfg */
uint8_t m1_backlight_type = 0;

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
        case LCD_SET_BUZZER:     return "Buzzer";
        case LCD_SET_LED:        return "LED Notify";
        case LCD_SET_ORIENT:     return "Orientation";
        case LCD_SET_SLEEP:      return "Sleep After";
        case LCD_SET_TIMEZONE:    return "UTC Offset";
        default:                 return "";
    }
}

static const char *settings_lcd_item_value(uint8_t item)
{
    switch (item)
    {
        case LCD_SET_BUZZER:     return m1_buzzer_on ? "On" : "Off";
        case LCD_SET_LED:        return m1_led_notify_on ? "On" : "Off";
        case LCD_SET_ORIENT:     return s_orient_text[m1_screen_orientation];
        case LCD_SET_SLEEP:      return s_sleep_text[m1_sleep_timeout_idx];
        case LCD_SET_TIMEZONE:
        {
            static char tz_buf[8];
            if (m1_tz_offset_hours >= 0)
                snprintf(tz_buf, sizeof(tz_buf), "UTC+%d", m1_tz_offset_hours);
            else
                snprintf(tz_buf, sizeof(tz_buf), "UTC%d", m1_tz_offset_hours);
            return tz_buf;
        }
        default:                 return "";
    }
}

static void settings_lcd_draw(uint8_t sel)
{
    char badge[12];
    uint8_t visible_start = 0;

    if (sel >= 2U && LCD_SETTINGS_ITEMS > 2U)
        visible_start = (uint8_t)(sel - 1U);

    snprintf(badge, sizeof(badge), "%u/%u", (unsigned)(sel + 1), (unsigned)LCD_SETTINGS_ITEMS);

    m1_u8g2_firstpage();
    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
    m1_draw_header_bar(&m1_u8g2, "Settings", badge);
    m1_draw_content_frame(&m1_u8g2, 2, 14, 124, 35);
    u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);

    for (uint8_t vi = 0; vi < 2U && (visible_start + vi) < LCD_SETTINGS_ITEMS; vi++)
    {
        uint8_t item = visible_start + vi;
        uint8_t y = 30 + vi * 12;
        const char *label = settings_lcd_item_label(item);
        const char *value = settings_lcd_item_value(item);

        if (item == sel)
        {
            u8g2_DrawBox(&m1_u8g2, 6, y - 7, 114, 11);
            u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
            u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_B);
            m1_draw_text(&m1_u8g2, 10, y, 64, label, TEXT_ALIGN_LEFT);
            m1_draw_text(&m1_u8g2, 78, y, 38, value, TEXT_ALIGN_RIGHT);
            u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
            u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
        }
        else
        {
            u8g2_DrawFrame(&m1_u8g2, 6, y - 7, 114, 11);
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
            if (sel == LCD_SET_BUZZER)
                m1_buzzer_on = !m1_buzzer_on;
            else if (sel == LCD_SET_LED)
                m1_led_notify_on = !m1_led_notify_on;
            else if (sel == LCD_SET_ORIENT)
                settings_apply_orientation((m1_screen_orientation == 0) ? 2 : (m1_screen_orientation - 1));
            else if (sel == LCD_SET_SLEEP)
                m1_sleep_timeout_idx = (m1_sleep_timeout_idx == 0) ? 5 : (m1_sleep_timeout_idx - 1);
            else if (sel == LCD_SET_TIMEZONE)
                m1_tz_offset_hours = (m1_tz_offset_hours <= -12) ? 14 : (m1_tz_offset_hours - 1);
            needs_redraw = 1;
        }

        /* Right — increment selected setting */
        if (this_button_status.event[BUTTON_RIGHT_KP_ID] == BUTTON_EVENT_CLICK)
        {
            if (sel == LCD_SET_BUZZER)
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
            else if (sel == LCD_SET_TIMEZONE)
                m1_tz_offset_hours = (m1_tz_offset_hours >= 14) ? -12 : (m1_tz_offset_hours + 1);
            needs_redraw = 1;
        }

        /* OK — no special action in LCD & Notifications */
    } /* while (1) */
} /* settings_lcd_and_notifications */


/*============================================================================*/
/**
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

/*============================================================================*/
/**
  * @brief  Manual date/time set screen.
  *         Fields: Year Month Day Hour Min Sec
  *         Left/Right = move field, Up/Down = increment/decrement, OK = save, BACK = cancel
  */
/*============================================================================*/
static void settings_set_time_draw(const m1_time_t *dt, uint8_t field)
{
    char line[32];

    m1_u8g2_firstpage();
    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
    m1_draw_header_bar(&m1_u8g2, "Set Time", "OK=Save");

    /* Date line: YYYY-MM-DD */
    snprintf(line, sizeof(line), "%04u-%02u-%02u",
             (unsigned)dt->year, (unsigned)dt->month, (unsigned)dt->day);
    u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
    m1_draw_text(&m1_u8g2, 8, 28, 114, line, TEXT_ALIGN_LEFT);

    /* Time line: HH:MM:SS */
    snprintf(line, sizeof(line), "%02u:%02u:%02u",
             (unsigned)dt->hour, (unsigned)dt->minute, (unsigned)dt->second);
    u8g2_SetFont(&m1_u8g2, M1_DISP_LARGE_FONT_2B);
    m1_draw_text(&m1_u8g2, 8, 44, 114, line, TEXT_ALIGN_LEFT);

    /* Cursor underline for active field:
     * Fields 0-2 are in the date line (FUNC font ~6px wide per char)
     * Fields 3-5 are in the time line (LARGE font ~12px wide per char) */
    static const uint8_t date_field_x[] = { 8, 38, 56 };   /* Y M D start x */
    static const uint8_t time_field_x[] = { 8, 44, 80 };   /* H M S start x */

    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
    if (field < 3U)
    {
        /* underline 2-char date segment */
        u8g2_DrawHLine(&m1_u8g2, date_field_x[field], 30, 12);
    }
    else
    {
        /* underline 2-char time segment */
        u8g2_DrawHLine(&m1_u8g2, time_field_x[field - 3U], 46, 23);
    }

    m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Cancel", "Field", arrowright_8x8);
    m1_u8g2_nextpage();
}

static uint8_t settings_dim(uint16_t year, uint8_t month)
{
    static const uint8_t mdays[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (month < 1U || month > 12U) return 30U;
    uint8_t d = mdays[month - 1U];
    if (month == 2U && year % 4U == 0U && (year % 100U != 0U || year % 400U == 0U)) d = 29U;
    return d;
}

void settings_set_time(void)
{
    S_M1_Buttons_Status this_button_status;
    S_M1_Main_Q_t q_item;
    BaseType_t ret;
    m1_time_t dt;
    uint8_t field = 0;   /* 0=Year 1=Month 2=Day 3=Hour 4=Min 5=Sec */

    m1_get_datetime(&dt);
    /* Clamp to sane defaults if RTC is uninitialised */
    if (dt.year < 2020U || dt.year > 2099U) dt.year  = 2025U;
    if (dt.month < 1U  || dt.month > 12U)  dt.month  = 1U;
    if (dt.day   < 1U  || dt.day   > 31U)  dt.day    = 1U;
    if (dt.hour  > 23U)                     dt.hour   = 0U;
    if (dt.minute > 59U)                    dt.minute = 0U;
    if (dt.second > 59U)                    dt.second = 0U;
    dt.weekday = 1U;  /* will be ignored by RTC; just a sane value */

    settings_set_time_draw(&dt, field);

    while (1)
    {
        ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
        if (ret != pdTRUE) continue;
        if (q_item.q_evt_type != Q_EVENT_KEYPAD) continue;

        ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
        if (ret != pdTRUE) continue;

        if (this_button_status.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK)
        {
            xQueueReset(main_q_hdl);
            return;  /* cancel */
        }

        /* Left/Right — move field */
        if (this_button_status.event[BUTTON_LEFT_KP_ID] == BUTTON_EVENT_CLICK)
            field = (field == 0U) ? 5U : (uint8_t)(field - 1U);
        if (this_button_status.event[BUTTON_RIGHT_KP_ID] == BUTTON_EVENT_CLICK)
            field = (field + 1U) % 6U;

        /* Up — increment */
        if (this_button_status.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK)
        {
            switch (field)
            {
                case 0: dt.year   = (dt.year   >= 2099U) ? 2020U : (uint16_t)(dt.year + 1U);  break;
                case 1: dt.month  = (dt.month  >= 12U)   ? 1U    : (uint8_t)(dt.month + 1U);  break;
                case 2: dt.day    = (dt.day    >= settings_dim(dt.year, dt.month)) ? 1U : (uint8_t)(dt.day + 1U); break;
                case 3: dt.hour   = (dt.hour   >= 23U)   ? 0U    : (uint8_t)(dt.hour + 1U);   break;
                case 4: dt.minute = (dt.minute >= 59U)   ? 0U    : (uint8_t)(dt.minute + 1U); break;
                case 5: dt.second = (dt.second >= 59U)   ? 0U    : (uint8_t)(dt.second + 1U); break;
                default: break;
            }
        }

        /* Down — decrement */
        if (this_button_status.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK)
        {
            switch (field)
            {
                case 0: dt.year   = (dt.year   <= 2020U) ? 2099U : (uint16_t)(dt.year - 1U);  break;
                case 1: dt.month  = (dt.month  <= 1U)    ? 12U   : (uint8_t)(dt.month - 1U);  break;
                case 2: dt.day    = (dt.day    <= 1U)    ? settings_dim(dt.year, dt.month) : (uint8_t)(dt.day - 1U); break;
                case 3: dt.hour   = (dt.hour   == 0U)    ? 23U   : (uint8_t)(dt.hour - 1U);   break;
                case 4: dt.minute = (dt.minute == 0U)    ? 59U   : (uint8_t)(dt.minute - 1U); break;
                case 5: dt.second = (dt.second == 0U)    ? 59U   : (uint8_t)(dt.second - 1U); break;
                default: break;
            }
        }

        /* Clamp day after month/year change */
        {
            uint8_t max_day = settings_dim(dt.year, dt.month);
            if (dt.day > max_day) dt.day = max_day;
        }

        /* OK — commit and save */
        if (this_button_status.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK)
        {
            m1_set_datetime(&dt);
            m1_buzzer_notification();
            xQueueReset(main_q_hdl);
            return;
        }

        settings_set_time_draw(&dt, field);
    }
}


/*============================================================================*/
/**
  * @brief  System settings — two-item menu: ESP32 auto-init + Set Time
  */
/*============================================================================*/

#define SYS_MENU_ITEMS   2
#define SYS_ITEM_ESP32   0
#define SYS_ITEM_TIME    1

static void settings_system_draw(uint8_t sel)
{
    const char *label0 = "ESP32 at boot";
    const char *val0   = m1_esp32_auto_init ? "ON" : "OFF";
    const char *label1 = "Set Time";
    const char *val1   = ">";

    char badge[8];
    snprintf(badge, sizeof(badge), "%u/%u", (unsigned)(sel + 1U), (unsigned)SYS_MENU_ITEMS);

    m1_u8g2_firstpage();
    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
    m1_draw_header_bar(&m1_u8g2, "Settings", badge);
    m1_draw_content_frame(&m1_u8g2, 2, 14, 124, 35);
    u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);

    struct { const char *lbl; const char *val; } items[SYS_MENU_ITEMS] = {
        { label0, val0 },
        { label1, val1 }
    };

    uint8_t visible_start = (sel >= 2U) ? (uint8_t)(sel - 1U) : 0U;
    for (uint8_t vi = 0; vi < 2U && (visible_start + vi) < SYS_MENU_ITEMS; vi++)
    {
        uint8_t item = visible_start + vi;
        uint8_t y    = 30U + vi * 12U;
        if (item == sel)
        {
            u8g2_DrawBox(&m1_u8g2, 6, y - 7, 114, 11);
            u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
            u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_B);
            m1_draw_text(&m1_u8g2, 10, y, 70, items[item].lbl, TEXT_ALIGN_LEFT);
            m1_draw_text(&m1_u8g2, 82, y, 34, items[item].val, TEXT_ALIGN_RIGHT);
            u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
            u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
        }
        else
        {
            u8g2_DrawFrame(&m1_u8g2, 6, y - 7, 114, 11);
            m1_draw_text(&m1_u8g2, 10, y, 70, items[item].lbl, TEXT_ALIGN_LEFT);
            m1_draw_text(&m1_u8g2, 82, y, 34, items[item].val, TEXT_ALIGN_RIGHT);
        }
    }

    m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Back", "Select", arrowright_8x8);
    m1_u8g2_nextpage();
}

void settings_system(void)
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
            settings_system_draw(sel);
        }

        ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
        if (ret != pdTRUE) continue;
        if (q_item.q_evt_type != Q_EVENT_KEYPAD) continue;

        ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
        if (ret != pdTRUE) continue;

        if (this_button_status.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK)
        {
            settings_save_to_sd();
            xQueueReset(main_q_hdl);
            break;
        }

        if (this_button_status.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK)
        {
            sel = (sel == 0U) ? (SYS_MENU_ITEMS - 1U) : (uint8_t)(sel - 1U);
            needs_redraw = 1;
        }
        if (this_button_status.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK)
        {
            sel = (uint8_t)((sel + 1U) % SYS_MENU_ITEMS);
            needs_redraw = 1;
        }

        if (this_button_status.event[BUTTON_LEFT_KP_ID] == BUTTON_EVENT_CLICK ||
            this_button_status.event[BUTTON_RIGHT_KP_ID] == BUTTON_EVENT_CLICK ||
            this_button_status.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK)
        {
            if (sel == SYS_ITEM_ESP32)
            {
                m1_esp32_auto_init = m1_esp32_auto_init ? 0 : 1;
                settings_save_to_sd();
                needs_redraw = 1;
            }
            else if (sel == SYS_ITEM_TIME)
            {
                settings_set_time();
                needs_redraw = 1;
            }
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
			m1_draw_text(&m1_u8g2, 8, 34, 114, T1000_VERSION_STRING, TEXT_ALIGN_LEFT);
			sprintf(prn_name, "Active bank: %d", (m1_device_stat.active_bank==BANK1_ACTIVE)?1:2);
			m1_draw_text(&m1_u8g2, 8, 44, 114, prn_name, TEXT_ALIGN_LEFT);
			break;

		case 1: // Company info
			u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_B);
			m1_draw_text(&m1_u8g2, 8, 24, 114, "MonstaTek Inc.", TEXT_ALIGN_LEFT);
			u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
			m1_draw_text(&m1_u8g2, 8, 34, 114, "San Jose, CA, USA", TEXT_ALIGN_LEFT);
			break;

		default:
			u8g2_DrawXBMP(&m1_u8g2, 37, 20, 55, 24, m1_device_55x24);
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

    snprintf(buf, sizeof(buf), "tz_offset=%d\n", m1_tz_offset_hours);
    f_write(&fp, buf, strlen(buf), &bw);

    snprintf(buf, sizeof(buf), "ism_region=%d\n", m1_device_stat.config.ism_band_region);
    f_write(&fp, buf, strlen(buf), &bw);

    snprintf(buf, sizeof(buf), "backlight_type=%d\n", m1_backlight_type);
    f_write(&fp, buf, strlen(buf), &bw);

    snprintf(buf, sizeof(buf), "rgb_mode=%d\n", (int)rgb_bl_get_mode());
    f_write(&fp, buf, strlen(buf), &bw);
    snprintf(buf, sizeof(buf), "rgb_effect=%d\n", (int)rgb_bl_get_effect());
    f_write(&fp, buf, strlen(buf), &bw);
    snprintf(buf, sizeof(buf), "rgb_brightness=%d\n", (int)rgb_bl_get_brightness());
    f_write(&fp, buf, strlen(buf), &bw);
    {
        uint8_t cr, cg, cb;
        rgb_bl_get_custom(&cr, &cg, &cb);
        snprintf(buf, sizeof(buf), "rgb_custom=%d,%d,%d\n", (int)cr, (int)cg, (int)cb);
        f_write(&fp, buf, strlen(buf), &bw);
    }
    snprintf(buf, sizeof(buf), "rgb_reactive=%d\n", (int)rgb_bl_reactive_is_on());
    f_write(&fp, buf, strlen(buf), &bw);

#ifdef M1_APP_BADBT_ENABLE
    snprintf(buf, sizeof(buf), "badbt_name=%s\n", m1_badbt_name);
    f_write(&fp, buf, strlen(buf), &bw);
#endif

    /* Empty when the built-in US layout is active */
    {
        char kb_buf[KB_LAYOUT_PATH_MAX + 16];
        snprintf(kb_buf, sizeof(kb_buf), "kb_layout=%s\n", kb_layout_path());
        f_write(&fp, kb_buf, strlen(kb_buf), &bw);
    }

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

    /* Parse "tz_offset=X" */
    p = strstr(buf, "tz_offset=");
    if (p != NULL)
    {
        p += 10;
        int tz = atoi(p);
        if (tz >= -12 && tz <= 14)
            m1_tz_offset_hours = (int8_t)tz;
    }

    /* Parse "ism_region=X" */
    p = strstr(buf, "ism_region=");
    if (p != NULL)
    {
        val = (int)(*(p + 11) - '0');
        if (val >= 0 && val <= 3)
            m1_device_stat.config.ism_band_region = (uint8_t)val;
    }

    /* Parse "backlight_type=X" */
    p = strstr(buf, "backlight_type=");
    if (p != NULL)
    {
        val = (int)(*(p + 15) - '0');
        if (val == 0 || val == 1)
            m1_backlight_type = (uint8_t)val;
    }

    /* Parse "rgb_mode=X" */
    p = strstr(buf, "rgb_mode=");
    if (p != NULL)
    {
        val = atoi(p + 9);
        if (val >= 0 && val < RGB_MODE_COUNT)
            rgb_bl_set_mode((rgb_bl_mode_t)val);
    }

    /* Parse "rgb_effect=X" */
    p = strstr(buf, "rgb_effect=");
    if (p != NULL)
    {
        val = (int)(*(p + 11) - '0');
        if (val >= 0 && val < RGB_EFFECT_COUNT)
            rgb_bl_set_effect((rgb_bl_effect_t)val);
    }

    /* Parse "rgb_brightness=X" */
    p = strstr(buf, "rgb_brightness=");
    if (p != NULL)
    {
        val = atoi(p + 15);
        if (val >= 0 && val <= 255)
            rgb_bl_set_brightness((uint8_t)val);
    }

    /* Parse "rgb_custom=R,G,B" */
    p = strstr(buf, "rgb_custom=");
    if (p != NULL)
    {
        int cr = 0, cg = 0, cb = 0;
        if (sscanf(p + 11, "%d,%d,%d", &cr, &cg, &cb) == 3)
        {
            rgb_bl_mode_t saved_mode = rgb_bl_get_mode();
            rgb_bl_set_custom((uint8_t)(cr & 0xFF), (uint8_t)(cg & 0xFF), (uint8_t)(cb & 0xFF));
            /* set_custom forces CUSTOM mode — restore the saved color mode so
             * the persisted rgb_mode wins unless the user actually chose Custom. */
            rgb_bl_set_mode(saved_mode);
        }
    }

    /* Parse "rgb_reactive=X" — start the reactive timer if enabled for the RGB
     * mod. The timer no-ops until the RGB hardware is brought up below. */
    p = strstr(buf, "rgb_reactive=");
    if (p != NULL && *(p + 13) == '1' && m1_backlight_type == 1)
    {
        rgb_bl_reactive_set(1);
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

    /* Parse "kb_layout=<path>" — BadUSB/Bad-BT keyboard layout. An empty or
     * missing value keeps the built-in US layout. */
    p = strstr(buf, "kb_layout=");
    if (p != NULL)
    {
        char kb_path[KB_LAYOUT_PATH_MAX];
        char *end;
        size_t len;

        p += 10;  /* skip "kb_layout=" */
        end = strchr(p, '\n');
        if (!end) end = p + strlen(p);

        len = (size_t)(end - p);
        if (len > 0 && len < sizeof(kb_path))
        {
            memcpy(kb_path, p, len);
            kb_path[len] = '\0';
            if (!kb_layout_load(kb_path))
                M1_LOG_W(SETTINGS_TAG, "Layout load failed: %s\r\n", kb_path);
        }
    }

apply:
    /* Apply backlight */
    m1_backlight_on(s_brightness_values[m1_brightness_level]);

    /* If RGB type selected, init the RGB hardware */
    if (m1_backlight_type == 1)
    {
        rgb_bl_init();
        rgb_bl_on();
    }

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
        "0:/NFC/assets",
        "0:/RFID",
        "0:/SUBGHZ",
        "0:/IR",
        "0:/BadUSB",
        KB_LAYOUT_DIR,
        "0:/BT",
        "0:/System",
        "0:/apps"
    };

    for (uint8_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++)
    {
        fs_directory_ensure(dirs[i]);
    }
}

/*============================================================================*/
/**
  * @brief  Standalone Backlight menu — Type toggle + OK launches Stock/RGB app
  */
/*============================================================================*/
#define BL_MENU_ITEMS   1
#define BL_MENU_TYPE    0
#define BL_POLL_MS      120U

static void backlight_menu_draw(uint8_t sel)
{
    char badge[8];
    snprintf(badge, sizeof(badge), "%u/%u", (unsigned)(sel + 1U), (unsigned)BL_MENU_ITEMS);

    m1_u8g2_firstpage();
    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
    m1_draw_header_bar(&m1_u8g2, "Backlight", badge);
    m1_draw_content_frame(&m1_u8g2, 2, 14, 124, 35);
    u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);

    const char *label = "Type";
    const char *value = s_bl_type_text[m1_backlight_type];
    uint8_t y = 30;

    if (sel == BL_MENU_TYPE)
    {
        u8g2_DrawBox(&m1_u8g2, 6, y - 7, 114, 11);
        u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
        u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_B);
        m1_draw_text(&m1_u8g2, 10, y, 64, label, TEXT_ALIGN_LEFT);
        m1_draw_text(&m1_u8g2, 78, y, 38, value, TEXT_ALIGN_RIGHT);
        u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
        u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
    }
    else
    {
        u8g2_DrawFrame(&m1_u8g2, 6, y - 7, 114, 11);
        m1_draw_text(&m1_u8g2, 10, y, 64, label, TEXT_ALIGN_LEFT);
        m1_draw_text(&m1_u8g2, 78, y, 38, value, TEXT_ALIGN_RIGHT);
    }

    m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Back", "OK=Edit", arrowright_8x8);
    m1_u8g2_nextpage();
}

void settings_backlight(void)
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
            backlight_menu_draw(sel);
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

        /* Left/Right — toggle type */
        if (this_button_status.event[BUTTON_LEFT_KP_ID] == BUTTON_EVENT_CLICK ||
            this_button_status.event[BUTTON_RIGHT_KP_ID] == BUTTON_EVENT_CLICK)
        {
            if (sel == BL_MENU_TYPE)
            {
                m1_backlight_type = m1_backlight_type ? 0 : 1;
                needs_redraw = 1;
            }
        }

        /* OK — launch the appropriate backlight app */
        if (this_button_status.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK)
        {
            if (m1_backlight_type == 1)
                app_rgb_backlight_run();
            else
                app_stock_backlight_run();
            needs_redraw = 1;
        }
    }
}
