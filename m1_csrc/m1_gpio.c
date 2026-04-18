/* See COPYING.txt for license details. */

/*
*
*  m1_gpio.c
*
*  M1 GPIO functions
*
* M1 Project
*
*/

/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "stm32h5xx_hal.h"
#include "main.h"
#include "m1_gpio.h"

/*************************** D E F I N E S ************************************/

#define M1_LOGDB_TAG	"GPIO"

#define THIS_LCD_MENU_TEXT_FIRST_ROW_Y			11
#define THIS_LCD_MENU_TEXT_FRAME_FIRST_ROW_Y	1
#define THIS_LCD_MENU_TEXT_ROW_SPACE			10

/* Layout for the new 2-item scrollable menu */
#define THIS_GPIO_WINDOW_SIZE   2
#define THIS_GPIO_ROW_H         12
#define THIS_GPIO_ROW_X         6
#define THIS_GPIO_ROW_W         113
#define THIS_GPIO_LIST_TOP_Y    13
#define THIS_GPIO_SCROLLBAR_X   121
#define THIS_GPIO_SCROLLBAR_W   5

//************************** C O N S T A N T **********************************/

const char *m1_ext_gpio_label[M1_EXT_GPIO_LIST_N] = {	"Power 3.3V",
														"Power 5.0V",
														"",
														"Pin PE2",
														"Pin PE4",
														"Pin PE5",
														"Pin PE6",
														"Pin PD12",
														"Pin PD13",
														"Pin PA14",
														"Pin PA13",
														/*"Pin PA9",*/
														/*"Pin PA10",*/
														"Pin PC2",
														"Pin PC3",
														"Pin PD0",
														"Pin PD1"
													};

//************************** S T R U C T U R E S *******************************

/***************************** V A R I A B L E S ******************************/

S_GPIO_IO_t m1_ext_gpio[M1_EXT_GPIO_LIST_N] = {	{.gpio_port = EN_EXT_3V3_GPIO_Port, .gpio_pin = EN_EXT_3V3_Pin},
												{.gpio_port = EN_EXT_5V_GPIO_Port, .gpio_pin = EN_EXT_5V_Pin},
												{.gpio_port = EN_EXT2_5V_GPIO_Port, .gpio_pin = EN_EXT2_5V_Pin},
												{.gpio_port = PE2_GPIO_Port, .gpio_pin = PE2_Pin},
												{.gpio_port = PE2_GPIO_Port, .gpio_pin = PE4_Pin},
												{.gpio_port = PE2_GPIO_Port, .gpio_pin = PE5_Pin},
												{.gpio_port = PE2_GPIO_Port, .gpio_pin = PE6_Pin},
												{.gpio_port = PD12_GPIO_Port, .gpio_pin = PD12_Pin},
												{.gpio_port = PD13_GPIO_Port, .gpio_pin = PD13_Pin},
												{.gpio_port = SWCLK_GPIO_Port, .gpio_pin = SWCLK_Pin},
												{.gpio_port = SWDIO_GPIO_Port, .gpio_pin = SWDIO_Pin},
												/*{.gpio_port = UART_1_TX_GPIO_Port, .gpio_pin = UART_1_TX_Pin},*/
												/*{.gpio_port = UART_1_RX_GPIO_Port, .gpio_pin = UART_1_RX_Pin},*/
												{.gpio_port = PC2_GPIO_Port, .gpio_pin = PC2_Pin},
												{.gpio_port = PC3_GPIO_Port, .gpio_pin = PC3_Pin},
												{.gpio_port = PD0_GPIO_Port, .gpio_pin = PD0_Pin},
												{.gpio_port = PD1_GPIO_Port, .gpio_pin = PD1_Pin}
											};

static uint8_t m1_ext_gpio_stat[M1_EXT_GPIO_LIST_N] = {0};
static uint8_t m1_ext_gpio_id = M1_EXT_GPIO_FIRST_ID; // Default to the first ext. GPIO [PE2_GPIO_Port, PE2_Pin]

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

void menu_gpio_init(void);
void menu_gpio_exit(void);

void gpio_manual_control(void);
void gpio_5v_on_gpio(void);
void gpio_3_3v_on_gpio(void);
void gpio_usb_uart_bridge(void);
void ext_power_5V_set(uint8_t set_mode);
void ext_power_3V_set(uint8_t set_mode);
void gpio_gui_update(const S_M1_Menu_t *phmenu, uint8_t sel_item);
void gpio_xkey_handler(S_M1_Key_Event event, uint8_t button_id, uint8_t sel_item);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/******************************************************************************/
/**
  * @brief Initializes display for this sub-menu item.
  * @param
  * @retval
  */
/******************************************************************************/
void menu_gpio_init(void)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};
    uint8_t i;

    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    for(i=0; i<M1_EXT_GPIO_LIST_N; i++)
    {
    	if ( i >= M1_EXT_GPIO_FIRST_ID ) // Do not reinitialize power control pins
    	{
    		GPIO_InitStruct.Pin = m1_ext_gpio[i].gpio_pin;
    		HAL_GPIO_Init(m1_ext_gpio[i].gpio_port, &GPIO_InitStruct);
    	}
    	HAL_GPIO_WritePin(m1_ext_gpio[i].gpio_port, m1_ext_gpio[i].gpio_pin, GPIO_PIN_RESET);
    	m1_ext_gpio_stat[i] = 0;
    }

    m1_ext_gpio_id = M1_EXT_GPIO_FIRST_ID; // Default to the first ext. GPIO [PE2_GPIO_Port, PE2_Pin]
} // void menu_gpio_init(void)


/******************************************************************************/
/**
  * @brief Exits this sub-menu and return to the upper level menu.
  * @param
  * @retval
  */
/******************************************************************************/
void menu_gpio_exit(void)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};
    uint8_t i;

    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    for(i=M1_EXT_GPIO_FIRST_ID; i<M1_EXT_GPIO_LIST_N; i++)
    {
    	GPIO_InitStruct.Pin = m1_ext_gpio[i].gpio_pin;
    	HAL_GPIO_Init(m1_ext_gpio[i].gpio_port, &GPIO_InitStruct);
    }

    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Alternate = GPIO_AF0_SWJ;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN; // Pulldown for SWCLK
    GPIO_InitStruct.Pin = SWCLK_Pin;
    HAL_GPIO_Init(SWCLK_GPIO_Port, &GPIO_InitStruct); // SWCLK
    GPIO_InitStruct.Pull = GPIO_PULLUP; // Pullup for SWDIO
    GPIO_InitStruct.Pin = SWDIO_Pin;
    HAL_GPIO_Init(SWDIO_GPIO_Port, &GPIO_InitStruct); // SWDIO

    for(i=0; i<M1_EXT_GPIO_FIRST_ID; i++) // Reset power control pins
    {
    	HAL_GPIO_WritePin(m1_ext_gpio[i].gpio_port, m1_ext_gpio[i].gpio_pin, GPIO_PIN_RESET);
    }
} // void menu_gpio_exit(void)



/******************************************************************************/
/**
  * @brief
  * @param
  * @retval
  */
/******************************************************************************/
void gpio_manual_control(void)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};
	uint8_t prn_name[GUI_DISP_LINE_LEN_MAX + 1] = {0};

    m1_ext_gpio_stat[m1_ext_gpio_id] ^= 1; // Toggle

    HAL_GPIO_WritePin(m1_ext_gpio[m1_ext_gpio_id].gpio_port, m1_ext_gpio[m1_ext_gpio_id].gpio_pin, m1_ext_gpio_stat[m1_ext_gpio_id]);

	sprintf(prn_name, "%s: %s", m1_ext_gpio_label[m1_ext_gpio_id], (m1_ext_gpio_stat[m1_ext_gpio_id]==1)?"ON":"OFF");
	m1_info_box_display_draw(INFO_BOX_ROW_1, prn_name);
	u8g2_NextPage(&m1_u8g2); // Update display RAM

	xQueueReset(main_q_hdl); // Reset main q before return
} // void gpio_manual_control(void)



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void gpio_3_3v_on_gpio(void)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};
	uint8_t prn_name[GUI_DISP_LINE_LEN_MAX + 1] = {0};

    m1_ext_gpio_stat[0] ^= 1; // Toggle
    if ( m1_ext_gpio_stat[0] )
    {
    	m1_ext_gpio_stat[1] = 0; // 3.3V and 5.0V must not be turned ON at the same time
        HAL_GPIO_WritePin(m1_ext_gpio[1].gpio_port, m1_ext_gpio[1].gpio_pin, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(m1_ext_gpio[2].gpio_port, m1_ext_gpio[2].gpio_pin, GPIO_PIN_RESET);
    }

    HAL_GPIO_WritePin(m1_ext_gpio[0].gpio_port, m1_ext_gpio[0].gpio_pin, m1_ext_gpio_stat[0]);

	sprintf(prn_name, "%s: %s", m1_ext_gpio_label[0], (m1_ext_gpio_stat[0]==1)?"ON":"OFF");
	m1_info_box_display_draw(INFO_BOX_ROW_1, prn_name);
	u8g2_NextPage(&m1_u8g2); // Update display RAM

	xQueueReset(main_q_hdl); // Reset main q before return
} // void gpio_3_3v_on_gpio(void)



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void gpio_5v_on_gpio(void)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};
	uint8_t prn_name[GUI_DISP_LINE_LEN_MAX + 1] = {0};

    m1_ext_gpio_stat[1] ^= 1; // Toggle
    if ( m1_ext_gpio_stat[1] )
    {
    	m1_ext_gpio_stat[0] = 0; // 3.3V and 5.0V must not be turned ON at the same time
    	HAL_GPIO_WritePin(m1_ext_gpio[0].gpio_port, m1_ext_gpio[0].gpio_pin, GPIO_PIN_RESET);
    }

    HAL_GPIO_WritePin(m1_ext_gpio[1].gpio_port, m1_ext_gpio[1].gpio_pin, m1_ext_gpio_stat[1]);
    HAL_GPIO_WritePin(m1_ext_gpio[2].gpio_port, m1_ext_gpio[2].gpio_pin, m1_ext_gpio_stat[1]);

	sprintf(prn_name, "%s: %s", m1_ext_gpio_label[1], (m1_ext_gpio_stat[1]==1)?"ON":"OFF");
	m1_info_box_display_draw(INFO_BOX_ROW_1, prn_name);
	u8g2_NextPage(&m1_u8g2); // Update display RAM

	xQueueReset(main_q_hdl); // Reset main q before return
} // void gpio_5v_on_gpio(void)



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void gpio_usb_uart_bridge(void)
{
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	BaseType_t ret;

	m1_gui_let_update_fw();

	while (1 ) // Main loop of this task
	{
		;
		; // Do other parts of this task here
		;

		// Wait for the notification from button_event_handler_task to subfunc_handler_task.
		// This task is the sub-task of subfunc_handler_task.
		// The notification is given in the form of an item in the main queue.
		// So let read the main queue.
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
	} // while (1 ) // Main loop of this task

} // void gpio_usb_uart_bridge(void)



/******************************************************************************/
/**
  * @brief
  * @param None
  * @retval None
  */
/******************************************************************************/
void ext_power_5V_set(uint8_t set_mode)
{
	HAL_GPIO_WritePin(EN_EXT_5V_GPIO_Port, EN_EXT_5V_Pin, set_mode);
	HAL_GPIO_WritePin(EN_EXT2_5V_GPIO_Port, EN_EXT2_5V_Pin, set_mode);
} // void ext_power_5V_set(uint8_t set_mode)


/******************************************************************************/
/**
  * @brief
  * @param None
  * @retval None
  */
/******************************************************************************/
void ext_power_3V_set(uint8_t set_mode)
{
	  HAL_GPIO_WritePin(EN_EXT_3V3_GPIO_Port, EN_EXT_3V3_Pin, set_mode);
} // void ext_power_5V_set(uint8_t set_mode)



/******************************************************************************/
/**
  * @brief
  * @param None
  * @retval None
  */
/******************************************************************************/
void gpio_gui_update(const S_M1_Menu_t *phmenu, uint8_t sel_item)
{
	uint8_t n_items, row, top_row, bottom_row;
	uint8_t row_y, row_text_y;
	uint8_t prn_name[GUI_DISP_LINE_LEN_MAX + 1] = {0};

	n_items = phmenu->num_submenu_items;

	/* Scroll window: keep sel_item visible inside a window of size
	 * THIS_GPIO_WINDOW_SIZE (2). */
	if (n_items <= THIS_GPIO_WINDOW_SIZE)
		top_row = 0;
	else if (sel_item == 0)
		top_row = 0;
	else if (sel_item >= n_items - 1)
		top_row = n_items - THIS_GPIO_WINDOW_SIZE;
	else
		top_row = sel_item - (THIS_GPIO_WINDOW_SIZE - 1);

	bottom_row = top_row + THIS_GPIO_WINDOW_SIZE;
	if (bottom_row > n_items)
		bottom_row = n_items;

	/* Graphic work starts here */
	m1_u8g2_firstpage();
	do
	{
		/* Shared header (battery + SD) */
		m1_draw_header_bar(&m1_u8g2, "GPIO", NULL);

		/* Scrollable 2-row list */
		u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
		for (row = top_row; row < bottom_row; row++)
		{
			row_y = THIS_GPIO_LIST_TOP_Y + ((row - top_row) * THIS_GPIO_ROW_H);
			row_text_y = row_y + 9;

			if (row == sel_item)
			{
				u8g2_DrawBox(&m1_u8g2, THIS_GPIO_ROW_X, row_y,
				             THIS_GPIO_ROW_W, THIS_GPIO_ROW_H - 1);
				u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
				u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_B);
				u8g2_DrawStr(&m1_u8g2, THIS_GPIO_ROW_X + 4, row_text_y,
				             phmenu->submenu[row]->title);
				if (row == 0) // GPIO Control — show pin-cycle arrows on the row
				{
					u8g2_DrawXBMP(&m1_u8g2, THIS_GPIO_ROW_X + 78, row_y + 1,
					              10, 10, arrowleft_10x10);
					u8g2_DrawXBMP(&m1_u8g2, THIS_GPIO_ROW_X + 91, row_y + 1,
					              10, 10, arrowright_10x10);
				}
				u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
				u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
			}
			else
			{
				u8g2_DrawFrame(&m1_u8g2, THIS_GPIO_ROW_X, row_y,
				               THIS_GPIO_ROW_W, THIS_GPIO_ROW_H - 1);
				u8g2_DrawStr(&m1_u8g2, THIS_GPIO_ROW_X + 4, row_text_y,
				             phmenu->submenu[row]->title);
			}
		}

		/* Scrollbar (only when there is more to scroll) */
		if (n_items > THIS_GPIO_WINDOW_SIZE)
		{
			uint8_t track_y = THIS_GPIO_LIST_TOP_Y;
			uint8_t track_h = (THIS_GPIO_WINDOW_SIZE * THIS_GPIO_ROW_H) - 1;
			uint8_t handle_h, handle_y;
			u8g2_DrawFrame(&m1_u8g2, THIS_GPIO_SCROLLBAR_X, track_y,
			               THIS_GPIO_SCROLLBAR_W, track_h);
			handle_h = (uint8_t)((track_h * THIS_GPIO_WINDOW_SIZE) / n_items);
			if (handle_h < 4) handle_h = 4;
			handle_y = track_y +
			           (uint8_t)(((track_h - handle_h) * top_row) /
			                     (n_items - THIS_GPIO_WINDOW_SIZE));
			u8g2_DrawBox(&m1_u8g2, THIS_GPIO_SCROLLBAR_X + 1, handle_y + 1,
			             THIS_GPIO_SCROLLBAR_W - 2, handle_h - 2);
		}

		/* Detail frame for currently-selected row */
		m1_draw_content_frame(&m1_u8g2, 2, 38, 124, 13);

		switch (sel_item)
		{
			case 0: // GPIO (cycled pin state)
				sprintf(prn_name, "%s: %s",
				        m1_ext_gpio_label[m1_ext_gpio_id],
				        (m1_ext_gpio_stat[m1_ext_gpio_id] == 1) ? "ON" : "OFF");
				m1_draw_text(&m1_u8g2, 6, 47, 116, prn_name, TEXT_ALIGN_LEFT);
				break;

			case 1: // Power 3.3V
				sprintf(prn_name, "%s: %s", m1_ext_gpio_label[0],
				        (m1_ext_gpio_stat[0] == 1) ? "ON" : "OFF");
				m1_draw_text(&m1_u8g2, 6, 47, 116, prn_name, TEXT_ALIGN_LEFT);
				break;

			case 2: // Power 5.0V
				sprintf(prn_name, "%s: %s", m1_ext_gpio_label[1],
				        (m1_ext_gpio_stat[1] == 1) ? "ON" : "OFF");
				m1_draw_text(&m1_u8g2, 6, 47, 116, prn_name, TEXT_ALIGN_LEFT);
				break;

			case 3:
				m1_draw_text(&m1_u8g2, 6, 47, 116, "Please update firmware!",
				             TEXT_ALIGN_LEFT);
				break;

			default:
				break;
		}

		m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Back", "Toggle",
		                   arrowright_8x8);
	} while (m1_u8g2_nextpage());
} // void gpio_gui_update(const S_M1_Menu_t *phmenu, uint8_t sel_item)



/******************************************************************************/
/**
  * @brief
  * @param None
  * @retval None
  */
/******************************************************************************/
void gpio_xkey_handler(S_M1_Key_Event event, uint8_t button_id, uint8_t sel_item)
{
	uint8_t prn_name[GUI_DISP_LINE_LEN_MAX + 1] = {0};

	if ( sel_item != 0) // Not the index of GPIO Control
		return;

	if ( event==BUTTON_EVENT_CLICK )
	{
		if ( button_id==BUTTON_LEFT_KP_ID ) // Left arrow key
		{
			m1_ext_gpio_id--;
			if ( m1_ext_gpio_id < M1_EXT_GPIO_FIRST_ID )
				m1_ext_gpio_id = M1_EXT_GPIO_LIST_N - 1;
		} // if ( button_id==BUTTON_LEFT_KP_ID )
		else if ( button_id==BUTTON_RIGHT_KP_ID ) // Right arrow key
		{
			m1_ext_gpio_id++;
			if ( m1_ext_gpio_id >= M1_EXT_GPIO_LIST_N )
				m1_ext_gpio_id = M1_EXT_GPIO_FIRST_ID;
		}

		sprintf(prn_name, "%s: %s", m1_ext_gpio_label[m1_ext_gpio_id], (m1_ext_gpio_stat[m1_ext_gpio_id]==1)?"ON":"OFF");
    	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
    	u8g2_DrawBox(&m1_u8g2, 4, 45, 120, 15);
    	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
		m1_draw_text(&m1_u8g2, 8, 55, 114, prn_name, TEXT_ALIGN_LEFT);

		m1_u8g2_nextpage(); // Update LCD display RAM
	} // if ( event==BUTTON_EVENT_CLICK )
} // void gpio_xkey_handler(S_M1_Key_Event event, uint8_t button_id, uint8_t)
