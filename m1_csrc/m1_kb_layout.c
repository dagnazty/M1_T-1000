/* See COPYING.txt for license details. */

/*
*
* m1_kb_layout.c
*
* Shared ASCII -> HID keyboard layout for BadUSB and Bad-BT.
* See m1_kb_layout.h for the .kl file format.
*
* M1 Project
*
*/

/*************************** I N C L U D E S **********************************/

#include <string.h>
#include <stdio.h>
#include "ff.h"
#include "m1_kb_layout.h"
#include "m1_file_util.h"
#include "m1_log_debug.h"

/*************************** D E F I N E S ************************************/

#define M1_LOGDB_TAG    "KbLayout"

#define KB_LAYOUT_BUILTIN_NAME  "US (built-in)"

/* Printable ASCII range covered by the built-in table */
#define KB_ASCII_FIRST  0x20U
#define KB_ASCII_LAST   0x7EU

/************************** S T R U C T U R E S *******************************/

typedef struct
{
    uint8_t keycode;
    uint8_t modifier;   /* HID modifier bitmask */
} kb_key_t;

/***************************** V A R I A B L E S ******************************/

/* Built-in US layout, indices 0x20-0x7E */
static const kb_key_t s_us_layout[KB_ASCII_LAST - KB_ASCII_FIRST + 1U] =
{
    /* 0x20 ' '  */ {KEY_SPACE,      0},
    /* 0x21 '!'  */ {KEY_1,          HID_MOD_LSHIFT},
    /* 0x22 '"'  */ {KEY_APOSTROPHE, HID_MOD_LSHIFT},
    /* 0x23 '#'  */ {KEY_3,          HID_MOD_LSHIFT},
    /* 0x24 '$'  */ {KEY_4,          HID_MOD_LSHIFT},
    /* 0x25 '%'  */ {KEY_5,          HID_MOD_LSHIFT},
    /* 0x26 '&'  */ {KEY_7,          HID_MOD_LSHIFT},
    /* 0x27 '\'' */ {KEY_APOSTROPHE, 0},
    /* 0x28 '('  */ {KEY_9,          HID_MOD_LSHIFT},
    /* 0x29 ')'  */ {KEY_0,          HID_MOD_LSHIFT},
    /* 0x2A '*'  */ {KEY_8,          HID_MOD_LSHIFT},
    /* 0x2B '+'  */ {KEY_EQUAL,      HID_MOD_LSHIFT},
    /* 0x2C ','  */ {KEY_COMMA,      0},
    /* 0x2D '-'  */ {KEY_MINUS,      0},
    /* 0x2E '.'  */ {KEY_DOT,        0},
    /* 0x2F '/'  */ {KEY_SLASH,      0},
    /* 0x30 '0'  */ {KEY_0,          0},
    /* 0x31 '1'  */ {KEY_1,          0},
    /* 0x32 '2'  */ {KEY_2,          0},
    /* 0x33 '3'  */ {KEY_3,          0},
    /* 0x34 '4'  */ {KEY_4,          0},
    /* 0x35 '5'  */ {KEY_5,          0},
    /* 0x36 '6'  */ {KEY_6,          0},
    /* 0x37 '7'  */ {KEY_7,          0},
    /* 0x38 '8'  */ {KEY_8,          0},
    /* 0x39 '9'  */ {KEY_9,          0},
    /* 0x3A ':'  */ {KEY_SEMICOLON,  HID_MOD_LSHIFT},
    /* 0x3B ';'  */ {KEY_SEMICOLON,  0},
    /* 0x3C '<'  */ {KEY_COMMA,      HID_MOD_LSHIFT},
    /* 0x3D '='  */ {KEY_EQUAL,      0},
    /* 0x3E '>'  */ {KEY_DOT,        HID_MOD_LSHIFT},
    /* 0x3F '?'  */ {KEY_SLASH,      HID_MOD_LSHIFT},
    /* 0x40 '@'  */ {KEY_2,          HID_MOD_LSHIFT},
    /* 0x41-0x5A: A-Z */
    {KEY_A, HID_MOD_LSHIFT}, {KEY_B, HID_MOD_LSHIFT}, {KEY_C, HID_MOD_LSHIFT},
    {KEY_D, HID_MOD_LSHIFT}, {KEY_E, HID_MOD_LSHIFT}, {KEY_F, HID_MOD_LSHIFT},
    {KEY_G, HID_MOD_LSHIFT}, {KEY_H, HID_MOD_LSHIFT}, {KEY_I, HID_MOD_LSHIFT},
    {KEY_J, HID_MOD_LSHIFT}, {KEY_K, HID_MOD_LSHIFT}, {KEY_L, HID_MOD_LSHIFT},
    {KEY_M, HID_MOD_LSHIFT}, {KEY_N, HID_MOD_LSHIFT}, {KEY_O, HID_MOD_LSHIFT},
    {KEY_P, HID_MOD_LSHIFT}, {KEY_Q, HID_MOD_LSHIFT}, {KEY_R, HID_MOD_LSHIFT},
    {KEY_S, HID_MOD_LSHIFT}, {KEY_T, HID_MOD_LSHIFT}, {KEY_U, HID_MOD_LSHIFT},
    {KEY_V, HID_MOD_LSHIFT}, {KEY_W, HID_MOD_LSHIFT}, {KEY_X, HID_MOD_LSHIFT},
    {KEY_Y, HID_MOD_LSHIFT}, {KEY_Z, HID_MOD_LSHIFT},
    /* 0x5B '['  */ {KEY_LEFTBRACE,  0},
    /* 0x5C '\\' */ {KEY_BACKSLASH,  0},
    /* 0x5D ']'  */ {KEY_RIGHTBRACE, 0},
    /* 0x5E '^'  */ {KEY_6,          HID_MOD_LSHIFT},
    /* 0x5F '_'  */ {KEY_MINUS,      HID_MOD_LSHIFT},
    /* 0x60 '`'  */ {KEY_GRAVE,      0},
    /* 0x61-0x7A: a-z */
    {KEY_A, 0}, {KEY_B, 0}, {KEY_C, 0}, {KEY_D, 0}, {KEY_E, 0},
    {KEY_F, 0}, {KEY_G, 0}, {KEY_H, 0}, {KEY_I, 0}, {KEY_J, 0},
    {KEY_K, 0}, {KEY_L, 0}, {KEY_M, 0}, {KEY_N, 0}, {KEY_O, 0},
    {KEY_P, 0}, {KEY_Q, 0}, {KEY_R, 0}, {KEY_S, 0}, {KEY_T, 0},
    {KEY_U, 0}, {KEY_V, 0}, {KEY_W, 0}, {KEY_X, 0}, {KEY_Y, 0},
    {KEY_Z, 0},
    /* 0x7B '{'  */ {KEY_LEFTBRACE,  HID_MOD_LSHIFT},
    /* 0x7C '|'  */ {KEY_BACKSLASH,  HID_MOD_LSHIFT},
    /* 0x7D '}'  */ {KEY_RIGHTBRACE, HID_MOD_LSHIFT},
    /* 0x7E '~'  */ {KEY_GRAVE,      HID_MOD_LSHIFT},
};

/* Layout loaded from SD. The byte order of a .kl file (keycode, modifier)
 * matches this struct on a little-endian target, so it is read in directly. */
static kb_key_t s_loaded[KB_LAYOUT_ENTRIES];
static bool     s_loaded_active = false;
static char     s_layout_name[KB_LAYOUT_NAME_MAX + 1] = KB_LAYOUT_BUILTIN_NAME;
static char     s_layout_path[KB_LAYOUT_PATH_MAX] = "";

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/*============================================================================*/
/**
  * @brief  Sanity-check a freshly loaded layout
  * @note   A layout that cannot type the basic alphanumerics is either a
  *         corrupt file or not a layout at all — reject it rather than let
  *         a payload silently type nothing.
  */
/*============================================================================*/
static bool kb_layout_is_sane(void)
{
    static const char probe[] = { 'a', 'z', 'A', 'Z', '0', '9', ' ' };

    for (uint8_t i = 0; i < sizeof(probe); i++)
    {
        if (s_loaded[(uint8_t)probe[i]].keycode == KEY_NONE)
            return false;
    }

    return true;
}


/*============================================================================*/
/**
  * @brief  Look up the keycode and modifier for an ASCII character
  * @retval false if the character is not typeable on the active layout
  */
/*============================================================================*/
bool kb_layout_lookup(char c, uint8_t *keycode, uint8_t *modifier)
{
    uint8_t uc = (uint8_t)c;

    if (keycode == NULL || modifier == NULL || uc >= KB_LAYOUT_ENTRIES)
        return false;

    if (s_loaded_active)
    {
        if (s_loaded[uc].keycode == KEY_NONE)
            return false;

        *keycode  = s_loaded[uc].keycode;
        *modifier = s_loaded[uc].modifier;
        return true;
    }

    if (uc < KB_ASCII_FIRST || uc > KB_ASCII_LAST)
        return false;

    *keycode  = s_us_layout[uc - KB_ASCII_FIRST].keycode;
    *modifier = s_us_layout[uc - KB_ASCII_FIRST].modifier;
    return true;
}


/*============================================================================*/
/**
  * @brief  Revert to the built-in US layout
  */
/*============================================================================*/
void kb_layout_reset(void)
{
    s_loaded_active = false;
    s_layout_path[0] = '\0';
    strcpy(s_layout_name, KB_LAYOUT_BUILTIN_NAME);
}


/*============================================================================*/
/**
  * @brief  Load a .kl layout file from SD
  * @param  path: full path to the layout file
  * @retval true on success; on failure the built-in layout is restored
  */
/*============================================================================*/
bool kb_layout_load(const char *path)
{
    FIL     fp;
    FRESULT fres;
    UINT    br = 0;

    if (path == NULL || path[0] == '\0')
    {
        kb_layout_reset();
        return false;
    }

    fres = f_open(&fp, path, FA_READ);
    if (fres != FR_OK)
    {
        M1_LOG_W(M1_LOGDB_TAG, "Open failed: %s (err=%d)\r\n", path, fres);
        kb_layout_reset();
        return false;
    }

    fres = f_read(&fp, s_loaded, sizeof(s_loaded), &br);
    f_close(&fp);

    if (fres != FR_OK || br != KB_LAYOUT_FILE_SIZE)
    {
        M1_LOG_W(M1_LOGDB_TAG, "Bad layout size: %u (want %u)\r\n",
                 (unsigned)br, (unsigned)KB_LAYOUT_FILE_SIZE);
        kb_layout_reset();
        return false;
    }

    if (!kb_layout_is_sane())
    {
        M1_LOG_W(M1_LOGDB_TAG, "Layout rejected: %s\r\n", path);
        kb_layout_reset();
        return false;
    }

    s_loaded_active = true;
    strncpy(s_layout_path, path, sizeof(s_layout_path) - 1);
    s_layout_path[sizeof(s_layout_path) - 1] = '\0';
    fu_get_filename_without_ext(path, s_layout_name, sizeof(s_layout_name));

    return true;
}


/*============================================================================*/
/**
  * @brief  Display name of the active layout
  */
/*============================================================================*/
const char *kb_layout_name(void)
{
    return s_layout_name;
}


/*============================================================================*/
/**
  * @brief  Path of the loaded layout file, or "" when built-in is active
  */
/*============================================================================*/
const char *kb_layout_path(void)
{
    return s_layout_path;
}
