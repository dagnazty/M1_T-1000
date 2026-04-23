/* See COPYING.txt for license details. */

/*
*
* m1_display.c
*
* Functions to display objects on the M1 LCD
*
* M1 Project
*
*/

/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
//#include "stm32h5xx_hal.h"
//#include "main.h"
#include "battery.h"
#include "m1_lp5814.h"
#include "m1_io_defs.h"
#include "m1_branding.h"
#include "m1_compile_cfg.h"
#include "m1_display.h"
#include "m1_sdcard.h"
#include "m1_wifi.h"
#include "m1_system.h"

/*************************** D E F I N E S ************************************/

#define M1_LOGDB_TAG	"Display"

#define MENU_M1_LOGO_ARRAY_LEN 		1
#define MENU_M1_SCR_ANI_TIMEOUT		1000 // animation timeout in millisecond

#define MENU_SCROLLBAR_POS_X				124
#define MENU_SCROLLBAR_POS_Y				12
#define MENU_SCROLLBAR_WIDTH				4

#define MENU_HEADER_HEIGHT                  11
#define MENU_SCROLLBAR_TRACK_Y              (MENU_HEADER_HEIGHT + 2)
#define MENU_SCROLLBAR_TRACK_H              50

#define MENU_MAIN_PANEL_X                   0
#define MENU_MAIN_PANEL_Y                   13
#define MENU_MAIN_PANEL_W                   42
#define MENU_MAIN_PANEL_H                   49

#define MENU_MAIN_ROW_X                     45
#define MENU_MAIN_ROW_W                     76
#define MENU_MAIN_ROW_H                     13
#define MENU_MAIN_ROW_TEXT_X                67
#define MENU_MAIN_ROW_ICON_X                50

#define MENU_SUB_ROW_X                      2
#define MENU_SUB_ROW_W                      120
#define MENU_SUB_ROW_H                      12
#define MENU_SUB_ROW_TEXT_X                 8

#define MAIN_MENU_TEXT_ITEMS				4
#define MAIN_MENU_TEXT_FRAME_TOP_POS_Y		0
#define MAIN_MENU_TXT_LEFT_POS_X			68
#define MAIN_MENU_TXT_TOP_POS_Y				12
#define MAIN_MENU_TXT_SPACING_VERT			16
#define MAIN_MENU_TEXT_FRAME_H				16
#define MAIN_MENU_TEXT_FRAME_W				75
#define MAIN_MENU_TEXT_FRAME_LEFT_POS_X		48

#define MAIN_MENU_ICON_LEFT_POS_X			50
#define MAIN_MENU_ICON_WIDTH				14
#define MAIN_MENU_ICON_HEIGHT				14

#define MAIN_MENU_LOGO_TOP_POS_Y			24
#define MAIN_MENU_LOGO_LEFT_POS_X			0
#define MAIN_MENU_LOGO_WIDTH				26
#define MAIN_MENU_LOGO_HEIGHT				14

#define MAIN_MENU_LOGO_FONT					M1_MAIN_LOGO_FONT_1B

#define SUB_MENU_TEXT_ITEMS					4 // rows
#define SUB_MENU_TEXT_FRAME_TOP_POS_Y		0
#define SUB_MENU_TXT_LEFT_POS_X				4
#define SUB_MENU_TEXT_TOP_POS_Y				12
#define SUB_MENU_TEXT_SPACING_VERT			16
#define SUB_MENU_TEXT_FRAME_H				16
#define SUB_MENU_TEXT_FRAME_W				(M1_LCD_DISPLAY_WIDTH - MENU_SCROLLBAR_WIDTH - 1)
#define SUB_MENU_TEXT_FRAME_LEFT_POS_X		0


//************************** C O N S T A N T **********************************/

const S_M1_menu_icon_data menu_fb_icon_prev = {fb_m1_icon_folder, 10, 8};
const S_M1_menu_icon_data menu_fb_icon_dir = {fb_m1_icon_folder, 10, 8};
const S_M1_menu_icon_data menu_fb_icon_text = {fb_m1_icon_file, 10, 8};
const S_M1_menu_icon_data menu_fb_icon_data = {fb_m1_icon_file, 10, 8};
const S_M1_menu_icon_data menu_fb_icon_other = {fb_m1_icon_file, 10, 8};

const uint8_t *menu_m1_logo_array[MENU_M1_LOGO_ARRAY_LEN] = {
	menu_m1_icon_M1_logo_1,
//	menu_m1_icon_M1_logo_2,
//	menu_m1_icon_M1_logo_3,
//	menu_m1_icon_M1_logo_4
};

static const uint8_t menu_window_sizes[] = {MAIN_MENU_TEXT_ITEMS, SUB_MENU_TEXT_ITEMS};
static const uint8_t *menu_text_font_n[] = {M1_DISP_MAIN_MENU_FONT_N, M1_DISP_SUB_MENU_FONT_N};
static const uint8_t *menu_text_font_b[] = {M1_DISP_MAIN_MENU_FONT_B, M1_DISP_SUB_MENU_FONT_B};

//************************** S T R U C T U R E S *******************************

typedef struct
{
 	uint8_t active_disp_row; // selected row of the display window
	uint8_t disp_top_row; // first row of the moving display window
	uint8_t sel_item; // selected item of the menu list
} S_M1_Disp_Window_t;

/***************************** V A R I A B L E S ******************************/

static uint8_t disp_window_active_row;
static uint8_t disp_window_top_row; // moving display window
static uint8_t menu_level_id; // main menu (0) or sub menu
static bool info_box_high_box;
static uint8_t info_box_first_row = 0;
static const S_M1_Menu_t *this_gui_menu;

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

void m1_gui_init(void);
void m1_gui_welcome_scr(void);
void m1_gui_scr_animation(void);
void m1_gui_menu_update(const S_M1_Menu_t *phmenu, uint8_t sel_item, uint8_t direction);
uint8_t m1_gui_submenu_update(const char *phmenu[], uint8_t num_items, uint8_t sel_item, uint8_t direction);

void m1_info_box_display_init(bool high_box);
void m1_info_box_display_clear(void);
void m1_info_box_display_draw(uint8_t box_row, const uint8_t *ptext);
uint8_t m1_message_box(u8g2_t *u8g2, const char *title1, const char *title2, const char *title3, const char *buttons);
void m1_draw_bottom_bar(u8g2_t *u8g2, const uint8_t *lbitmap, const char *ltext, const char *rtext, const uint8_t *rbitmap);
void m1_draw_icon(uint8_t color, u8g2_uint_t x, u8g2_uint_t y, u8g2_uint_t w, u8g2_uint_t h, const uint8_t *bitmap);
void m1_draw_text(u8g2_t *u8g2,
                 int x, int y,
                 int max_width,
                 const char *text,
                 S_M1_text_align_t align);
void m1_draw_text_box(u8g2_t *u8g2,
                    int x, int y,
                    int max_width,          // 텍스트 박스 폭(px)
                    int line_height,        // 줄 간격(px)
                    const char *text,
                    S_M1_text_align_t align);
static void m1_gui_draw_menu_header(const char *title, uint8_t sel_item, uint8_t num_items);
static void m1_gui_draw_main_menu_panel(void);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/*============================================================================*/
/**
  * @brief This function sets up some settings for the menu display
  * @param None
  * @retval None
  */
/*============================================================================*/
void m1_gui_init(void)
{
	m1_lcd_cleardisplay();
	u8g2_SetBitmapMode(&m1_u8g2, 1);

	// used for scrolling text
	/*
	u8g2_SetFont(&m1_u8g2, u8g2_font_10x20_mr);
	width = u8g2_GetUTF8Width(&m1_u8g2, "text");
	u8g2_SetFontMode(&m1_u8g2, 0);		// enable transparent mode, which is faster
	*/
} // void m1_gui_init(void)



/*============================================================================*/
/**
  * @brief This function displays the M1 welcome screen
  * @param None
  * @retval None
  */
/*============================================================================*/
void m1_gui_welcome_scr(void)
{
	startup_info_screen_display("");
} // void m1_gui_welcome_scr(void)



/*============================================================================*/
/**
  * @brief This function updates the menu content on the display
  * @param
  * @retval None
  */
/*============================================================================*/
void m1_gui_menu_update(const S_M1_Menu_t *phmenu, uint8_t sel_item, uint8_t direction)
{
	static S_M1_Disp_Window_t menu_display[SUB_MENU_LEVEL_MAX];
	static uint8_t menu_level = 0;
	uint8_t n_items, run;
	const char *psubmenu[SUB_MENU_ITEMS_MAX];

	if ( direction==MENU_UPDATE_INIT )
		menu_level = 0;
	this_gui_menu = phmenu;
	n_items = phmenu->num_submenu_items;

	if ( phmenu->gui_menu_update ) // This menu item has its own gui update function?
	{
		phmenu->gui_menu_update(phmenu, sel_item);
		if ( direction==MENU_UPDATE_RESET ) // Sub-menu changes to deeper level?
		{
			menu_display[menu_level].disp_top_row = disp_window_top_row;
			menu_display[menu_level].active_disp_row = disp_window_active_row; // back up
			menu_level++;
		}
		return;
	} // if ( phmenu->gui_menu_update )

	if ( direction==MENU_UPDATE_RESET ) // Sub-menu is changed
	{
		menu_level++;
	} // if ( direction==MENU_UPDATE_RESET )
	else if ( direction==MENU_UPDATE_RESTORE )
	{
		menu_level--;
		// Restore previous display values
		disp_window_top_row = menu_display[menu_level].disp_top_row;
		disp_window_active_row = menu_display[menu_level].active_disp_row;
	} // else if ( direction==MENU_UPDATE_RESTORE )
	for (run=0; run<n_items; run++)
	{
		if (phmenu->submenu[run] == NULL) {
			/* Should never happen, but prevent crash */
			psubmenu[run] = "[ERROR]";
			continue;
		}
		psubmenu[run] = phmenu->submenu[run]->title;
	}
	menu_level_id = (menu_level==0)?0:1;
	m1_gui_submenu_update(psubmenu, n_items, sel_item, direction);

	// Back up
	menu_display[menu_level].disp_top_row = disp_window_top_row;
	menu_display[menu_level].active_disp_row = disp_window_active_row;
} // void m1_gui_menu_update(const S_M1_Menu_t *phmenu, uint8_t sel_item, uint8_t direction)



/*============================================================================*/
/**
  * @brief This function updates the sub-menu content on the display
  * @param
  * @retval None
  */
/*============================================================================*/
uint8_t m1_gui_submenu_update(const char *phmenu[], uint8_t num_items, uint8_t sel_item, uint8_t direction)
{
	static S_M1_Disp_Window_t x_menu_display[SUB_MENU_LEVEL_MAX];
	static uint8_t x_menu_level = 0, x_menu_update_init = 0;
	uint8_t disp_window_bottom_row;
	uint8_t n_items, active_item, run;
	uint8_t row_box_x, row_box_w, row_h, row_text_x;
	uint8_t row_box_y, row_text_y;
	uint8_t scroll_handle_h, scroll_handle_y;
	const char *header_title;

	n_items = num_items;

	switch ( direction )
	{
		case X_MENU_UPDATE_MOVE_DOWN:
			sel_item = x_menu_display[x_menu_level].sel_item;
			sel_item++;
			if ( sel_item >= n_items )
				sel_item = 0;
			/* fall through */

		case MENU_UPDATE_MOVE_DOWN: // moving down
			if ( n_items > menu_window_sizes[menu_level_id] )
			{
				if ( disp_window_active_row < (menu_window_sizes[menu_level_id] - 1) )
				{
					disp_window_active_row++;
				}
				else if ( disp_window_active_row==(menu_window_sizes[menu_level_id] - 1) )
				{
					if ( sel_item==(n_items-1) ) // reach end of the menu items list?
					{
						disp_window_active_row++;
					} // if ( sel_item==(n_items-1) )
					else
					{
						// move display window down
						disp_window_top_row++;
					}
				} // else if ( disp_window_active_row==(menu_window_sizes[menu_level_id] - 1) )
				else // disp_window_active_row==menu_window_sizes[menu_level_id]
				{
					disp_window_active_row = 1; // reset to the first row
					disp_window_top_row = 1;
				} // else
			} // if ( n_items > menu_window_sizes[menu_level_id] )
			else // n_items <= menu_window_sizes[menu_level_id]
			{
				if ( disp_window_active_row < n_items )
				{
					disp_window_active_row++;
				} // if ( disp_window_active_row < n_items )
				else // disp_window_active_row==n_items
				{
					disp_window_active_row = 1; // reset to the first row
				} // else
			} // else
			break;

		case X_MENU_UPDATE_MOVE_UP:
			sel_item = x_menu_display[x_menu_level].sel_item;
			if ( sel_item )
				sel_item--;
			else
				sel_item = n_items - 1;
			/* fall through */

		case MENU_UPDATE_MOVE_UP: // moving up
			if ( n_items >= menu_window_sizes[menu_level_id] )
			{
				if ( disp_window_active_row > 2 )
				{
					disp_window_active_row--;
				} // if ( disp_window_active_row > 2 )
				else if ( disp_window_active_row==2 )
				{
					if ( sel_item==0 ) // reach first item of the list?
					{
						disp_window_active_row = 1; // rest to the first row
					} // if ( sel_item==0 )
					else
					{
						// move display window up
						disp_window_top_row--;
					} // else
				} // else if ( disp_window_active_row==2 )
				else // being at the first row
				{
					disp_window_active_row = menu_window_sizes[menu_level_id]; // move to the last row
					disp_window_top_row = n_items - (menu_window_sizes[menu_level_id] - 1);
				} // else
			} // if ( n_items >= menu_window_sizes[menu_level_id] )
			else // n_items < menu_window_sizes[menu_level_id]
			{
				if ( disp_window_active_row > 1 )
				{
					disp_window_active_row--;
				} // if ( disp_window_active_row > 1 )
				else // being at the first row
				{
					disp_window_active_row = n_items; // move to the last item
				} // else
			} // else
			break;

		case MENU_UPDATE_INIT:
		case MENU_UPDATE_RESET:
			disp_window_active_row = 1; // reset to first row
    		disp_window_top_row = 1;
			break;

		case MENU_UPDATE_REFRESH:
			if ( x_menu_update_init )
			{
				x_menu_update_init = 0; // Reset
				x_menu_level = 0;
				disp_window_active_row = x_menu_display[0].active_disp_row; // Restore
				disp_window_top_row = x_menu_display[0].disp_top_row;
			} // if ( x_menu_update_init )
			break;

		case X_MENU_UPDATE_INIT:
			x_menu_update_init = 1; // Mark as initialized
    		x_menu_level = 0; // Reset
			x_menu_display[0].active_disp_row = disp_window_active_row; // back up
			x_menu_display[0].disp_top_row = disp_window_top_row;
			x_menu_display[0].sel_item = 0;
			return 0;

		case X_MENU_UPDATE_RESET:
			x_menu_level++;
			if ( x_menu_level >= SUB_MENU_LEVEL_MAX )
				x_menu_level = SUB_MENU_LEVEL_MAX - 1;
    		disp_window_top_row = 1;
			disp_window_active_row = 1; // reset to first row
    		sel_item = 0; // Overwrite input param
			break;

		case X_MENU_UPDATE_RESTORE:
			if ( x_menu_level )
				x_menu_level--;
			disp_window_active_row = x_menu_display[x_menu_level].active_disp_row; // previous values
			disp_window_top_row = x_menu_display[x_menu_level].disp_top_row;
			sel_item = x_menu_display[x_menu_level].sel_item;
			if ( sel_item >= n_items ) // Restore to an unexpected submenu?
			{
				sel_item = 0;
				disp_window_top_row = 1;
				disp_window_active_row = 1; // Reset to first row
			}
			if ( x_menu_level==0 )
				return 0; // Do nothing if the x_menu stays at its init level
			break;

		case X_MENU_UPDATE_REFRESH:
			sel_item = x_menu_display[x_menu_level].sel_item; // Restore from saved setting
			break;

		case MENU_UPDATE_NONE:
			if ( x_menu_update_init )
			{
				return x_menu_display[x_menu_level].sel_item;
			} // if ( x_menu_update_init )
			break;

		default:
			break;
	} // switch ( direction )

	/* Graphic work starts here */
    u8g2_FirstPage(&m1_u8g2); // This call required for page drawing in mode 1
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);

	if ( n_items >= menu_window_sizes[menu_level_id] )
		disp_window_bottom_row = disp_window_top_row + (menu_window_sizes[menu_level_id] - 1); // full window display
	else
		disp_window_bottom_row = disp_window_top_row + (n_items - 1); // partial window display
	active_item = disp_window_top_row;
	active_item += disp_window_active_row - 1;

	header_title = (menu_level_id == 0) ? M1_PRODUCT_MARK : this_gui_menu->title;
	m1_gui_draw_menu_header(header_title, sel_item, n_items);

	if (menu_level_id == 0)
	{
		row_box_x = MENU_MAIN_ROW_X;
		row_box_w = MENU_MAIN_ROW_W;
		row_h = MENU_MAIN_ROW_H;
		row_text_x = MENU_MAIN_ROW_TEXT_X;
		m1_gui_draw_main_menu_panel();
	}
	else
	{
		row_box_x = MENU_SUB_ROW_X;
		row_box_w = MENU_SUB_ROW_W;
		row_h = MENU_SUB_ROW_H;
		row_text_x = MENU_SUB_ROW_TEXT_X;
	}

	u8g2_SetFont(&m1_u8g2, menu_text_font_n[menu_level_id]);
	for (run=disp_window_top_row; run<=disp_window_bottom_row; run++)
	{
		row_box_y = MENU_HEADER_HEIGHT + 1 + ((run - disp_window_top_row) * row_h);
		row_text_y = row_box_y + ((menu_level_id == 0) ? 10 : 9);

		if ( run==active_item )
		{
			u8g2_DrawBox(&m1_u8g2, row_box_x, row_box_y, row_box_w, row_h);
			u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
			u8g2_SetFont(&m1_u8g2, menu_text_font_b[menu_level_id]);
		}
		else
		{
			u8g2_DrawFrame(&m1_u8g2, row_box_x, row_box_y, row_box_w, row_h);
			u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
			u8g2_SetFont(&m1_u8g2, menu_text_font_n[menu_level_id]);
		} // if ( run==active_item )

		if ( menu_level_id==0 )
		{
			if (this_gui_menu->submenu[run - 1]->icon_ptr != NULL)
			{
				u8g2_DrawXBMP(&m1_u8g2, MENU_MAIN_ROW_ICON_X, row_box_y + 1,
				              MAIN_MENU_ICON_WIDTH, MAIN_MENU_ICON_HEIGHT,
				              this_gui_menu->submenu[run - 1]->icon_ptr);
			}
		}

		u8g2_DrawStr(&m1_u8g2, row_text_x, row_text_y, phmenu[run - 1]);
		u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	} // for (run=disp_window_top_row; run<=disp_window_bottom_row; run++)

	// Draw the scroll bar
	u8g2_DrawFrame(&m1_u8g2, MENU_SCROLLBAR_POS_X, MENU_SCROLLBAR_TRACK_Y, MENU_SCROLLBAR_WIDTH, MENU_SCROLLBAR_TRACK_H);
	if (num_items > 0)
	{
		scroll_handle_h = MENU_SCROLLBAR_TRACK_H / num_items;
		if (scroll_handle_h < 6)
			scroll_handle_h = 6;

		scroll_handle_y = MENU_SCROLLBAR_TRACK_Y + ((MENU_SCROLLBAR_TRACK_H * sel_item) / num_items);
		if ((scroll_handle_y + scroll_handle_h) > (MENU_SCROLLBAR_TRACK_Y + MENU_SCROLLBAR_TRACK_H - 1))
			scroll_handle_y = MENU_SCROLLBAR_TRACK_Y + MENU_SCROLLBAR_TRACK_H - scroll_handle_h - 1;

		u8g2_DrawBox(&m1_u8g2, MENU_SCROLLBAR_POS_X + 1, scroll_handle_y, MENU_SCROLLBAR_WIDTH - 2, scroll_handle_h);
	}

	u8g2_NextPage(&m1_u8g2); // Update display RAM

	// Back up
	if ( x_menu_update_init )
	{
		x_menu_display[x_menu_level].active_disp_row = disp_window_active_row;
		x_menu_display[x_menu_level].disp_top_row = disp_window_top_row;
		x_menu_display[x_menu_level].sel_item = sel_item;
	} // if ( x_menu_update_init )

	return 0;
} // uint8_t m1_gui_submenu_update(const char *phmenu[], uint8_t num_items, uint8_t sel_item, uint8_t direction)

static void m1_gui_draw_menu_header(const char *title, uint8_t sel_item, uint8_t num_items)
{
	char header_status[12];
	S_M1_Power_Status_t power_status;
	char battery_status[8];
	m1_time_t now;
	char time_str[9];

	u8g2_DrawBox(&m1_u8g2, 0, 0, M1_LCD_DISPLAY_WIDTH, MENU_HEADER_HEIGHT - 1);
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
	u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);

	if (menu_level_id == 0)
	{
		battery_power_status_get(&power_status);
		snprintf(battery_status, sizeof(battery_status), "%u%%", power_status.battery_level);

		m1_get_localtime(&now);
		snprintf(time_str, sizeof(time_str), "%02u:%02u", now.hour, now.minute);

		m1_draw_text(&m1_u8g2, 3, 8, 20, title, TEXT_ALIGN_LEFT);
		m1_draw_text(&m1_u8g2, 40, 8, 30, time_str, TEXT_ALIGN_RIGHT);
		m1_draw_text(&m1_u8g2, 72, 8, 24, battery_status, TEXT_ALIGN_RIGHT);

		int rssi = wifi_get_rssi();
		if (rssi != 0)
		{
			uint8_t bars;
			if (rssi > -50) bars = 4;
			else if (rssi > -60) bars = 3;
			else if (rssi > -70) bars = 2;
			else bars = 1;
			for (uint8_t b = 0; b < 4; b++)
			{
				uint8_t h = 2 + b * 2;
				uint8_t bx = 97 + b * 3;
				uint8_t by = 9 - h;
				if (b < bars)
					u8g2_DrawBox(&m1_u8g2, bx, by, 2, h);
				else
					u8g2_DrawFrame(&m1_u8g2, bx, by, 2, h);
			}
		}
		else
		{
			m1_draw_text(&m1_u8g2, 96, 8, 18, "--", TEXT_ALIGN_CENTER);
		}
		if (m1_sdcard_get_status() == SD_access_OK)
			u8g2_DrawXBMP(&m1_u8g2, 114, 1, 10, 10, sd_card_10x10);
		else
			m1_draw_text(&m1_u8g2, 104, 8, 20, "--", TEXT_ALIGN_RIGHT);
	}
	else
	{
		m1_draw_text(&m1_u8g2, 3, 8, 84, title, TEXT_ALIGN_LEFT);
		snprintf(header_status, sizeof(header_status), "%u/%u", (unsigned)(sel_item + 1), (unsigned)num_items);
		m1_draw_text(&m1_u8g2, 88, 8, 26, header_status, TEXT_ALIGN_RIGHT);

		if (m1_sdcard_get_status() == SD_access_OK)
		{
			u8g2_DrawXBMP(&m1_u8g2, 116, 1, 10, 10, sd_card_10x10);
		}
	}

	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_DrawHLine(&m1_u8g2, 0, MENU_HEADER_HEIGHT - 1, M1_LCD_DISPLAY_WIDTH);
}

static void m1_gui_draw_main_menu_panel(void)
{
	u8g2_DrawFrame(&m1_u8g2, MENU_MAIN_PANEL_X, MENU_MAIN_PANEL_Y, MENU_MAIN_PANEL_W, MENU_MAIN_PANEL_H);
	u8g2_DrawFrame(&m1_u8g2, MENU_MAIN_PANEL_X + 1, MENU_MAIN_PANEL_Y + 1, MENU_MAIN_PANEL_W - 2, MENU_MAIN_PANEL_H - 2);
	u8g2_DrawXBMP(&m1_u8g2, 1, 18, 40, 32, m1_logo_40x32);
	u8g2_DrawHLine(&m1_u8g2, 6, 52, 30);
	u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
	m1_draw_text(&m1_u8g2, 0, 60, MENU_MAIN_PANEL_W, "M1", TEXT_ALIGN_CENTER);
}



/*============================================================================*/
/**
  * @brief This function updates welcome screen content periodically
  * @param  None
  * @retval None
  */
/*============================================================================*/
void m1_gui_scr_animation(void)
{
	static uint32_t time_t0 = 0;
	static uint32_t time_tn = 0;

	if ( m1_device_stat.op_mode != M1_OPERATION_MODE_DISPLAY_ON ) // do animation at welcome screen only
		return;

	time_tn = HAL_GetTick();
	if ( (time_tn - time_t0) >= MENU_M1_SCR_ANI_TIMEOUT )
	{
		time_t0 = time_tn; // save current time

		// Draw time measured approximately 14ms for a full screen
		// CPU running at 75MHz
		// I2C speed = 400KHz
		u8g2_FirstPage(&m1_u8g2);
		do
	    {
			u8g2_DrawXBMP(&m1_u8g2, 32, 0, 64, 64, m1_logo_64x64);
	    } while (u8g2_NextPage(&m1_u8g2));
	} // if ( (time_tn - time_t0) >= MENU_M1_SCR_ANI_TIMEOUT )
} // void m1_gui_scr_animation(void)



/*============================================================================*/
/**
  * @brief
  * @param  None
  * @retval None
  */
/*============================================================================*/
void m1_info_box_display_init(bool high_box)
{
	uint8_t y0 = 42, h0 = 22;
	const uint8_t *pframe = m1_frame_128x22;

	info_box_first_row = INFO_BOX_Y_POS_ROW_2; // low box
	info_box_high_box = high_box;
	if ( high_box )
	{
		info_box_first_row = INFO_BOX_Y_POS_ROW_1; // high box
		y0 = 32;
		h0 = 32;
		pframe = m1_frame_128x32;
	}

	// Draw info box at the bottom
	u8g2_DrawXBMP(&m1_u8g2, 0, y0, 128, h0, pframe);
} // void m1_info_box_display_init(bool high_box)



/*============================================================================*/
/**
  * @brief
  * @param  None
  * @retval None
  */
/*============================================================================*/
void m1_info_box_display_clear(void)
{
	uint8_t y0 = 42, h0 = 22;

	if ( info_box_high_box )
	{
		y0 = 32;
		h0 = 32;
	}

	// Draw box to clear old content
	u8g2_DrawBox(&m1_u8g2, 1, y0 + 1, 126, h0 - 2);
} // void m1_info_box_display_clear(void)



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval None
  */
/*============================================================================*/
void m1_info_box_display_draw(uint8_t box_row, const uint8_t *ptext)
{
	uint8_t yn;

	if ( box_row >= INFO_BOX_ROW_EOL )
		return;

	yn = (box_row - INFO_BOX_ROW_1)*10;
	yn += info_box_first_row;

	u8g2_DrawStr(&m1_u8g2, 4, yn, ptext);
} // void m1_info_box_display_draw(uint8_t box_row, const uint8_t *ptext)



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
uint8_t m1_message_box(u8g2_t *u8g2, const char *title1, const char *title2, const char *title3, const char *buttons)
{

  u8g2_UserInterfaceMessage(u8g2, title1, title2, title3, buttons);

  for(;;)
  {
      S_M1_Main_Q_t q_item;
      S_M1_Buttons_Status this_button_status;
      BaseType_t ret;

		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
		if (ret==pdTRUE)
		{
			if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
			{
				// Notification is only sent to this task when there's any button activity,
				// so it doesn't need to wait when reading the event from the queue
				ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
				if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK
			  || this_button_status.event[BUTTON_LEFT_KP_ID]==BUTTON_EVENT_CLICK
			  || this_button_status.event[BUTTON_OK_KP_ID]==BUTTON_EVENT_CLICK
			  || this_button_status.event[BUTTON_RIGHT_KP_ID]==BUTTON_EVENT_CLICK ) // user wants to exit?
				{
					; // Do extra tasks here if needed

					xQueueReset(main_q_hdl); // Reset main q before return
					break; // Exit and return to the calling task (subfunc_handler_task)
				} // if ( m1_buttons_status[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK )
				else
				{
					; // Do other things for this task, if needed
				}
			} // if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
			else
			{
				; // Do other things for this task
			}
		} // if (ret==pdTRUE)

  }

  /* never reached */
  return 0;
}

uint8_t m1_message_box_choice(u8g2_t *u8g2, const char *title1, const char *title2, const char *title3, const char *buttons)
{
    uint8_t cursor = 0;
    uint8_t button_cnt = u8x8_GetStringLineCnt(buttons);
    if (button_cnt == 0) return 0;

    for (;;) {
        u8g2_FirstPage(u8g2);
        do {
            u8g2_SetFont(u8g2, u8g2_font_6x10_tr);
            uint8_t line_height = u8g2_GetAscent(u8g2) - u8g2_GetDescent(u8g2);
            uint8_t y = 15;
            
            if (title1) { u8g2_DrawStr(u8g2, 2, y, title1); y += line_height; }
            if (title2) { u8g2_DrawStr(u8g2, 2, y, title2); y += line_height; }
            if (title3) { u8g2_DrawStr(u8g2, 2, y, title3); y += line_height; }
            
            // Draw buttons at the bottom
            for (uint8_t i = 0; i < button_cnt; i++) {
                const char *btn_text = u8x8_GetStringLineStart(i, buttons);
                uint8_t btn_w = u8g2_GetStrWidth(u8g2, btn_text) + 4;
                uint8_t btn_x = (128 / (button_cnt + 1)) * (i + 1) - (btn_w / 2);
                
                if (i == cursor) {
                    u8g2_DrawBox(u8g2, btn_x - 2, 50, btn_w, 12);
                    u8g2_SetDrawColor(u8g2, 0);
                }
                u8g2_DrawStr(u8g2, btn_x, 60, btn_text);
                u8g2_SetDrawColor(u8g2, 1);
            }
        } while (u8g2_NextPage(u8g2));

        S_M1_Main_Q_t q_item;
        S_M1_Buttons_Status btn_status;
        if (xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY) == pdTRUE) {
            if (q_item.q_evt_type == Q_EVENT_KEYPAD) {
                xQueueReceive(button_events_q_hdl, &btn_status, 0);
                if (btn_status.event[BUTTON_LEFT_KP_ID] == BUTTON_EVENT_CLICK || 
                    btn_status.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK) {
                    if (cursor > 0) cursor--; else cursor = button_cnt - 1;
                }
                else if (btn_status.event[BUTTON_RIGHT_KP_ID] == BUTTON_EVENT_CLICK || 
                         btn_status.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK) {
                    if (cursor < button_cnt - 1) cursor++; else cursor = 0;
                }
                else if (btn_status.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK) {
                    xQueueReset(main_q_hdl);
                    return cursor + 1;
                }
                else if (btn_status.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) {
                    xQueueReset(main_q_hdl);
                    return 0;
                }
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
void m1_draw_bottom_bar(u8g2_t *u8g2, const uint8_t *lbitmap, const char *ltext, const char *rtext, const uint8_t *rbitmap)
{
	u8g2_SetDrawColor(u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_DrawHLine(u8g2, 0, 51, 128);
	u8g2_DrawBox(u8g2, 0, 52, 128, 12);

	u8g2_SetDrawColor(u8g2, M1_DISP_DRAW_COLOR_BG);
	u8g2_DrawVLine(u8g2, 63, 54, 8);
	if (lbitmap != NULL)
		u8g2_DrawXBMP(u8g2, 2, 54, 8, 8, lbitmap);
	if (rbitmap != NULL)
		u8g2_DrawXBMP(u8g2, 118, 54, 8, 8, rbitmap);

	m1_draw_text(u8g2, 12, 61, 46, ltext, TEXT_ALIGN_LEFT);
	m1_draw_text(u8g2, 68, 61, 46, rtext, TEXT_ALIGN_RIGHT);
	u8g2_SetDrawColor(u8g2, M1_DISP_DRAW_COLOR_TXT);
}

/*============================================================================*/
/**
  * @brief Draw the shared top header used by runtime screens
  * @param u8g2 Display handle
  * @param title Left-aligned title text
  * @param badge Optional right-aligned badge/status text
  * @retval None
  */
/*============================================================================*/
void m1_draw_header_bar(u8g2_t *u8g2, const char *title, const char *badge)
{
	S_M1_Power_Status_t power_status;
	char battery_status[8];
	bool sd_ok;
	uint8_t title_w;

	battery_power_status_get(&power_status);
	snprintf(battery_status, sizeof(battery_status), "%u%%",
	         (unsigned)power_status.battery_level);
	sd_ok = (m1_sdcard_get_status() == SD_access_OK);

	u8g2_SetDrawColor(u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_DrawBox(u8g2, 0, 0, 128, 11);
	u8g2_SetDrawColor(u8g2, M1_DISP_DRAW_COLOR_BG);
	u8g2_SetFont(u8g2, M1_DISP_FUNC_MENU_FONT_N);

	/* Layout (128-wide bar):
	 *   [0..3] padding  [3..title_w]  title
	 *   [badge at x=60..86]  (optional, only if passed)
	 *   [battery% at x=86..112, right-aligned]
	 *   [SD icon at x=114..124]  or "--" text when no SD
	 * When no badge is passed, title gets the extra width. */
	title_w = badge ? 54 : 82;
	m1_draw_text(u8g2, 3, 8, title_w,
	             title ? title : M1_PRODUCT_MARK, TEXT_ALIGN_LEFT);
	if (badge != NULL)
	{
		m1_draw_text(u8g2, 60, 8, 26, badge, TEXT_ALIGN_LEFT);
	}
	m1_draw_text(u8g2, 86, 8, 26, battery_status, TEXT_ALIGN_RIGHT);
	if (sd_ok)
	{
		u8g2_DrawXBMP(u8g2, 114, 1, 10, 10, sd_card_10x10);
	}
	else
	{
		m1_draw_text(u8g2, 112, 8, 14, "--", TEXT_ALIGN_RIGHT);
	}

	u8g2_SetDrawColor(u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_DrawHLine(u8g2, 0, 11, 128);
}

/*============================================================================*/
/**
  * @brief Draw a shared content frame for detail/status screens
  * @param u8g2 Display handle
  * @param x Left position
  * @param y Top position
  * @param w Frame width
  * @param h Frame height
  * @retval None
  */
/*============================================================================*/
void m1_draw_content_frame(u8g2_t *u8g2, u8g2_uint_t x, u8g2_uint_t y, u8g2_uint_t w, u8g2_uint_t h)
{
	u8g2_DrawFrame(u8g2, x, y, w, h);
	if (w > 4 && h > 4)
	{
		u8g2_DrawFrame(u8g2, x + 1, y + 1, w - 2, h - 2);
	}
}

/*============================================================================*/
/**
  * @brief Draw a shared framed status panel with optional icon
  * @param u8g2 Display handle
  * @param title Header title
  * @param badge Optional header badge
  * @param icon Optional bitmap
  * @param icon_w Bitmap width
  * @param icon_h Bitmap height
  * @param line1 First body line
  * @param line2 Second body line
  * @param line3 Third body line
  * @retval None
  */
/*============================================================================*/
void m1_draw_status_panel(u8g2_t *u8g2,
				 const char *title,
				 const char *badge,
				 const uint8_t *icon,
				 u8g2_uint_t icon_w,
				 u8g2_uint_t icon_h,
				 const char *line1,
				 const char *line2,
				 const char *line3)
{
	int text_x = 8;
	int text_w = 114;
	int icon_x = 7;
	int icon_y = 16;

	m1_draw_header_bar(u8g2, title, badge);
	m1_draw_content_frame(u8g2, 2, 14, 124, 35);

	if (icon != NULL)
	{
		if (icon_h < 30)
			icon_y = 16 + (30 - icon_h) / 2;
		u8g2_DrawXBMP(u8g2, icon_x, icon_y, icon_w, icon_h, icon);
		text_x = icon_x + icon_w + 6;
		text_w = 122 - text_x;
	}

	u8g2_SetFont(u8g2, M1_DISP_FUNC_MENU_FONT_N);
	if (line1 != NULL)
		m1_draw_text(u8g2, text_x, 25, text_w, line1, TEXT_ALIGN_LEFT);
	if (line2 != NULL)
		m1_draw_text(u8g2, text_x, 34, text_w, line2, TEXT_ALIGN_LEFT);
	if (line3 != NULL)
		m1_draw_text(u8g2, text_x, 43, text_w, line3, TEXT_ALIGN_LEFT);
}

/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void m1_draw_icon(uint8_t color, u8g2_uint_t x, u8g2_uint_t y, u8g2_uint_t w, u8g2_uint_t h, const uint8_t *bitmap)
{
	if (bitmap == NULL)
		return;

	u8g2_SetDrawColor(&m1_u8g2, color);
	u8g2_FirstPage(&m1_u8g2);
	do
	{
		u8g2_DrawXBMP(&m1_u8g2, x, y, w, h, bitmap);
	} while (u8g2_NextPage(&m1_u8g2));
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
}

/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void m1_draw_text(u8g2_t *u8g2,
                 int x, int y,
                 int max_width,
                 const char *text,
                 S_M1_text_align_t align)
{
    if (text == NULL) return;

    uint16_t text_width = u8g2_GetStrWidth(u8g2, text);
    int draw_x = x;

    switch (align)
    {
        case TEXT_ALIGN_CENTER:
            draw_x = x + (max_width - text_width) / 2;
            break;

        case TEXT_ALIGN_RIGHT:
            draw_x = x + (max_width - text_width);
            break;

        default: // TEXT_ALIGN_LEFT
            draw_x = x;
            break;
    }

    u8g2_DrawStr(u8g2, draw_x, y, text);
}

/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void m1_draw_text_box(u8g2_t *u8g2,
                    int x, int y,
                    int max_width,          // 텍스트 박스 폭(px)
                    int line_height,        // 줄 간격(px)
                    const char *text,
                    S_M1_text_align_t align)
{
    const char *p = text;
    char line[128];
    int line_len = 0;

    while (*p)
    {
        /* -----------------------------
         * 1) '\n' 이 나오면 강제 줄바꿈
         * ----------------------------- */
        if (*p == '\n')
        {
            // 지금까지 쌓인 line 출력 (있다면)
            if (line_len > 0)
            {
                line[line_len] = '\0';

                int draw_x = x;
                uint16_t line_width = u8g2_GetStrWidth(u8g2, line);

                switch (align)
                {
                    case TEXT_ALIGN_CENTER:
                        draw_x = x + (max_width - line_width) / 2;
                        break;

                    case TEXT_ALIGN_RIGHT:
                        draw_x = x + (max_width - line_width);
                        break;

                    default: // LEFT
                        draw_x = x;
                        break;
                }

                u8g2_DrawStr(u8g2, draw_x, y, line);
                y += line_height;
            }

            // 새 줄 준비
            line_len = 0;
            p++;            // '\n' 문자 소비
            continue;
        }

        /* -----------------------------
         * 2) 일반 문자 처리
         * ----------------------------- */
        line[line_len] = *p;
        line[line_len + 1] = '\0';

        uint16_t w = u8g2_GetStrWidth(u8g2, line);

        // 폭 초과 → 이전 글자까지 출력
        if (w > max_width)
        {
            line[line_len] = '\0';  // 마지막 한 글자 빼고 확정된 라인 출력

            int draw_x = x;
            uint16_t line_width = u8g2_GetStrWidth(u8g2, line);

            // 정렬 처리
            switch (align)
            {
                case TEXT_ALIGN_CENTER:
                    draw_x = x + (max_width - line_width) / 2;
                    break;

                case TEXT_ALIGN_RIGHT:
                    draw_x = x + (max_width - line_width);
                    break;

                default: // LEFT
                    draw_x = x;
                    break;
            }

            u8g2_DrawStr(u8g2, draw_x, y, line);
            y += line_height;

            // 다음 라인 준비 (현재 글자 다시 시작)
            line_len = 0;
            continue;   // *p 는 다시 처리
        }

        line_len++;
        p++;
    }

    // 마지막 라인 출력
    if (line_len > 0)
    {
        line[line_len] = '\0';
        int draw_x = x;

        uint16_t line_width = u8g2_GetStrWidth(u8g2, line);

        switch (align)
        {
            case TEXT_ALIGN_CENTER:
                draw_x = x + (max_width - line_width) / 2;
                break;

            case TEXT_ALIGN_RIGHT:
                draw_x = x + (max_width - line_width);
                break;

            default:
                draw_x = x;
                break;
        }

        u8g2_DrawStr(u8g2, draw_x, y, line);
    }
} // void m1_draw_text_box(...)



/*============================================================================*/
/**
  * @brief
  * @param  None
  * @retval None
  */
/*============================================================================*/
void m1_gui_let_update_fw(void)
{
    /* Graphic work starts here */
    u8g2_FirstPage(&m1_u8g2); // This call required for page drawing in mode 1
    do
    {
    	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
		u8g2_DrawXBMP(&m1_u8g2, 40, 2, 48, 48, fw_update_48x48); // draw firmware update icon
		u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
		u8g2_DrawStr(&m1_u8g2, 10, 62, "Pls update firmware");
    } while (u8g2_NextPage(&m1_u8g2));
} // void m1_gui_let_update_fw(void)
