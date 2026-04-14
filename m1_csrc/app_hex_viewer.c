/* See COPYING.txt for license details. */

/*
*
* app_hex_viewer.c
*
* Hex viewer utility app
*
* M1 Project
*
*/

/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "ff.h"
#include "m1_builtin_apps.h"
#include "m1_games.h"
#include "m1_storage.h"

/*************************** D E F I N E S ************************************/

#define HEX_VIEWER_PAGE_BYTES  24U
#define HEX_VIEWER_ROW_BYTES    6U
#define HEX_VIEWER_ROW_COUNT    4U
#define HEX_VIEWER_POLL_MS    120U

//************************** S T R U C T U R E S *******************************/

typedef struct
{
    FIL file;
    char path[192];
    char name[32];
    uint32_t file_size;
    uint32_t offset;
    bool file_open;
} hex_viewer_state_t;

/***************************** V A R I A B L E S ******************************/

static hex_viewer_state_t g_hex_viewer;

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

static void hex_viewer_close(void);
static bool hex_viewer_pick_file(hex_viewer_state_t *st);
static void hex_viewer_ascii_preview(const uint8_t *buf, uint16_t len, char *out, size_t out_len);
static void hex_viewer_format_row(const uint8_t *buf, uint16_t len, uint32_t row_offset, char *out, size_t out_len);
static void hex_viewer_draw(const hex_viewer_state_t *st);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

static void hex_viewer_close(void)
{
    if (g_hex_viewer.file_open)
    {
        f_close(&g_hex_viewer.file);
        g_hex_viewer.file_open = false;
    }
}


static bool hex_viewer_pick_file(hex_viewer_state_t *st)
{
    S_M1_file_info *f_info;
    FRESULT res;

    if (st == NULL)
    {
        return false;
    }

    hex_viewer_close();
    f_info = storage_browse(NULL);
    if (f_info == NULL || !f_info->file_is_selected)
    {
        return false;
    }

    snprintf(st->path, sizeof(st->path), "%s/%s", f_info->dir_name, f_info->file_name);
    strncpy(st->name, f_info->file_name, sizeof(st->name) - 1U);
    st->name[sizeof(st->name) - 1U] = '\0';

    res = f_open(&st->file, st->path, FA_READ | FA_OPEN_EXISTING);
    if (res != FR_OK)
    {
        st->file_open = false;
        st->file_size = 0U;
        st->offset = 0U;
        return false;
    }

    st->file_size = f_size(&st->file);
    st->offset = 0U;
    st->file_open = true;
    return true;
}


static void hex_viewer_ascii_preview(const uint8_t *buf, uint16_t len, char *out, size_t out_len)
{
    uint16_t count;

    if (out_len == 0U)
    {
        return;
    }

    count = (len < (uint16_t)(out_len - 1U)) ? len : (uint16_t)(out_len - 1U);
    for (uint16_t i = 0U; i < count; i++)
    {
        out[i] = isprint((int)buf[i]) ? (char)buf[i] : '.';
    }
    out[count] = '\0';
}


static void hex_viewer_format_row(const uint8_t *buf, uint16_t len, uint32_t row_offset, char *out, size_t out_len)
{
    uint16_t pos = 0U;

    if (out_len == 0U)
    {
        return;
    }

    pos += (uint16_t)snprintf(out + pos, out_len - pos, "%04lX ", (unsigned long)(row_offset & 0xFFFFUL));
    for (uint16_t i = 0U; i < len && pos < (out_len - 3U); i++)
    {
        pos += (uint16_t)snprintf(out + pos, out_len - pos, "%02X", buf[i]);
    }

    out[out_len - 1U] = '\0';
}


static void hex_viewer_draw(const hex_viewer_state_t *st)
{
    uint8_t page_buf[HEX_VIEWER_PAGE_BYTES];
    UINT br = 0U;
    char badge[14];
    char row_buf[24];
    char ascii_buf[20];
    char status_buf[24];
    uint32_t row_offset;
    uint8_t row_y;
    uint16_t row_len;

    if (st == NULL)
    {
        return;
    }

    memset(page_buf, 0, sizeof(page_buf));
    memset(ascii_buf, 0, sizeof(ascii_buf));

    if (st->file_open)
    {
        f_lseek((FIL *)&st->file, st->offset);
        f_read((FIL *)&st->file, page_buf, sizeof(page_buf), &br);
    }

    snprintf(badge, sizeof(badge), "%04lX/%04lX",
             (unsigned long)(st->offset & 0xFFFFUL),
             (unsigned long)(st->file_size & 0xFFFFUL));
    if (st->file_open)
    {
        strncpy(status_buf, st->name, sizeof(status_buf) - 1U);
        status_buf[sizeof(status_buf) - 1U] = '\0';
    }
    else
    {
        strncpy(status_buf, "No file selected", sizeof(status_buf) - 1U);
        status_buf[sizeof(status_buf) - 1U] = '\0';
    }
    hex_viewer_ascii_preview(page_buf, (uint16_t)br, ascii_buf, sizeof(ascii_buf));

    m1_u8g2_firstpage();
    m1_draw_header_bar(&m1_u8g2, "Hex Viewer", badge);
    m1_draw_content_frame(&m1_u8g2, 2, 14, 124, 37);
    u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
    m1_draw_text(&m1_u8g2, 8, 22, 114, status_buf, TEXT_ALIGN_LEFT);

    row_y = 30U;
    for (uint8_t row = 0U; row < HEX_VIEWER_ROW_COUNT; row++)
    {
        row_offset = st->offset + ((uint32_t)row * HEX_VIEWER_ROW_BYTES);
        if (row_offset >= st->file_size || row_offset >= (st->offset + br))
        {
            break;
        }

        row_len = (uint16_t)(br - (row * HEX_VIEWER_ROW_BYTES));
        if (row_len > HEX_VIEWER_ROW_BYTES)
        {
            row_len = HEX_VIEWER_ROW_BYTES;
        }

        hex_viewer_format_row(&page_buf[row * HEX_VIEWER_ROW_BYTES], row_len, row_offset, row_buf, sizeof(row_buf));
        m1_draw_text(&m1_u8g2, 8, row_y, 114, row_buf, TEXT_ALIGN_LEFT);
        row_y = (uint8_t)(row_y + 8U);
    }

    m1_draw_text(&m1_u8g2, 8, 50, 114, ascii_buf[0] ? ascii_buf : "ASCII preview", TEXT_ALIGN_LEFT);
    m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Back", "Browse", arrowright_8x8);
    m1_u8g2_nextpage();
}


void app_hex_viewer_run(void)
{
    game_button_t btn;

    memset(&g_hex_viewer, 0, sizeof(g_hex_viewer));
    if (!hex_viewer_pick_file(&g_hex_viewer))
    {
        return;
    }

    for (;;)
    {
        hex_viewer_draw(&g_hex_viewer);
        btn = game_poll_button(HEX_VIEWER_POLL_MS);

        if (btn == GAME_BTN_BACK)
        {
            hex_viewer_close();
            return;
        }
        if (btn == GAME_BTN_UP)
        {
            if (g_hex_viewer.offset >= HEX_VIEWER_ROW_BYTES)
            {
                g_hex_viewer.offset -= HEX_VIEWER_ROW_BYTES;
            }
            else
            {
                g_hex_viewer.offset = 0U;
            }
        }
        else if (btn == GAME_BTN_DOWN)
        {
            if ((g_hex_viewer.offset + HEX_VIEWER_ROW_BYTES) < g_hex_viewer.file_size)
            {
                g_hex_viewer.offset += HEX_VIEWER_ROW_BYTES;
            }
        }
        else if (btn == GAME_BTN_LEFT)
        {
            if (g_hex_viewer.offset >= HEX_VIEWER_PAGE_BYTES)
            {
                g_hex_viewer.offset -= HEX_VIEWER_PAGE_BYTES;
            }
            else
            {
                g_hex_viewer.offset = 0U;
            }
        }
        else if (btn == GAME_BTN_RIGHT)
        {
            if ((g_hex_viewer.offset + HEX_VIEWER_PAGE_BYTES) < g_hex_viewer.file_size)
            {
                g_hex_viewer.offset += HEX_VIEWER_PAGE_BYTES;
            }
        }
        else if (btn == GAME_BTN_OK)
        {
            if (!hex_viewer_pick_file(&g_hex_viewer))
            {
                hex_viewer_close();
                return;
            }
        }
    }
}
