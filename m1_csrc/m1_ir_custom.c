/* See COPYING.txt for license details. */

/*
*
*  m1_ir_custom.c
*
*  Custom (user-built) IR remotes: create a named remote, learn+name buttons,
*  replay from a scrolling button list, and edit (rename/delete) buttons.
*  Complements m1_ir_universal.c (IRDB browser) and m1_infrared.c (RX/TX core).
*
* M1 Project
*
*/

/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "m1_compile_cfg.h"

#ifdef M1_APP_FILE_IMPORT_ENABLE

#include "stm32h5xx_hal.h"
#include "main.h"
#include "m1_ir_custom.h"
#include "m1_ir_universal.h"
#include "m1_system.h"
#include "m1_display.h"
#include "m1_lcd.h"
#include "m1_tasks.h"
#include "m1_virtual_kb.h"
#include "m1_infrared.h"
#include "m1_led_indicator.h"
#include "m1_lp5814.h"
#include "flipper_file.h"
#include "flipper_ir.h"
#include "ff.h"

/*************************** D E F I N E S ************************************/

/* Root directory for user-built custom remotes on the SD card. */
#define IR_CUSTOM_DIR           "0:/IR"
#define IR_CUSTOM_NAME_MAX_LEN  32
#define IR_CUSTOM_PATH_MAX_LEN  256
#define IR_CUSTOM_MAX_DEDUP     99

/* My-Remotes manager list geometry (mirrors m1_ir_universal draw_list_screen). */
#define IR_CUSTOM_MAX_REMOTES   24
#define IR_CUSTOM_MAX_BUTTONS   64      /* matches IR_UNIVERSAL_MAX_CMDS */
#define IR_CUSTOM_DISP_NAME_LEN 32
#define IR_CUSTOM_LIST_VISIBLE  4
#define IR_CUSTOM_LIST_HEADER_H 12
#define IR_CUSTOM_LIST_ITEM_H   9
#define IR_CUSTOM_LIST_START_Y  (IR_CUSTOM_LIST_HEADER_H + 2)

#define IR_CUSTOM_NEW_LABEL     "[+ New Remote]"

/* Raw learn fallback (entered only when IRMP cannot decode the signal). */
#define IR_LEARN_GAP_MS         200      /* no edge for this long -> end of frame */
#define IR_RAW_GAP_US           12000    /* a space this long = inter-frame gap  */
#define IR_RAW_MIN_SAMPLES      8        /* fewer than this = noise, discard      */
#define IR_RAW_FREQ_DEFAULT     38000
#define IR_RAW_DUTY_DEFAULT     0.33f

typedef enum {
	IR_CAP_NONE = 0,
	IR_CAP_PARSED,
	IR_CAP_RAW
} ir_cap_state_t;

//****************************** V A R I A B L E S *****************************/

static char     s_remote_names[IR_CUSTOM_MAX_REMOTES][IR_CUSTOM_DISP_NAME_LEN];
static uint16_t s_remote_count;

/* Button-name cache for the edit list (one remote at a time). */
static char     s_button_names[IR_CUSTOM_MAX_BUTTONS][IR_CUSTOM_DISP_NAME_LEN];
static uint16_t s_button_count;

/* Raw-learn accumulator kept out of the task stack (one learn at a time). */
static flipper_ir_signal_t s_learn_raw;

/* Scratch signal for reading button names off the task stack (~2 KB). */
static flipper_ir_signal_t s_scan_sig;

/* Per-remote action menu. */
static const char *const s_remote_menu_items[] = { "Play Buttons", "Learn Button", "Edit Buttons" };
#define IR_CUSTOM_REMOTE_MENU_COUNT \
	((uint8_t)(sizeof(s_remote_menu_items) / sizeof(s_remote_menu_items[0])))

/* Per-button action menu. */
static const char *const s_button_action_items[] = { "Rename", "Delete" };
#define IR_CUSTOM_BTN_ACTION_COUNT \
	((uint8_t)(sizeof(s_button_action_items) / sizeof(s_button_action_items[0])))

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

static void        ir_custom_sanitize_name(const char *in, char *out, size_t out_len);
static bool        ir_custom_unique_path(const char *name, char *out, size_t out_len);
static bool        ir_custom_create_empty(const char *name, char *out_path, size_t out_len);
static void        ir_custom_draw_result(bool ok, const char *name);
static void        ir_custom_wait_key(void);
static void        ir_custom_create_flow(void);
static void        ir_custom_scan_remotes(void);
static const char *ir_custom_item_text(uint16_t idx);
static void        ir_custom_draw_list(uint16_t selection, uint16_t total);
static bool        ir_custom_append_parsed(const char *path, const IRMP_DATA *data);
static bool        ir_custom_append_raw(const char *path, flipper_ir_signal_t *sig);
static void        ir_custom_draw_remote_menu(const char *name, uint8_t selection);
static void        ir_custom_draw_learn(ir_cap_state_t state, const IRMP_DATA *data, uint16_t raw_count);
static void        ir_custom_draw_message(const char *l1, const char *l2);
static void        ir_custom_learn_button(const char *path);
static void        ir_custom_scan_buttons(const char *path);
static void        ir_custom_draw_button_list(const char *name, uint16_t selection, uint16_t total);
static void        ir_custom_draw_action_menu(const char *name, uint8_t selection);
static bool        ir_custom_rename_button(const char *path, uint16_t index);
static bool        ir_custom_confirm(const char *l1, const char *l2);
static void        ir_custom_button_action(const char *path, const char *rname, uint16_t index);
static void        ir_custom_edit_buttons(const char *path, const char *name);
static void        ir_custom_open_remote(const char *path, const char *name);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/*============================================================================*/
/**
 * @brief  Sanitize a user-entered name into a FAT-legal filename component.
 *         Path separators, reserved characters, and control characters are
 *         replaced with '_'; trailing spaces/dots are trimmed (FAT dislikes
 *         them). Falls back to "Remote" if nothing usable remains.
 * @param  in       raw name (may be NULL)
 * @param  out      output buffer
 * @param  out_len  size of out (must be > 0)
 */
static void ir_custom_sanitize_name(const char *in, char *out, size_t out_len)
{
	size_t j = 0;

	if (out == NULL || out_len == 0)
		return;

	if (in != NULL)
	{
		size_t i;
		for (i = 0; in[i] != '\0' && j < out_len - 1; i++)
		{
			unsigned char c = (unsigned char)in[i];

			if (c < 0x20 || c == '/' || c == '\\' || c == ':' || c == '*' ||
			    c == '?'  || c == '"' || c == '<'  || c == '>' || c == '|')
				out[j++] = '_';
			else
				out[j++] = (char)c;
		}
	}
	out[j] = '\0';

	/* Trim trailing spaces and dots. */
	while (j > 0 && (out[j - 1] == ' ' || out[j - 1] == '.'))
		out[--j] = '\0';

	if (j == 0)
	{
		strncpy(out, "Remote", out_len - 1);
		out[out_len - 1] = '\0';
	}
}

/*============================================================================*/
/**
 * @brief  Build a unique "0:/IR/<name>.ir" path, appending _1, _2, ... on
 *         collision with an existing file.
 * @param  name     sanitized base name
 * @param  out      output path buffer
 * @param  out_len  size of out
 * @return true if a free path was produced
 */
static bool ir_custom_unique_path(const char *name, char *out, size_t out_len)
{
	FILINFO  fno;
	int      n;
	unsigned i;

	/* Ensure the destination directory exists (ignore "already exists"). */
	f_mkdir(IR_CUSTOM_DIR);

	n = snprintf(out, out_len, "%s/%s.ir", IR_CUSTOM_DIR, name);
	if (n < 0 || n >= (int)out_len)
		return false;
	if (f_stat(out, &fno) != FR_OK)
		return true;   /* no such file -> unique */

	for (i = 1; i <= IR_CUSTOM_MAX_DEDUP; i++)
	{
		n = snprintf(out, out_len, "%s/%s_%u.ir", IR_CUSTOM_DIR, name, i);
		if (n < 0 || n >= (int)out_len)
			return false;
		if (f_stat(out, &fno) != FR_OK)
			return true;
	}

	return false;   /* too many name collisions */
}

/*============================================================================*/
/**
 * @brief  Create an empty (header-only, zero-signal) custom remote file.
 * @param  name      sanitized remote name
 * @param  out_path  receives the full path that was written
 * @param  out_len   size of out_path
 * @return true on success
 */
static bool ir_custom_create_empty(const char *name, char *out_path, size_t out_len)
{
	flipper_file_t ff;

	if (!ir_custom_unique_path(name, out_path, out_len))
		return false;

	if (!ff_open_write(&ff, out_path))
		return false;

	if (!flipper_ir_write_header(&ff))
	{
		ff_close(&ff);
		return false;
	}

	ff_close(&ff);
	return true;
}

/*============================================================================*/
/**
 * @brief  Draw the create-result screen (success shows the remote name).
 */
static void ir_custom_draw_result(bool ok, const char *name)
{
	m1_u8g2_firstpage();

	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);

	u8g2_SetFont(&m1_u8g2, M1_DISP_RUN_MENU_FONT_B);
	u8g2_DrawStr(&m1_u8g2, 2, 10, "Create Remote");
	u8g2_DrawHLine(&m1_u8g2, 0, 12, 128);

	u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
	if (ok)
	{
		u8g2_DrawStr(&m1_u8g2, 4, 28, "Created:");
		u8g2_DrawStr(&m1_u8g2, 4, 40, (name != NULL) ? name : "");
	}
	else
	{
		u8g2_DrawStr(&m1_u8g2, 4, 34, "Create failed");
	}

	m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Back", "", arrowright_8x8);

	m1_u8g2_nextpage();
}

/*============================================================================*/
/**
 * @brief  Block until a confirm/cancel key is pressed, then return to caller.
 */
static void ir_custom_wait_key(void)
{
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t       q_item;
	BaseType_t          ret;

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

		if (this_button_status.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK ||
		    this_button_status.event[BUTTON_LEFT_KP_ID] == BUTTON_EVENT_CLICK ||
		    this_button_status.event[BUTTON_OK_KP_ID]   == BUTTON_EVENT_CLICK)
		{
			xQueueReset(main_q_hdl);
			break;
		}
	}
}

/*============================================================================*/
/**
 * @brief  Prompt for a name and create a new empty remote. Escaping the
 *         keyboard writes nothing.
 */
static void ir_custom_create_flow(void)
{
	char    default_name[IR_CUSTOM_NAME_MAX_LEN] = "Remote";
	char    entered[IR_CUSTOM_NAME_MAX_LEN];
	char    name[IR_CUSTOM_NAME_MAX_LEN];
	char    path[IR_CUSTOM_PATH_MAX_LEN];
	uint8_t got;
	bool    ok;

	entered[0] = '\0';

	got = m1_vkb_get_filename("Remote name:", default_name, entered);
	if (got == 0)
		return;   /* cancelled — nothing written */

	ir_custom_sanitize_name(entered, name, sizeof(name));
	ok = ir_custom_create_empty(name, path, sizeof(path));

	ir_custom_draw_result(ok, name);
	ir_custom_wait_key();
}

/*============================================================================*/
/**
 * @brief  Scan 0:/IR/ for user-built remote files (*.ir at the root, not the
 *         shipped category sub-directories) into s_remote_names[] (extension
 *         stripped for display).
 */
static void ir_custom_scan_remotes(void)
{
	DIR     dir;
	FILINFO fno;
	FRESULT res;

	s_remote_count = 0;

	f_mkdir(IR_CUSTOM_DIR);

	res = f_opendir(&dir, IR_CUSTOM_DIR);
	if (res != FR_OK)
		return;

	while (s_remote_count < IR_CUSTOM_MAX_REMOTES)
	{
		size_t len;
		size_t copy;

		res = f_readdir(&dir, &fno);
		if (res != FR_OK || fno.fname[0] == '\0')
			break;

		/* Skip hidden entries and category sub-directories. */
		if (fno.fname[0] == '.')
			continue;
		if (fno.fattrib & AM_DIR)
			continue;

		/* Only ".ir" files. */
		len = strlen(fno.fname);
		if (len < 4 || strcmp(&fno.fname[len - 3], ".ir") != 0)
			continue;

		/* Store the name without the ".ir" extension for display. */
		copy = len - 3;
		if (copy > IR_CUSTOM_DISP_NAME_LEN - 1)
			copy = IR_CUSTOM_DISP_NAME_LEN - 1;
		memcpy(s_remote_names[s_remote_count], fno.fname, copy);
		s_remote_names[s_remote_count][copy] = '\0';
		s_remote_count++;
	}

	f_closedir(&dir);
}

/*============================================================================*/
/**
 * @brief  Text for manager list item idx (0 = the "new remote" action).
 */
static const char *ir_custom_item_text(uint16_t idx)
{
	if (idx == 0)
		return IR_CUSTOM_NEW_LABEL;
	return s_remote_names[idx - 1];
}

/*============================================================================*/
/**
 * @brief  Draw the My-Remotes manager list ("[+ New Remote]" then remotes).
 * @param  selection  highlighted item index
 * @param  total      total item count (remotes + 1)
 */
static void ir_custom_draw_list(uint16_t selection, uint16_t total)
{
	uint16_t start_idx;
	uint16_t visible;
	uint16_t i;
	uint8_t  y;

	if (selection < IR_CUSTOM_LIST_VISIBLE)
		start_idx = 0;
	else
		start_idx = selection - IR_CUSTOM_LIST_VISIBLE + 1;

	visible = total - start_idx;
	if (visible > IR_CUSTOM_LIST_VISIBLE)
		visible = IR_CUSTOM_LIST_VISIBLE;

	m1_u8g2_firstpage();
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);

	u8g2_SetFont(&m1_u8g2, M1_DISP_RUN_MENU_FONT_B);
	u8g2_DrawStr(&m1_u8g2, 2, 10, "Custom Remotes");
	u8g2_DrawHLine(&m1_u8g2, 0, IR_CUSTOM_LIST_HEADER_H, 128);

	u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
	for (i = 0; i < visible; i++)
	{
		uint16_t idx = start_idx + i;
		y = IR_CUSTOM_LIST_START_Y + (i * IR_CUSTOM_LIST_ITEM_H);

		if (idx == selection)
		{
			u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
			u8g2_DrawBox(&m1_u8g2, 0, y, 128, IR_CUSTOM_LIST_ITEM_H);
			u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
			u8g2_DrawStr(&m1_u8g2, 4, y + 8, ir_custom_item_text(idx));
			u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
		}
		else
		{
			u8g2_DrawStr(&m1_u8g2, 4, y + 8, ir_custom_item_text(idx));
		}
	}

	{
		const char *action = (selection == 0) ? "New" : "Open";
		m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Back", action, arrowright_8x8);
	}

	m1_u8g2_nextpage();
}

/*============================================================================*/
/**
 * @brief  Append a freshly-learned parsed signal to a remote file. Prompts for
 *         a button name (auto-suggested "<Proto>_<Cmd>", matches the existing
 *         learned-signal convention); escaping the keyboard aborts the append.
 * @param  path  full path to the remote's .ir file
 * @param  data  decoded IRMP signal
 * @return true if a button was appended
 */
static bool ir_custom_append_parsed(const char *path, const IRMP_DATA *data)
{
	flipper_file_t      ff;
	flipper_ir_signal_t sig;
	char                default_name[IR_CUSTOM_NAME_MAX_LEN];
	char                entered[IR_CUSTOM_NAME_MAX_LEN];
	uint8_t             got;
	bool                ok;

	snprintf(default_name, sizeof(default_name), "%s_%04X",
	         flipper_ir_irmp_to_proto(data->protocol), data->command);
	entered[0] = '\0';

	got = m1_vkb_get_filename("Button name:", default_name, entered);
	if (got == 0)
		return false;   /* cancelled the append */

	memset(&sig, 0, sizeof(sig));
	sig.type = FLIPPER_IR_SIGNAL_PARSED;
	sig.valid = true;
	ir_custom_sanitize_name(entered, sig.name, sizeof(sig.name));
	sig.parsed.protocol = data->protocol;
	sig.parsed.address  = data->address;
	sig.parsed.command  = data->command;
	sig.parsed.flags    = data->flags;

	if (!flipper_ir_open_append(&ff, path))
		return false;

	ok = flipper_ir_write_signal(&ff, &sig);
	ff_close(&ff);
	return ok;
}

/*============================================================================*/
/**
 * @brief  Append an already-accumulated raw signal to a remote file. Prompts
 *         for a button name; escaping the keyboard aborts the append.
 * @param  path  full path to the remote's .ir file
 * @param  sig   finalized raw signal (name is overwritten by the entered name)
 * @return true if a button was appended
 */
static bool ir_custom_append_raw(const char *path, flipper_ir_signal_t *sig)
{
	flipper_file_t ff;
	char           default_name[IR_CUSTOM_NAME_MAX_LEN] = "Raw";
	char           entered[IR_CUSTOM_NAME_MAX_LEN];
	uint8_t        got;
	bool           ok;

	entered[0] = '\0';

	got = m1_vkb_get_filename("Button name:", default_name, entered);
	if (got == 0)
		return false;   /* cancelled the append */

	ir_custom_sanitize_name(entered, sig->name, sizeof(sig->name));

	if (!flipper_ir_open_append(&ff, path))
		return false;

	ok = flipper_ir_write_signal(&ff, sig);
	ff_close(&ff);
	return ok;
}

/*============================================================================*/
/**
 * @brief  Draw the per-remote action menu (Play Buttons / Learn Button).
 */
static void ir_custom_draw_remote_menu(const char *name, uint8_t selection)
{
	uint8_t i;

	m1_u8g2_firstpage();
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);

	u8g2_SetFont(&m1_u8g2, M1_DISP_RUN_MENU_FONT_B);
	u8g2_DrawStr(&m1_u8g2, 2, 10, (name != NULL) ? name : "Remote");
	u8g2_DrawHLine(&m1_u8g2, 0, IR_CUSTOM_LIST_HEADER_H, 128);

	u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
	for (i = 0; i < IR_CUSTOM_REMOTE_MENU_COUNT; i++)
	{
		uint8_t y = (uint8_t)(IR_CUSTOM_LIST_START_Y + (i * IR_CUSTOM_LIST_ITEM_H));

		if (i == selection)
		{
			u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
			u8g2_DrawBox(&m1_u8g2, 0, y, 128, IR_CUSTOM_LIST_ITEM_H);
			u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
			u8g2_DrawStr(&m1_u8g2, 4, y + 8, s_remote_menu_items[i]);
			u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
		}
		else
		{
			u8g2_DrawStr(&m1_u8g2, 4, y + 8, s_remote_menu_items[i]);
		}
	}

	m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Back", "Select", arrowright_8x8);
	m1_u8g2_nextpage();
}

/*============================================================================*/
/**
 * @brief  Draw the learn screen: a prompt while waiting, the decoded
 *         protocol/address/command for a parsed capture, or a sample count for
 *         a raw (IRMP-undecodable) capture.
 */
static void ir_custom_draw_learn(ir_cap_state_t state, const IRMP_DATA *data, uint16_t raw_count)
{
	char line[24];

	m1_u8g2_firstpage();
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);

	u8g2_SetFont(&m1_u8g2, M1_DISP_RUN_MENU_FONT_B);
	u8g2_DrawStr(&m1_u8g2, 2, 10, "Learn Button");
	u8g2_DrawHLine(&m1_u8g2, 0, 12, 128);

	u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
	if (state == IR_CAP_PARSED && data != NULL)
	{
		u8g2_DrawStr(&m1_u8g2, 4, 24, "Captured:");
		u8g2_DrawStr(&m1_u8g2, 4, 34, flipper_ir_irmp_to_proto(data->protocol));
		snprintf(line, sizeof(line), "A:%04X C:%04X", data->address, data->command);
		u8g2_DrawStr(&m1_u8g2, 4, 44, line);
		m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Back", "Save", arrowright_8x8);
	}
	else if (state == IR_CAP_RAW)
	{
		u8g2_DrawStr(&m1_u8g2, 4, 24, "Captured (raw):");
		snprintf(line, sizeof(line), "%u samples", (unsigned)raw_count);
		u8g2_DrawStr(&m1_u8g2, 4, 38, line);
		m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Back", "Save", arrowright_8x8);
	}
	else
	{
		u8g2_DrawStr(&m1_u8g2, 4, 26, "Point remote at M1,");
		u8g2_DrawStr(&m1_u8g2, 4, 38, "press a button...");
		m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Back", "", arrowright_8x8);
	}
	m1_u8g2_nextpage();
}

/*============================================================================*/
/**
 * @brief  Draw a two-line message screen (returns on any key via wait_key).
 */
static void ir_custom_draw_message(const char *l1, const char *l2)
{
	m1_u8g2_firstpage();
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
	if (l1 != NULL)
		u8g2_DrawStr(&m1_u8g2, 4, 28, l1);
	if (l2 != NULL)
		u8g2_DrawStr(&m1_u8g2, 4, 40, l2);
	m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Back", "", arrowright_8x8);
	m1_u8g2_nextpage();
}

/*============================================================================*/
/**
 * @brief  Learn one button and append it to the open remote. An IRMP decode is
 *         preferred (parsed signal); when IRMP cannot decode the frame, the raw
 *         edge stream is captured instead (raw fallback) so undecodable remotes
 *         can still be stored. Reuses the IR receiver + IRMP path with LED/buzzer
 *         feedback like the stock Learn flow.
 * @param  path  full path to the remote's .ir file
 */
static void ir_custom_learn_button(const char *path)
{
	S_M1_Buttons_Status btn;
	S_M1_Main_Q_t       q_item;
	BaseType_t          ret;
	IRMP_DATA           data;
	ir_cap_state_t      cap = IR_CAP_NONE;

	infrared_decode_sys_init();
	irmp_init();
	m1_led_fast_blink(LED_BLINK_ON_RGB, LED_FASTBLINK_PWM_M, LED_FASTBLINK_ONTIME_M);

	memset(&data, 0, sizeof(data));
	flipper_ir_raw_begin(&s_learn_raw, "Raw");
	ir_custom_draw_learn(IR_CAP_NONE, NULL, 0);

	while (1)
	{
		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
		if (ret != pdTRUE)
			continue;

		if (q_item.q_evt_type == Q_EVENT_IRRED_RX)
		{
			uint32_t te  = q_item.q_data.ir_rx_data.ir_edge_te;
			uint8_t  dir = q_item.q_data.ir_rx_data.ir_edge_dir;

			irmp_data_sampler(te, dir);

			/* Prefer an IRMP decode; keep the first one and stop capturing. */
			if (cap == IR_CAP_NONE && irmp_get_data(&data))
			{
				cap = IR_CAP_PARSED;
				ir_custom_draw_learn(IR_CAP_PARSED, &data, 0);
			}
			else if (cap == IR_CAP_NONE)
			{
				/* No decode yet: accumulate the raw edge stream. The RX timer
				 * runs at 1 MHz so ir_edge_te is already microseconds; the
				 * active-low receiver makes a rising edge end a mark and a
				 * falling edge end a space. te > IRMP_TIMEOUT_TIME is the
				 * inter-frame timeout marker => end of frame. */
				bool is_mark   = (dir == EDGE_DET_RISING);
				bool frame_end = (te > IRMP_TIMEOUT_TIME);
				flipper_ir_raw_feed_result_t r =
					flipper_ir_raw_feed(&s_learn_raw, te, is_mark, frame_end,
					                    IR_RAW_MIN_SAMPLES, IR_RAW_FREQ_DEFAULT,
					                    IR_RAW_DUTY_DEFAULT);

				if (r == FLIPPER_IR_RAW_FRAME_COMPLETE)
				{
					cap = IR_CAP_RAW;
					ir_custom_draw_learn(IR_CAP_RAW, NULL, s_learn_raw.raw.sample_count);
				}
				/* FRAME_NOISE already reset the accumulator; keep waiting. */
			}
		}
		else if (q_item.q_evt_type == Q_EVENT_KEYPAD)
		{
			ret = xQueueReceive(button_events_q_hdl, &btn, 0);
			if (ret != pdTRUE)
				continue;

			if (btn.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK ||
			    btn.event[BUTTON_LEFT_KP_ID] == BUTTON_EVENT_CLICK)
			{
				m1_led_fast_blink(LED_BLINK_ON_RGB, LED_FASTBLINK_PWM_OFF, LED_FASTBLINK_ONTIME_OFF);
				infrared_decode_sys_deinit();
				xQueueReset(main_q_hdl);
				return;
			}
			else if ((btn.event[BUTTON_OK_KP_ID]    == BUTTON_EVENT_CLICK ||
			          btn.event[BUTTON_RIGHT_KP_ID]  == BUTTON_EVENT_CLICK) && cap != IR_CAP_NONE)
			{
				bool ok;

				/* Tear RX down before the keyboard drives its own event loop. */
				m1_led_fast_blink(LED_BLINK_ON_RGB, LED_FASTBLINK_PWM_OFF, LED_FASTBLINK_ONTIME_OFF);
				infrared_decode_sys_deinit();

				if (cap == IR_CAP_PARSED)
					ok = ir_custom_append_parsed(path, &data);
				else
					ok = ir_custom_append_raw(path, &s_learn_raw);

				ir_custom_draw_message(ok ? "Button added" : "Add cancelled/failed", NULL);
				ir_custom_wait_key();
				xQueueReset(main_q_hdl);
				return;
			}
		}
	}
}

/*============================================================================*/
/**
 * @brief  Read the button names of a remote into s_button_names[] (extension
 *         handled by the file layer). Uses an off-stack scratch signal.
 * @param  path  full path to the remote's .ir file
 */
static void ir_custom_scan_buttons(const char *path)
{
	flipper_file_t ff;

	s_button_count = 0;

	if (!flipper_ir_open(&ff, path))
		return;

	while (s_button_count < IR_CUSTOM_MAX_BUTTONS &&
	       flipper_ir_read_signal(&ff, &s_scan_sig))
	{
		size_t copy = strlen(s_scan_sig.name);
		if (copy > IR_CUSTOM_DISP_NAME_LEN - 1)
			copy = IR_CUSTOM_DISP_NAME_LEN - 1;
		memcpy(s_button_names[s_button_count], s_scan_sig.name, copy);
		s_button_names[s_button_count][copy] = '\0';
		s_button_count++;
	}

	ff_close(&ff);
}

/*============================================================================*/
/**
 * @brief  Draw the scrolling button list for the edit screen (title = remote
 *         name). Draws "(no buttons)" when the remote is empty.
 */
static void ir_custom_draw_button_list(const char *name, uint16_t selection, uint16_t total)
{
	uint16_t start_idx;
	uint16_t visible;
	uint16_t i;
	uint8_t  y;

	m1_u8g2_firstpage();
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);

	u8g2_SetFont(&m1_u8g2, M1_DISP_RUN_MENU_FONT_B);
	u8g2_DrawStr(&m1_u8g2, 2, 10, (name != NULL) ? name : "Edit");
	u8g2_DrawHLine(&m1_u8g2, 0, IR_CUSTOM_LIST_HEADER_H, 128);

	u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);

	if (total == 0)
	{
		u8g2_DrawStr(&m1_u8g2, 4, IR_CUSTOM_LIST_START_Y + 8, "(no buttons)");
		m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Back", "", arrowright_8x8);
		m1_u8g2_nextpage();
		return;
	}

	if (selection < IR_CUSTOM_LIST_VISIBLE)
		start_idx = 0;
	else
		start_idx = selection - IR_CUSTOM_LIST_VISIBLE + 1;

	visible = total - start_idx;
	if (visible > IR_CUSTOM_LIST_VISIBLE)
		visible = IR_CUSTOM_LIST_VISIBLE;

	for (i = 0; i < visible; i++)
	{
		uint16_t idx = start_idx + i;
		y = IR_CUSTOM_LIST_START_Y + (i * IR_CUSTOM_LIST_ITEM_H);

		if (idx == selection)
		{
			u8g2_DrawBox(&m1_u8g2, 0, y, 128, IR_CUSTOM_LIST_ITEM_H);
			u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
			u8g2_DrawStr(&m1_u8g2, 4, y + 8, s_button_names[idx]);
			u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
		}
		else
		{
			u8g2_DrawStr(&m1_u8g2, 4, y + 8, s_button_names[idx]);
		}
	}

	m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Back", "Edit", arrowright_8x8);
	m1_u8g2_nextpage();
}

/*============================================================================*/
/**
 * @brief  Draw the per-button action menu (title = button name).
 */
static void ir_custom_draw_action_menu(const char *name, uint8_t selection)
{
	uint8_t i;

	m1_u8g2_firstpage();
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);

	u8g2_SetFont(&m1_u8g2, M1_DISP_RUN_MENU_FONT_B);
	u8g2_DrawStr(&m1_u8g2, 2, 10, (name != NULL) ? name : "Button");
	u8g2_DrawHLine(&m1_u8g2, 0, IR_CUSTOM_LIST_HEADER_H, 128);

	u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
	for (i = 0; i < IR_CUSTOM_BTN_ACTION_COUNT; i++)
	{
		uint8_t y = (uint8_t)(IR_CUSTOM_LIST_START_Y + (i * IR_CUSTOM_LIST_ITEM_H));

		if (i == selection)
		{
			u8g2_DrawBox(&m1_u8g2, 0, y, 128, IR_CUSTOM_LIST_ITEM_H);
			u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
			u8g2_DrawStr(&m1_u8g2, 4, y + 8, s_button_action_items[i]);
			u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
		}
		else
		{
			u8g2_DrawStr(&m1_u8g2, 4, y + 8, s_button_action_items[i]);
		}
	}

	m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Back", "Select", arrowright_8x8);
	m1_u8g2_nextpage();
}

/*============================================================================*/
/**
 * @brief  Rename the button at index via the on-screen keyboard (current name
 *         pre-filled). ESC cancels without touching the file.
 * @return true if the file was rewritten with the new name
 */
static bool ir_custom_rename_button(const char *path, uint16_t index)
{
	char    default_name[IR_CUSTOM_NAME_MAX_LEN];
	char    entered[IR_CUSTOM_NAME_MAX_LEN];
	char    sane[IR_CUSTOM_NAME_MAX_LEN];
	uint8_t got;

	strncpy(default_name, s_button_names[index], sizeof(default_name) - 1);
	default_name[sizeof(default_name) - 1] = '\0';
	entered[0] = '\0';

	got = m1_vkb_get_filename("Rename:", default_name, entered);
	if (got == 0)
		return false;   /* cancelled — file untouched */

	ir_custom_sanitize_name(entered, sane, sizeof(sane));
	return flipper_ir_rename_signal(path, index, sane);
}

/*============================================================================*/
/**
 * @brief  Draw a two-line confirmation prompt and block until the user answers.
 *         OK/RIGHT confirms, BACK/LEFT cancels.
 * @return true if confirmed, false if cancelled
 */
static bool ir_custom_confirm(const char *l1, const char *l2)
{
	S_M1_Buttons_Status btn;
	S_M1_Main_Q_t       q_item;
	BaseType_t          ret;

	m1_u8g2_firstpage();
	u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
	u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
	if (l1 != NULL)
		u8g2_DrawStr(&m1_u8g2, 4, 24, l1);
	if (l2 != NULL)
		u8g2_DrawStr(&m1_u8g2, 4, 36, l2);
	m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Cancel", "Delete", arrowright_8x8);
	m1_u8g2_nextpage();

	while (1)
	{
		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
		if (ret != pdTRUE || q_item.q_evt_type != Q_EVENT_KEYPAD)
			continue;

		ret = xQueueReceive(button_events_q_hdl, &btn, 0);
		if (ret != pdTRUE)
			continue;

		if (btn.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK ||
		    btn.event[BUTTON_LEFT_KP_ID] == BUTTON_EVENT_CLICK)
		{
			xQueueReset(main_q_hdl);
			return false;
		}
		if (btn.event[BUTTON_OK_KP_ID]    == BUTTON_EVENT_CLICK ||
		    btn.event[BUTTON_RIGHT_KP_ID] == BUTTON_EVENT_CLICK)
		{
			xQueueReset(main_q_hdl);
			return true;
		}
	}
}

/*============================================================================*/
/**
 * @brief  Per-button action menu loop (Rename / Delete). Returns to the button
 *         list on BACK/LEFT or after an action completes.
 * @param  path   remote file path
 * @param  rname  button name (menu title)
 * @param  index  button index within the file
 */
static void ir_custom_button_action(const char *path, const char *rname, uint16_t index)
{
	S_M1_Buttons_Status btn;
	S_M1_Main_Q_t       q_item;
	BaseType_t          ret;
	uint8_t             selection = 0;

	ir_custom_draw_action_menu(rname, selection);

	while (1)
	{
		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
		if (ret != pdTRUE || q_item.q_evt_type != Q_EVENT_KEYPAD)
			continue;

		ret = xQueueReceive(button_events_q_hdl, &btn, 0);
		if (ret != pdTRUE)
			continue;

		if (btn.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK ||
		    btn.event[BUTTON_LEFT_KP_ID] == BUTTON_EVENT_CLICK)
		{
			xQueueReset(main_q_hdl);
			return;
		}
		else if (btn.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK)
		{
			selection = (selection > 0) ? (uint8_t)(selection - 1)
			                            : (uint8_t)(IR_CUSTOM_BTN_ACTION_COUNT - 1);
			ir_custom_draw_action_menu(rname, selection);
		}
		else if (btn.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK)
		{
			selection = (selection < IR_CUSTOM_BTN_ACTION_COUNT - 1)
			                ? (uint8_t)(selection + 1) : 0;
			ir_custom_draw_action_menu(rname, selection);
		}
		else if (btn.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK)
		{
			if (selection == 0)   /* Rename */
			{
				bool ok = ir_custom_rename_button(path, index);
				if (ok)
				{
					ir_custom_draw_message("Renamed", NULL);
					ir_custom_wait_key();
					return;   /* names changed; caller re-scans */
				}
			}
			else                  /* Delete */
			{
				if (ir_custom_confirm("Delete button?", rname))
				{
					bool ok = flipper_ir_delete_signal(path, index);
					ir_custom_draw_message(ok ? "Deleted" : "Delete failed", NULL);
					ir_custom_wait_key();
					return;   /* list changed; caller re-scans */
				}
			}
			xQueueReset(main_q_hdl);
			return;
		}
	}
}

/*============================================================================*/
/**
 * @brief  Edit screen: list the remote's buttons, open a per-button action
 *         menu on OK. Returns to the per-remote menu on BACK/LEFT.
 * @param  path  full path to the remote's .ir file
 * @param  name  display name (title)
 */
static void ir_custom_edit_buttons(const char *path, const char *name)
{
	S_M1_Buttons_Status btn;
	S_M1_Main_Q_t       q_item;
	BaseType_t          ret;
	uint16_t            selection = 0;

	ir_custom_scan_buttons(path);
	ir_custom_draw_button_list(name, selection, s_button_count);

	while (1)
	{
		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
		if (ret != pdTRUE || q_item.q_evt_type != Q_EVENT_KEYPAD)
			continue;

		ret = xQueueReceive(button_events_q_hdl, &btn, 0);
		if (ret != pdTRUE)
			continue;

		if (btn.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK ||
		    btn.event[BUTTON_LEFT_KP_ID] == BUTTON_EVENT_CLICK)
		{
			xQueueReset(main_q_hdl);
			return;
		}
		else if (s_button_count == 0)
		{
			continue;   /* empty remote: only BACK does anything */
		}
		else if (btn.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK)
		{
			selection = (selection > 0) ? (uint16_t)(selection - 1)
			                            : (uint16_t)(s_button_count - 1);
			ir_custom_draw_button_list(name, selection, s_button_count);
		}
		else if (btn.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK)
		{
			selection = (selection < s_button_count - 1) ? (uint16_t)(selection + 1) : 0;
			ir_custom_draw_button_list(name, selection, s_button_count);
		}
		else if (btn.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK)
		{
			ir_custom_button_action(path, s_button_names[selection], selection);

			/* Names may have changed; re-scan and clamp the selection. */
			ir_custom_scan_buttons(path);
			if (s_button_count == 0)
				selection = 0;
			else if (selection >= s_button_count)
				selection = (uint16_t)(s_button_count - 1);
			ir_custom_draw_button_list(name, selection, s_button_count);
		}
	}
}

/*============================================================================*/
/**
 * @brief  Per-remote screen: choose Play Buttons (shared replay), Learn Button
 *         (learn + append), or Edit Buttons (rename). Returns to the manager on
 *         BACK/LEFT.
 * @param  path  full path to the remote's .ir file
 * @param  name  display name (title)
 */
static void ir_custom_open_remote(const char *path, const char *name)
{
	S_M1_Buttons_Status btn;
	S_M1_Main_Q_t       q_item;
	BaseType_t          ret;
	uint8_t             selection = 0;

	ir_custom_draw_remote_menu(name, selection);

	while (1)
	{
		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
		if (ret != pdTRUE)
			continue;

		if (q_item.q_evt_type != Q_EVENT_KEYPAD)
			continue;

		ret = xQueueReceive(button_events_q_hdl, &btn, 0);
		if (ret != pdTRUE)
			continue;

		if (btn.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK ||
		    btn.event[BUTTON_LEFT_KP_ID] == BUTTON_EVENT_CLICK)
		{
			xQueueReset(main_q_hdl);
			return;
		}
		else if (btn.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK)
		{
			selection = (selection > 0) ? (uint8_t)(selection - 1)
			                            : (uint8_t)(IR_CUSTOM_REMOTE_MENU_COUNT - 1);
			ir_custom_draw_remote_menu(name, selection);
		}
		else if (btn.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK)
		{
			selection = (selection < IR_CUSTOM_REMOTE_MENU_COUNT - 1)
			                ? (uint8_t)(selection + 1) : 0;
			ir_custom_draw_remote_menu(name, selection);
		}
		else if (btn.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK)
		{
			if (selection == 0)
				ir_replay_file(path);       /* shared button-list replay */
			else if (selection == 1)
				ir_custom_learn_button(path);
			else
				ir_custom_edit_buttons(path, name);

			ir_custom_draw_remote_menu(name, selection);
		}
	}
}

/*============================================================================*/
/**
 * @brief  Custom-remote entry point ("Custom Remotes" Infrared submenu item).
 *         Shows a manager list: "[+ New Remote]" plus every user-built
 *         0:/IR/*.ir remote. New -> name+create; opening a remote runs the
 *         shared scrolling button-list replay screen (ir_replay_file). Learn
 *         and edit actions land in later slices.
 */
void ir_custom_run(void)
{
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t       q_item;
	BaseType_t          ret;
	uint16_t            selection = 0;
	uint16_t            total;

	ir_custom_scan_remotes();
	total = (uint16_t)(s_remote_count + 1);   /* +1 for "[+ New Remote]" */
	ir_custom_draw_list(selection, total);

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

		if (this_button_status.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK ||
		    this_button_status.event[BUTTON_LEFT_KP_ID] == BUTTON_EVENT_CLICK)
		{
			xQueueReset(main_q_hdl);
			break;   /* return to the Infrared menu */
		}
		else if (this_button_status.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK)
		{
			selection = (selection > 0) ? (uint16_t)(selection - 1)
			                            : (uint16_t)(total - 1);
			ir_custom_draw_list(selection, total);
		}
		else if (this_button_status.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK)
		{
			selection = (selection < total - 1) ? (uint16_t)(selection + 1) : 0;
			ir_custom_draw_list(selection, total);
		}
		else if (this_button_status.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK)
		{
			if (selection == 0)
			{
				ir_custom_create_flow();
			}
			else
			{
				char path[IR_CUSTOM_PATH_MAX_LEN];
				snprintf(path, sizeof(path), "%s/%s.ir",
				         IR_CUSTOM_DIR, s_remote_names[selection - 1]);
				ir_custom_open_remote(path, s_remote_names[selection - 1]);
			}

			/* A remote may have been created; re-scan and clamp selection. */
			ir_custom_scan_remotes();
			total = (uint16_t)(s_remote_count + 1);
			if (selection >= total)
				selection = (uint16_t)(total - 1);
			ir_custom_draw_list(selection, total);
		}
	}
} /* void ir_custom_run(void) */

#endif /* M1_APP_FILE_IMPORT_ENABLE */
