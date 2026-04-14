/* See COPYING.txt for license details. */

/*
 *
 * app_rgb_backlight.c
 *
 * RGB Backlight settings menu for M1
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
#include "m1_rgb_backlight.h"
#include "m1_display.h"

/*************************** D E F I N E S ************************************/

#define RGB_MENU_ITEMS     4U
#define RGB_MENU_ONOFF     0U
#define RGB_MENU_COLOR     1U
#define RGB_MENU_EFFECT    2U
#define RGB_MENU_BRIGHT    3U

#define RGB_POLL_MS      120U

/***************************** V A R I A B L E S ******************************/

static const char *s_brightness_labels[] = {
    "Off", "10%", "25%", "50%", "75%", "100%"
};
static const uint8_t s_brightness_values[] = {
    0, 26, 64, 128, 192, 255
};

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

static void rgb_menu_draw(uint8_t sel);
static const char *rgb_menu_item_label(uint8_t item);
static const char *rgb_menu_item_value(uint8_t item);
static uint8_t rgb_brightness_index(uint8_t brightness);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

static const char *rgb_menu_item_label(uint8_t item)
{
    switch (item)
    {
        case RGB_MENU_ONOFF:   return "RGB Mod";
        case RGB_MENU_COLOR:   return "LCD Color";
        case RGB_MENU_EFFECT:  return "Animation";
        case RGB_MENU_BRIGHT:  return "Brightness";
        default:               return "";
    }
}

static const char *rgb_menu_item_value(uint8_t item)
{
    static char val_buf[12];

    switch (item)
    {
        case RGB_MENU_ONOFF:   return rgb_bl_is_on() ? "On" : "Off";
        case RGB_MENU_COLOR:   return rgb_bl_mode_name(rgb_bl_get_mode());
        case RGB_MENU_EFFECT:  return rgb_bl_effect_name(rgb_bl_get_effect());
        case RGB_MENU_BRIGHT:
        {
            uint8_t b = rgb_bl_get_brightness();
            for (uint8_t i = 0; i < 6; i++)
            {
                if (b <= s_brightness_values[i])
                {
                    snprintf(val_buf, sizeof(val_buf), "%s", s_brightness_labels[i]);
                    return val_buf;
                }
            }
            return "100%";
        }
        default:               return "";
    }
}

static uint8_t rgb_brightness_index(uint8_t brightness)
{
    uint8_t best = 0U;
    uint16_t best_diff = 255U;

    for (uint8_t i = 0; i < 6U; i++)
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

static void rgb_menu_draw(uint8_t sel)
{
    char badge[8];
    uint8_t visible_start = 0;

    if (sel > 1 && RGB_MENU_ITEMS > 3)
        visible_start = (sel - 1 > RGB_MENU_ITEMS - 3) ? RGB_MENU_ITEMS - 3 : sel - 1;

    snprintf(badge, sizeof(badge), "%u/%u", (unsigned)(sel + 1U), (unsigned)RGB_MENU_ITEMS);

    m1_u8g2_firstpage();
    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
    m1_draw_header_bar(&m1_u8g2, "RGB Backlight", badge);
    m1_draw_content_frame(&m1_u8g2, 2, 14, 124, 35);
    u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);

    for (uint8_t vi = 0; vi < 3 && (visible_start + vi) < RGB_MENU_ITEMS; vi++)
    {
        uint8_t item = visible_start + vi;
        uint8_t y = 24 + vi * 10;
        const char *label = rgb_menu_item_label(item);
        const char *value = rgb_menu_item_value(item);

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


void app_rgb_backlight_run(void)
{
    game_button_t btn;
    uint8_t sel = 0U;
    uint8_t needs_redraw = 1U;
    uint8_t bright_idx;

    /* Initialize RGB backlight on entry */
    rgb_bl_init();
    rgb_bl_on();
    bright_idx = rgb_brightness_index(rgb_bl_get_brightness());

    for (;;)
    {
        if (needs_redraw)
        {
            needs_redraw = 0U;
            rgb_menu_draw(sel);
        }

        /* Run effects at ~8fps */
        btn = game_poll_button(RGB_POLL_MS);
        rgb_bl_update();

        if (btn == GAME_BTN_BACK)
        {
            rgb_bl_off();
            rgb_bl_deinit();
            return;
        }

        if (btn == GAME_BTN_UP)
        {
            sel = (sel == 0U) ? (RGB_MENU_ITEMS - 1U) : (sel - 1U);
            needs_redraw = 1U;
        }
        else if (btn == GAME_BTN_DOWN)
        {
            sel = (sel + 1U) % RGB_MENU_ITEMS;
            needs_redraw = 1U;
        }
        else if (btn == GAME_BTN_LEFT)
        {
            if (sel == RGB_MENU_COLOR)
            {
                rgb_bl_mode_t m = rgb_bl_get_mode();
                rgb_bl_set_mode((m == 0U) ? (RGB_MODE_COUNT - 1U) : (rgb_bl_mode_t)(m - 1U));
            }
            else if (sel == RGB_MENU_EFFECT)
            {
                rgb_bl_effect_t e = rgb_bl_get_effect();
                rgb_bl_set_effect((e == 0U) ? (RGB_EFFECT_COUNT - 1U) : (rgb_bl_effect_t)(e - 1U));
            }
            else if (sel == RGB_MENU_BRIGHT)
            {
                bright_idx = (bright_idx == 0U) ? 5U : (bright_idx - 1U);
                rgb_bl_set_brightness(s_brightness_values[bright_idx]);
            }
            else if (sel == RGB_MENU_ONOFF)
            {
                if (rgb_bl_is_on()) rgb_bl_off();
                else rgb_bl_on();
            }
            needs_redraw = 1U;
        }
        else if (btn == GAME_BTN_RIGHT || btn == GAME_BTN_OK)
        {
            if (sel == RGB_MENU_COLOR)
            {
                rgb_bl_mode_t m = rgb_bl_get_mode();
                rgb_bl_set_mode((rgb_bl_mode_t)((m + 1U) % RGB_MODE_COUNT));
            }
            else if (sel == RGB_MENU_EFFECT)
            {
                rgb_bl_effect_t e = rgb_bl_get_effect();
                rgb_bl_set_effect((rgb_bl_effect_t)((e + 1U) % RGB_EFFECT_COUNT));
            }
            else if (sel == RGB_MENU_BRIGHT)
            {
                bright_idx = (bright_idx + 1U) % 6U;
                rgb_bl_set_brightness(s_brightness_values[bright_idx]);
            }
            else if (sel == RGB_MENU_ONOFF)
            {
                if (rgb_bl_is_on()) rgb_bl_off();
                else rgb_bl_on();
            }
            needs_redraw = 1U;
        }
    }
}
