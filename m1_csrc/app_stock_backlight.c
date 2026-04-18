/* See COPYING.txt for license details. */

/*
 *
 * app_stock_backlight.c
 *
 * Stock LP5814 backlight control menu for M1
 *
 * M1 Project
 *
 */

/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "m1_builtin_apps.h"
#include "m1_games.h"
#include "m1_lp5814.h"
#include "m1_display.h"
#include "m1_system.h"
#include "m1_rgb_backlight.h"

/*************************** D E F I N E S ************************************/

#define STOCK_MENU_ITEMS     2U
#define STOCK_MENU_ONOFF     0U
#define STOCK_MENU_BRIGHT    1U

#define STOCK_POLL_MS      120U
#define STOCK_VISIBLE_ITEMS 2U

/***************************** V A R I A B L E S ******************************/

static const char *s_brightness_labels[] = {
    "Off", "Low", "Med", "High", "Max"
};
static const uint8_t s_brightness_values[] = {
    0, 64, 128, 192, 255
};

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

static void stock_menu_draw(uint8_t sel, uint8_t bright_idx);
static uint8_t stock_brightness_index(uint8_t brightness);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

static uint8_t stock_brightness_index(uint8_t brightness)
{
    uint8_t best = 0U;
    uint16_t best_diff = 255U;

    for (uint8_t i = 0; i < 5U; i++)
    {
        uint16_t diff = (brightness > s_brightness_values[i]) ?
            (uint16_t)(brightness - s_brightness_values[i]) :
            (uint16_t)(s_brightness_values[i] - brightness);
        if (diff < best_diff)
        {
            best = i;
            best_diff = diff;
        }
    }

    return best;
}

static void stock_menu_draw(uint8_t sel, uint8_t bright_idx)
{
    char badge[8];
    uint8_t visible_start = 0;

    if (sel >= STOCK_VISIBLE_ITEMS && STOCK_MENU_ITEMS > STOCK_VISIBLE_ITEMS)
        visible_start = (uint8_t)(sel - STOCK_VISIBLE_ITEMS + 1U);

    snprintf(badge, sizeof(badge), "%u/%u", (unsigned)(sel + 1U), (unsigned)STOCK_MENU_ITEMS);

    m1_u8g2_firstpage();
    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
    m1_draw_header_bar(&m1_u8g2, "Backlight", badge);
    m1_draw_content_frame(&m1_u8g2, 2, 14, 124, 35);
    u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);

    for (uint8_t vi = 0; vi < STOCK_VISIBLE_ITEMS && (visible_start + vi) < STOCK_MENU_ITEMS; vi++)
    {
        uint8_t item = visible_start + vi;
        uint8_t y = 30 + vi * 12;
        const char *label;
        const char *value;

        switch (item)
        {
            case STOCK_MENU_ONOFF:  label = "Backlight"; value = (m1_brightness_level > 0) ? "On" : "Off"; break;
            case STOCK_MENU_BRIGHT: label = "Brightness"; value = s_brightness_labels[bright_idx]; break;
            default: label = ""; value = ""; break;
        }

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


void app_stock_backlight_run(void)
{
    game_button_t btn;
    uint8_t sel = 0U;
    uint8_t needs_redraw = 1U;
    uint8_t bright_idx = stock_brightness_index(s_brightness_values[m1_brightness_level]);

    for (;;)
    {
        if (needs_redraw)
        {
            needs_redraw = 0U;
            stock_menu_draw(sel, bright_idx);
        }

        btn = game_poll_button(STOCK_POLL_MS);

        if (btn == GAME_BTN_BACK)
            return;

        if (btn == GAME_BTN_UP)
        {
            sel = (sel == 0U) ? (STOCK_MENU_ITEMS - 1U) : (sel - 1U);
            needs_redraw = 1U;
        }
        else if (btn == GAME_BTN_DOWN)
        {
            sel = (sel + 1U) % STOCK_MENU_ITEMS;
            needs_redraw = 1U;
        }
        else if (btn == GAME_BTN_LEFT)
        {
            if (sel == STOCK_MENU_BRIGHT)
            {
                bright_idx = (bright_idx == 0U) ? 4U : (bright_idx - 1U);
                m1_brightness_level = bright_idx;
                m1_backlight_on(s_brightness_values[bright_idx]);
            }
            else if (sel == STOCK_MENU_ONOFF)
            {
                if (m1_brightness_level > 0)
                {
                    m1_brightness_level = 0;
                    m1_backlight_on(0);
                    bright_idx = 0;
                }
                else
                {
                    m1_brightness_level = 3;
                    m1_backlight_on(s_brightness_values[3]);
                    bright_idx = 3;
                }
            }
            needs_redraw = 1U;
        }
        else if (btn == GAME_BTN_RIGHT || btn == GAME_BTN_OK)
        {
            if (sel == STOCK_MENU_BRIGHT)
            {
                bright_idx = (bright_idx + 1U) % 5U;
                m1_brightness_level = bright_idx;
                m1_backlight_on(s_brightness_values[bright_idx]);
            }
            else if (sel == STOCK_MENU_ONOFF)
            {
                if (m1_brightness_level > 0)
                {
                    m1_brightness_level = 0;
                    m1_backlight_on(0);
                    bright_idx = 0;
                }
                else
                {
                    m1_brightness_level = 3;
                    m1_backlight_on(s_brightness_values[3]);
                    m1_backlight_on(s_brightness_values[3]);
                    bright_idx = 3;
                }
            }
            needs_redraw = 1U;
        }
    }
}
