/* See COPYING.txt for license details. */

/*
*
* app_dvd_logo.c
*
* Bouncing DVD logo app
*
* M1 Project
*
*/

/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "stm32h5xx_hal.h"
#include "m1_builtin_apps.h"
#include "m1_games.h"

/*************************** D E F I N E S ************************************/

#define DVD_LOGO_W              40
#define DVD_LOGO_H              32
#define DVD_AREA_X              0
#define DVD_AREA_Y              10
#define DVD_AREA_W              128
#define DVD_AREA_H              54
#define DVD_FRAME_MS            35U
#define DVD_SPEED_MIN           1
#define DVD_SPEED_MAX           3

//************************** S T R U C T U R E S *******************************/

typedef struct
{
    int16_t x;
    int16_t y;
    int8_t dx;
    int8_t dy;
    uint8_t speed;
    uint16_t bounces;
    uint16_t corners;
    bool paused;
    bool show_trail;
} dvd_logo_state_t;

/***************************** V A R I A B L E S ******************************/

static dvd_logo_state_t g_dvd_logo;

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

static void dvd_logo_init(dvd_logo_state_t *st);
static void dvd_logo_update(dvd_logo_state_t *st);
static void dvd_logo_draw(const dvd_logo_state_t *st);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

static void dvd_logo_init(dvd_logo_state_t *st)
{
    memset(st, 0, sizeof(*st));
    st->x = (DVD_AREA_W - DVD_LOGO_W) / 2;
    st->y = DVD_AREA_Y + ((DVD_AREA_H - DVD_LOGO_H) / 2);
    st->dx = 1;
    st->dy = 1;
    st->speed = 2;
    st->paused = false;
    st->show_trail = false;
}


static void dvd_logo_update(dvd_logo_state_t *st)
{
    bool hit_x = false;
    bool hit_y = false;
    int16_t next_x;
    int16_t next_y;

    if (st->paused)
    {
        return;
    }

    next_x = st->x + (st->dx * (int8_t)st->speed);
    next_y = st->y + (st->dy * (int8_t)st->speed);

    if (next_x <= DVD_AREA_X)
    {
        next_x = DVD_AREA_X;
        st->dx = 1;
        hit_x = true;
    }
    else if (next_x >= (DVD_AREA_X + DVD_AREA_W - DVD_LOGO_W))
    {
        next_x = DVD_AREA_X + DVD_AREA_W - DVD_LOGO_W;
        st->dx = -1;
        hit_x = true;
    }

    if (next_y <= DVD_AREA_Y)
    {
        next_y = DVD_AREA_Y;
        st->dy = 1;
        hit_y = true;
    }
    else if (next_y >= (DVD_AREA_Y + DVD_AREA_H - DVD_LOGO_H))
    {
        next_y = DVD_AREA_Y + DVD_AREA_H - DVD_LOGO_H;
        st->dy = -1;
        hit_y = true;
    }

    st->x = next_x;
    st->y = next_y;

    if (hit_x || hit_y)
    {
        st->bounces++;
        if (hit_x && hit_y)
        {
            st->corners++;
            m1_buzzer_notification();
        }
    }
}


static void dvd_logo_draw(const dvd_logo_state_t *st)
{
    char status_buf[24];
    char hint_buf[24];

    m1_u8g2_firstpage();
    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);

    u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
    snprintf(status_buf, sizeof(status_buf), "DVD S%u B%u C%u",
             st->speed, st->bounces, st->corners);
    u8g2_DrawStr(&m1_u8g2, 0, 8, status_buf);

    if (st->show_trail)
    {
        u8g2_DrawFrame(&m1_u8g2, st->x + 3, st->y + 3, DVD_LOGO_W, DVD_LOGO_H);
    }

    u8g2_DrawBox(&m1_u8g2, st->x, st->y, DVD_LOGO_W, DVD_LOGO_H);
    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
    u8g2_DrawXBMP(&m1_u8g2, st->x, st->y, DVD_LOGO_W, DVD_LOGO_H, m1_logo_40x32);
    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);

    if (st->paused)
    {
        u8g2_DrawBox(&m1_u8g2, 88, 0, 40, 9);
        u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
        u8g2_DrawStr(&m1_u8g2, 93, 8, "PAUSED");
        u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
    }

    snprintf(hint_buf, sizeof(hint_buf), "OK speed U trail");
    u8g2_DrawStr(&m1_u8g2, 0, 63, hint_buf);
    u8g2_DrawStr(&m1_u8g2, 83, 63, "D pause");

    m1_u8g2_nextpage();
}


void app_dvd_logo_run(void)
{
    game_button_t btn;

    dvd_logo_init(&g_dvd_logo);

    for (;;)
    {
        dvd_logo_update(&g_dvd_logo);
        dvd_logo_draw(&g_dvd_logo);

        btn = game_poll_button(DVD_FRAME_MS);

        if (btn == GAME_BTN_BACK)
        {
            return;
        }

        if (btn == GAME_BTN_OK)
        {
            if (g_dvd_logo.speed < DVD_SPEED_MAX)
            {
                g_dvd_logo.speed++;
            }
            else
            {
                g_dvd_logo.speed = DVD_SPEED_MIN;
            }
        }
        else if (btn == GAME_BTN_UP)
        {
            g_dvd_logo.show_trail = !g_dvd_logo.show_trail;
        }
        else if (btn == GAME_BTN_DOWN)
        {
            g_dvd_logo.paused = !g_dvd_logo.paused;
        }
    }
}
