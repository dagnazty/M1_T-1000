/* See COPYING.txt for license details. */

/*
*
* app_clock.c
*
* Clock utility app
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
#include "m1_system.h"

/*************************** D E F I N E S ************************************/

#define CLOCK_PAGE_COUNT  5U
#define CLOCK_POLL_MS     200U

//************************** S T R U C T U R E S *******************************/

typedef struct
{
    const char *label;
    int8_t offset_hours;
} world_zone_t;

/***************************** V A R I A B L E S ******************************/

static const char *clock_weekdays[] = {
    "---",
    "Mon",
    "Tue",
    "Wed",
    "Thu",
    "Fri",
    "Sat",
    "Sun"
};

static const world_zone_t clock_zones[] = {
    { "UTC",    0 },
    { "UTC+1",  1 },
    { "UTC+5",  5 },
    { "UTC+9",  9 }
};

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

static bool clock_is_leap_year(uint16_t year);
static uint8_t clock_days_in_month(uint16_t year, uint8_t month);
static void clock_adjust_days(m1_time_t *dt, int8_t delta_days);
static void clock_apply_offset(const m1_time_t *src, int8_t offset_hours, m1_time_t *dst);
static void clock_draw_page(uint8_t page);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

static bool clock_is_leap_year(uint16_t year)
{
    return ((year % 4U) == 0U && ((year % 100U) != 0U || (year % 400U) == 0U));
}


static uint8_t clock_days_in_month(uint16_t year, uint8_t month)
{
    static const uint8_t month_days[] = { 31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U };

    if (month == 2U && clock_is_leap_year(year))
    {
        return 29U;
    }
    if (month >= 1U && month <= 12U)
    {
        return month_days[month - 1U];
    }
    return 30U;
}


static void clock_adjust_days(m1_time_t *dt, int8_t delta_days)
{
    if (dt == NULL || delta_days == 0)
    {
        return;
    }

    while (delta_days > 0)
    {
        uint8_t dim = clock_days_in_month(dt->year, dt->month);
        if (dt->day < dim)
        {
            dt->day++;
        }
        else
        {
            dt->day = 1U;
            if (dt->month < 12U)
            {
                dt->month++;
            }
            else
            {
                dt->month = 1U;
                dt->year++;
            }
        }

        if (dt->weekday >= 1U && dt->weekday <= 7U)
        {
            dt->weekday = (dt->weekday == 7U) ? 1U : (uint8_t)(dt->weekday + 1U);
        }
        delta_days--;
    }

    while (delta_days < 0)
    {
        if (dt->day > 1U)
        {
            dt->day--;
        }
        else
        {
            if (dt->month > 1U)
            {
                dt->month--;
            }
            else
            {
                dt->month = 12U;
                if (dt->year > 2000U)
                {
                    dt->year--;
                }
            }
            dt->day = clock_days_in_month(dt->year, dt->month);
        }

        if (dt->weekday >= 1U && dt->weekday <= 7U)
        {
            dt->weekday = (dt->weekday == 1U) ? 7U : (uint8_t)(dt->weekday - 1U);
        }
        delta_days++;
    }
}


static void clock_apply_offset(const m1_time_t *src, int8_t offset_hours, m1_time_t *dst)
{
    int16_t hour;

    if (src == NULL || dst == NULL)
    {
        return;
    }

    *dst = *src;
    hour = (int16_t)src->hour + (int16_t)offset_hours;

    while (hour < 0)
    {
        hour += 24;
        clock_adjust_days(dst, -1);
    }

    while (hour >= 24)
    {
        hour -= 24;
        clock_adjust_days(dst, 1);
    }

    dst->hour = (uint8_t)hour;
}


static void clock_draw_page(uint8_t page)
{
    m1_time_t now;
    m1_time_t zone_time;
    char badge[8];
    char date_line[24];
    char time_line[12];
    const world_zone_t *zone = NULL;
    const char *weekday = clock_weekdays[1];
    const char *title = "Local clock";

    m1_get_localtime(&now);
    if (now.weekday <= 7U)
    {
        weekday = clock_weekdays[now.weekday];
    }

    snprintf(badge, sizeof(badge), "%u/%u", (unsigned)(page + 1U), (unsigned)CLOCK_PAGE_COUNT);
    if (page == 0U)
    {
        zone_time = now;
        snprintf(time_line, sizeof(time_line), "%02u:%02u:%02u", zone_time.hour, zone_time.minute, zone_time.second);
        snprintf(date_line, sizeof(date_line), "%s %02u/%02u/%04u", weekday, zone_time.month, zone_time.day, zone_time.year);
    }
    else
    {
        zone = &clock_zones[page - 1U];
        title = zone->label;
        clock_apply_offset(&now, zone->offset_hours, &zone_time);
        snprintf(time_line, sizeof(time_line), "%02u:%02u:%02u", zone_time.hour, zone_time.minute, zone_time.second);
        snprintf(date_line, sizeof(date_line), "%02u/%02u/%04u", zone_time.month, zone_time.day, zone_time.year);
    }

    m1_u8g2_firstpage();
    m1_draw_header_bar(&m1_u8g2, "Clock", badge);
    m1_draw_content_frame(&m1_u8g2, 2, 14, 124, 37);
    u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
    m1_draw_text(&m1_u8g2, 8, 22, 114, date_line, TEXT_ALIGN_LEFT);
    u8g2_SetFont(&m1_u8g2, M1_DISP_LARGE_FONT_2B);
    m1_draw_text(&m1_u8g2, 10, 40, 108, time_line, TEXT_ALIGN_CENTER);
    u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
    m1_draw_text(&m1_u8g2, 8, 50, 114, title, TEXT_ALIGN_LEFT);
    m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Back", "Next", arrowright_8x8);
    m1_u8g2_nextpage();
}


void app_clock_run(void)
{
    uint8_t page = 0U;
    game_button_t btn;

    for (;;)
    {
        clock_draw_page(page);
        btn = game_poll_button(CLOCK_POLL_MS);

        if (btn == GAME_BTN_BACK)
        {
            return;
        }
        if (btn == GAME_BTN_LEFT || btn == GAME_BTN_UP)
        {
            page = (page == 0U) ? (CLOCK_PAGE_COUNT - 1U) : (uint8_t)(page - 1U);
        }
        else if (btn == GAME_BTN_RIGHT || btn == GAME_BTN_DOWN || btn == GAME_BTN_OK)
        {
            page = (uint8_t)((page + 1U) % CLOCK_PAGE_COUNT);
        }
    }
}
