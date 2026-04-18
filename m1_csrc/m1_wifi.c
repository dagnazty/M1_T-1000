/* See COPYING.txt for license details. */

/*
*
* m1_wifi.c
*
* Library for M1 Wifi — scan, connect, saved networks, status
*
* M1 Project
*
*/

/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include "stm32h5xx_hal.h"
#include "main.h"
#include "m1_wifi.h"
#include "m1_esp32_hal.h"
//#include "control.h"
#include "ctrl_api.h"
#include "esp_app_main.h"
#include "esp_at_list.h"
#include "m1_compile_cfg.h"

#ifdef M1_APP_WIFI_CONNECT_ENABLE
#include "m1_wifi_cred.h"
#include "m1_virtual_kb.h"
#endif

/*************************** D E F I N E S ************************************/

#define M1_LOGDB_TAG	"Wifi"

#define M1_WIFI_AP_SCANNING_TIME	30 // seconds

#define M1_GUI_ROW_SPACING			1
#define M1_WIFI_BAND_NOTE			"2.4GHz only"
#define M1_WIFI_SCAN_ATTEMPTS		2
#define M1_WIFI_RECOVERY_DELAY_MS	400
#define M1_WIFI_HEALTH_MAX_TRACKED_APS	32

//************************** S T R U C T U R E S *******************************

/***************************** V A R I A B L E S ******************************/

#ifdef M1_APP_WIFI_CONNECT_ENABLE
/* Track current AP index in scan list for connect-from-scan */
static uint16_t s_current_ap_index = 0;
static bool s_wifi_connected = false;
static char s_connected_ssid[SSID_LENGTH];
#endif

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

void menu_wifi_init(void);
void menu_wifi_exit(void);

void wifi_scan_ap(void);
void wifi_survey_24g(void);
void wifi_health_24g(void);
void wifi_config(void);

static uint16_t wifi_ap_list_print(ctrl_cmd_t *app_resp, bool up_dir);
static uint8_t wifi_ap_list_validation(ctrl_cmd_t *app_resp);
static const char *wifi_auth_mode_to_str(int mode);
static const char *wifi_channel_band_to_str(int channel);
static const char *wifi_scan_ssid_label(const wifi_scanlist_t *ap);
static void wifi_free_scan_results(ctrl_cmd_t *app_req);
static void wifi_display_msg(const char *line1, const char *line2);
static void wifi_display_panel(const char *title, const char *line1, const char *line2, const char *line3);
static void wifi_recover_module(void);
static bool wifi_run_scan(ctrl_cmd_t *app_req, uint8_t attempts);
static uint16_t wifi_health_count_unmatched(const char lhs[][BSSID_STR_SIZE], uint16_t lhs_count,
		const char rhs[][BSSID_STR_SIZE], uint16_t rhs_count);

#ifdef M1_APP_WIFI_CONNECT_ENABLE
void wifi_saved_networks(void);
void wifi_show_status(void);
void wifi_show_mode(void);
void wifi_show_stats(void);
void wifi_disconnect(void);
static uint16_t wifi_ap_list_get_index(void);
static bool wifi_do_connect(const char *ssid, const char *password);
static bool wifi_scan_ap_prompt_password(const wifi_scanlist_t *ap, char *password, uint16_t password_len);
static bool wifi_scan_ap_connect_selected(const wifi_scanlist_t *ap);
static void wifi_scan_ap_save_selected(const wifi_scanlist_t *ap);
static void wifi_scan_ap_show_details(const wifi_scanlist_t *ap);
static uint8_t wifi_scan_ap_options(const wifi_scanlist_t *ap);

#ifdef M1_APP_WIFI_OFFENSIVE_ENABLE
static void wifi_attack_add_target(const wifi_scanlist_t *ap);
static bool wifi_attack_target_actions(uint8_t idx);
#endif
static void wifi_draw_stats_page(char body_lines[][24], uint8_t scroll);
#endif

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/*============================================================================*/
/**
  * @brief Report current remembered WiFi link state for shared UI
  * @retval 1 when connected, otherwise 0
  */
/*============================================================================*/
uint8_t wifi_is_connected(void)
{
#ifdef M1_APP_WIFI_CONNECT_ENABLE
	return s_wifi_connected ? 1U : 0U;
#else
	return 0U;
#endif
}

/*============================================================================*/
/**
  * @brief  Initialize ESP32 module if not already initialized
  * @retval true if ready, false if failed
  */
/*============================================================================*/
static bool wifi_ensure_esp32_ready(void)
{
	if ( !m1_esp32_get_init_status() )
	{
		m1_esp32_init();
	}

	if ( !get_esp32_main_init_status() )
	{
		m1_u8g2_firstpage();
		m1_draw_status_panel(&m1_u8g2, "WiFi 2.4G", "Init",
						  hourglass_18x32, 18, 32,
						  "Initializing", "Preparing ESP32-C6", M1_WIFI_BAND_NOTE);
		m1_u8g2_nextpage();
		esp32_main_init();
	}
	return get_esp32_main_init_status();
}


/*============================================================================*/
/**
  * @brief Map auth mode enum to a short UI string
  */
/*============================================================================*/
static const char *wifi_auth_mode_to_str(int mode)
{
	switch (mode)
	{
		case WIFI_AUTH_OPEN:
			return "Open";
		case WIFI_AUTH_WEP:
			return "WEP";
		case WIFI_AUTH_WPA_PSK:
			return "WPA-PSK";
		case WIFI_AUTH_WPA2_PSK:
			return "WPA2-PSK";
		case WIFI_AUTH_WPA_WPA2_PSK:
			return "WPA/WPA2";
		case WIFI_AUTH_WPA2_ENTERPRISE:
			return "WPA2-Ent";
		case WIFI_AUTH_WPA3_PSK:
			return "WPA3-PSK";
		case WIFI_AUTH_WPA2_WPA3_PSK:
			return "WPA2/WPA3";
		default:
			return "Unknown";
	}
}


/*============================================================================*/
/**
  * @brief Map scanned channel to the expected band label
  */
/*============================================================================*/
static const char *wifi_channel_band_to_str(int channel)
{
	if (channel >= 1 && channel <= 14)
		return "2.4GHz";

	return "unsupported";
}


/*============================================================================*/
/**
  * @brief Return a display-safe SSID label for scan results
  */
/*============================================================================*/
static const char *wifi_scan_ssid_label(const wifi_scanlist_t *ap)
{
	if (ap == NULL || ap->ssid[0] == 0x00)
		return "*hidden*";

	return (const char *)ap->ssid;
}


/*============================================================================*/
/**
  * @brief Free scan results allocated by the AT parser
  */
/*============================================================================*/
static void wifi_free_scan_results(ctrl_cmd_t *app_req)
{
	if (app_req && app_req->u.wifi_ap_scan.out_list != NULL)
	{
		free(app_req->u.wifi_ap_scan.out_list);
		app_req->u.wifi_ap_scan.out_list = NULL;
		app_req->u.wifi_ap_scan.count = 0;
	}
}


/*============================================================================*/
/**
  * @brief Recover ESP32-C6 after a failed scan transaction
  */
/*============================================================================*/
static void wifi_recover_module(void)
{
	esp32_disable();
	m1_hard_delay(100);
	esp32_enable();
	m1_hard_delay(M1_WIFI_RECOVERY_DELAY_MS);
}


/*============================================================================*/
/**
  * @brief Run AP scan with one controlled recovery retry
  */
/*============================================================================*/
static bool wifi_run_scan(ctrl_cmd_t *app_req, uint8_t attempts)
{
	uint8_t ret = ERROR;

	if (app_req == NULL)
		return false;

	for (uint8_t attempt = 0; attempt < attempts; attempt++)
	{
		wifi_free_scan_results(app_req);
		app_req->msg_type = CTRL_REQ;
		app_req->resp_event_status = ERROR;
		app_req->cmd_timeout_sec = M1_WIFI_AP_SCANNING_TIME;
		app_req->msg_id = CTRL_RESP_GET_AP_SCAN_LIST;

		ret = wifi_ap_scan_list(app_req);
		if (ret == SUCCESS && wifi_ap_list_validation(app_req))
			return true;

		if ((attempt + 1U) < attempts)
		{
			wifi_display_panel("WiFi 2.4G", "Scan failed", "Recovering ESP32...", "Retrying now");
			wifi_recover_module();
		}
	}

	return false;
}


/*============================================================================*/
/**
  * @brief Count AP BSSIDs present in lhs but not rhs
  */
/*============================================================================*/
static uint16_t wifi_health_count_unmatched(const char lhs[][BSSID_STR_SIZE], uint16_t lhs_count,
		const char rhs[][BSSID_STR_SIZE], uint16_t rhs_count)
{
	uint16_t unmatched = 0;

	for (uint16_t i = 0; i < lhs_count; i++)
	{
		bool found = false;

		if (lhs[i][0] == '\0')
			continue;

		for (uint16_t j = 0; j < rhs_count; j++)
		{
			if (rhs[j][0] == '\0')
				continue;

			if (strncmp(lhs[i], rhs[j], BSSID_STR_SIZE) == 0)
			{
				found = true;
				break;
			}
		}

		if (!found)
			unmatched++;
	}

	return unmatched;
}


/*============================================================================*/
/**
  * @brief Show a 2.4GHz health summary based on nearby AP scan results
  * @retval
  */
/*============================================================================*/
void wifi_health_24g(void)
{
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	ctrl_cmd_t app_req = {0};
	wifi_scanlist_t *list;
	uint16_t ap_count = 0;
	uint16_t open_count = 0;
	uint16_t hidden_count = 0;
	uint8_t channel_counts[15] = {0};
	uint8_t busiest_channel = 0;
	uint8_t busiest_count = 0;
	int strongest_rssi = -127;
	const char *health_label = "Clear";
	bool running = true;
	bool prev_valid = false;
	bool scan_ok;
	char line1[28];
	char line2[28];
	char line3[28];
	char footer[28];
	char prev_bssid[M1_WIFI_HEALTH_MAX_TRACKED_APS][BSSID_STR_SIZE] = {{0}};
	char curr_bssid[M1_WIFI_HEALTH_MAX_TRACKED_APS][BSSID_STR_SIZE] = {{0}};
	uint16_t prev_count = 0;
	uint16_t current_tracked = 0;
	uint16_t new_count = 0;
	uint16_t gone_count = 0;

	menu_wifi_init();

	while (running)
	{
		ap_count = 0;
		open_count = 0;
		hidden_count = 0;
		busiest_channel = 0;
		busiest_count = 0;
		strongest_rssi = -127;
		current_tracked = 0;
		new_count = 0;
		gone_count = 0;
		memset(channel_counts, 0, sizeof(channel_counts));
		memset(curr_bssid, 0, sizeof(curr_bssid));

		if ( !wifi_ensure_esp32_ready() )
		{
			wifi_display_panel("2.4G Health", "ESP32 not ready", "Check module link", "BACK to exit");
			scan_ok = false;
		}
		else
		{
			wifi_display_panel("2.4G Health", "Scanning nearby APs", "Building health view", "Please wait...");
			scan_ok = wifi_run_scan(&app_req, M1_WIFI_SCAN_ATTEMPTS);
		}

		if (scan_ok)
		{
			list = app_req.u.wifi_ap_scan.out_list;
			ap_count = app_req.u.wifi_ap_scan.count;

			for (uint16_t i = 0; i < ap_count; i++)
			{
				if (list[i].ssid[0] == 0x00)
					hidden_count++;
				if (list[i].encryption_mode == WIFI_AUTH_OPEN)
					open_count++;
				if (list[i].channel >= 1 && list[i].channel <= 14)
				{
					channel_counts[list[i].channel]++;
					if (channel_counts[list[i].channel] > busiest_count)
					{
						busiest_count = channel_counts[list[i].channel];
						busiest_channel = list[i].channel;
					}
				}
				if (list[i].rssi > strongest_rssi)
					strongest_rssi = list[i].rssi;

				if (current_tracked < M1_WIFI_HEALTH_MAX_TRACKED_APS)
				{
					strncpy(curr_bssid[current_tracked], (const char *)list[i].bssid, BSSID_STR_SIZE - 1);
					curr_bssid[current_tracked][BSSID_STR_SIZE - 1] = '\0';
					current_tracked++;
				}
			}

			if (prev_valid)
			{
				new_count = wifi_health_count_unmatched(curr_bssid, current_tracked, prev_bssid, prev_count);
				gone_count = wifi_health_count_unmatched(prev_bssid, prev_count, curr_bssid, current_tracked);
			}

			if (ap_count == 0)
				health_label = "Clear";
			else if (busiest_count >= 5 || ap_count >= 12 || (new_count + gone_count) >= 6)
				health_label = "Crowded";
			else if (busiest_count >= 3 || ap_count >= 7 || (new_count + gone_count) >= 3)
				health_label = "Busy";
			else
				health_label = "Normal";

			snprintf(line1, sizeof(line1), "APs:%u  Open:%u", ap_count, open_count);
			snprintf(line2, sizeof(line2), "Hidden:%u  Peak:%ddBm", hidden_count, strongest_rssi);
			if (busiest_channel != 0)
				snprintf(line3, sizeof(line3), "Ch%u x%u %s +%u/-%u", busiest_channel, busiest_count, health_label, new_count, gone_count);
			else
				snprintf(line3, sizeof(line3), "%s +%u/-%u", health_label, new_count, gone_count);
			snprintf(footer, sizeof(footer), "OK:Rescan BACK:Exit");

			u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
			m1_u8g2_firstpage();
			u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
			u8g2_DrawXBMP(&m1_u8g2, 0, 0, 128, 14, m1_frame_128_14);
			u8g2_DrawStr(&m1_u8g2, 2, M1_GUI_ROW_SPACING + M1_GUI_FONT_HEIGHT, "2.4G Health");
			u8g2_DrawFrame(&m1_u8g2, 2, 16, 124, 38);
			u8g2_DrawStr(&m1_u8g2, 6, 26, line1);
			u8g2_DrawStr(&m1_u8g2, 6, 36, line2);
			u8g2_DrawStr(&m1_u8g2, 6, 46, line3);
			u8g2_DrawStr(&m1_u8g2, 4, 64, footer);
			m1_u8g2_nextpage();

			memcpy(prev_bssid, curr_bssid, sizeof(prev_bssid));
			prev_count = current_tracked;
			prev_valid = true;
		}
		else
		{
			wifi_display_panel("2.4G Health", "Scan failed", "OK to retry", "BACK to exit");
		}

		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
		if (ret == pdTRUE && q_item.q_evt_type == Q_EVENT_KEYPAD)
		{
			xQueueReceive(button_events_q_hdl, &this_button_status, 0);
			if (this_button_status.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK)
				running = false;
			else if (this_button_status.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK)
			{
				/* Do nothing - OK button refreshes scan automatically */
			}
		}
	}

	wifi_free_scan_results(&app_req);
	menu_wifi_exit();
	xQueueReset(main_q_hdl);
	m1_app_send_q_message(main_q_hdl, Q_EVENT_MENU_EXIT);
}


/*============================================================================*/
void menu_wifi_init(void)
{
	;
} // void menu_wifi_init(void)

#ifdef M1_APP_WIFI_OFFENSIVE_ENABLE
/*============================================================================*/
/**
  * @brief No-op init for the "Offensive Tools" submenu container
  */
/*============================================================================*/
void menu_wifi_offensive_init(void)
{
	;
}
#endif


/*============================================================================*/
void  menu_wifi_exit(void)
{
	;
} // void  menu_wifi_exit(void)



/*============================================================================*/
/**
  * @brief Scans for wifi access point list and allows connecting
  * @param
  * @retval
  */
/*============================================================================*/
void wifi_scan_ap(void)
{
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	ctrl_cmd_t app_req = CTRL_CMD_DEFAULT_REQ();
	uint16_t list_count;
#ifdef M1_APP_WIFI_CONNECT_ENABLE
	wifi_scanlist_t *list;
#endif

    /* Graphic work starts here */
	u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
	if ( !wifi_ensure_esp32_ready() )
	{
		wifi_display_panel("WiFi 2.4G", "ESP32 not ready", "Check module link", "BACK to exit");
		/* Fall through to event loop so user can press BACK */
	}

	list_count = 0;

	if ( get_esp32_main_init_status() )
	{
		wifi_display_panel("WiFi 2.4G", "Scanning nearby APs", M1_WIFI_BAND_NOTE, "Please wait...");

		if ( wifi_run_scan(&app_req, M1_WIFI_SCAN_ATTEMPTS) )
		{
			list_count = wifi_ap_list_print(&app_req, true);
		} // if ( ret )
		else
		{
			wifi_display_panel("WiFi 2.4G", "Scan failed", "Press BACK to exit", "Try ESP Bring-up");
		}
	} // if ( get_esp32_main_init_status() )

	while (1 ) // Main loop of this task
	{
		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
		if (ret==pdTRUE)
		{
			if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
			{
				ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
				if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK ) // user wants to exit?
				{
					wifi_free_scan_results(&app_req);
					wifi_ap_list_print(NULL, false);

					xQueueReset(main_q_hdl); // Reset main q before return
					m1_esp32_deinit();
					esp32_main_force_reinit();
					break; // Exit
				}
				else if ( this_button_status.event[BUTTON_UP_KP_ID]==BUTTON_EVENT_CLICK )
				{
					if ( list_count )
						wifi_ap_list_print(&app_req, true);
				}
				else if ( this_button_status.event[BUTTON_DOWN_KP_ID]==BUTTON_EVENT_CLICK )
				{
					if ( list_count )
						wifi_ap_list_print(&app_req, false);
				}
				else if ( this_button_status.event[BUTTON_OK_KP_ID]==BUTTON_EVENT_CLICK ||
						  this_button_status.event[BUTTON_RIGHT_KP_ID]==BUTTON_EVENT_CLICK )
				{
					if ( !list_count )
					{
						if ( !wifi_ensure_esp32_ready() )
						{
							wifi_display_panel("WiFi 2.4G", "ESP32 not ready", "Check module link", "BACK to exit");
							continue;
						}

						wifi_display_panel("WiFi 2.4G", "Scanning nearby APs", M1_WIFI_BAND_NOTE, "Please wait...");
						if ( wifi_run_scan(&app_req, M1_WIFI_SCAN_ATTEMPTS) )
							list_count = wifi_ap_list_print(&app_req, true);
						else
							wifi_display_panel("WiFi 2.4G", "Scan failed", "Press OK to retry", "BACK to exit");
						continue;
					}

#ifdef M1_APP_WIFI_CONNECT_ENABLE
					/* Get the currently displayed AP */
					list = app_req.u.wifi_ap_scan.out_list;
					s_current_ap_index = wifi_ap_list_get_index();

					if ( s_current_ap_index >= app_req.u.wifi_ap_scan.count )
						continue;
					wifi_scan_ap_options(&list[s_current_ap_index]);
					u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
					wifi_ap_list_print(NULL, false); /* reset state */
					list_count = wifi_ap_list_print(&app_req, true);
#endif /* M1_APP_WIFI_CONNECT_ENABLE */
				}
			} // if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
		} // if (ret==pdTRUE)
	} // while (1 ) // Main loop of this task

} // void wifi_scan_ap(void)


/*============================================================================*/
/**
  * @brief Survey nearby 2.4 GHz access points and summarize channel usage
  */
/*============================================================================*/
void wifi_survey_24g(void)
{
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	ctrl_cmd_t app_req = CTRL_CMD_DEFAULT_REQ();
	uint8_t channel_counts[14];
	int strongest_idx;
	int busiest_channel;
	int busiest_count;
	int seen_channels;
	char prn_msg[25];
	uint8_t y_offset;
	bool need_rescan = true;

	u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);

	if ( !wifi_ensure_esp32_ready() )
	{
		wifi_display_msg("ESP32", "not ready!");
		while (1)
		{
			ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
			if (ret==pdTRUE && q_item.q_evt_type==Q_EVENT_KEYPAD)
			{
				ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
				if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK )
				{
					xQueueReset(main_q_hdl);
					m1_esp32_deinit();
					return;
				}
			}
		}
	}

	while (1)
	{
		if ( need_rescan )
		{
			wifi_free_scan_results(&app_req);
			memset(channel_counts, 0, sizeof(channel_counts));
			strongest_idx = -1;
			busiest_channel = -1;
			busiest_count = 0;
			seen_channels = 0;

			wifi_display_panel("2.4G Survey", "Scanning channels", "Checking nearby APs", "Please wait...");

			app_req = (ctrl_cmd_t)CTRL_CMD_DEFAULT_REQ();
			ret = wifi_run_scan(&app_req, M1_WIFI_SCAN_ATTEMPTS);

			if ( ret && app_req.u.wifi_ap_scan.count > 0 && app_req.u.wifi_ap_scan.out_list != NULL )
			{
				for (int idx = 0; idx < app_req.u.wifi_ap_scan.count; idx++)
				{
					int channel = app_req.u.wifi_ap_scan.out_list[idx].channel;
					if ( channel >= 1 && channel <= 14 )
					{
						channel_counts[channel - 1]++;
						if ( channel_counts[channel - 1] == 1 )
							seen_channels++;
					}

					if ( strongest_idx < 0 ||
						app_req.u.wifi_ap_scan.out_list[idx].rssi > app_req.u.wifi_ap_scan.out_list[strongest_idx].rssi )
					{
						strongest_idx = idx;
					}
				}

				for (int ch = 0; ch < 14; ch++)
				{
					if ( channel_counts[ch] > busiest_count )
					{
						busiest_count = channel_counts[ch];
						busiest_channel = ch + 1;
					}
				}

				m1_u8g2_firstpage();
				u8g2_DrawXBMP(&m1_u8g2, 0, 0, 128, 14, m1_frame_128_14);
				u8g2_DrawStr(&m1_u8g2, 2, M1_GUI_ROW_SPACING + M1_GUI_FONT_HEIGHT, "2.4G Survey");

				y_offset = 14 + M1_GUI_FONT_HEIGHT;
				snprintf(prn_msg, sizeof(prn_msg), "APs:%d Ch:%d",
					app_req.u.wifi_ap_scan.count, seen_channels);
				u8g2_DrawStr(&m1_u8g2, 2, y_offset, prn_msg);
				y_offset += M1_GUI_FONT_HEIGHT;

				if ( strongest_idx >= 0 )
				{
					snprintf(prn_msg, sizeof(prn_msg), "Best:%ddBm ch%d",
						app_req.u.wifi_ap_scan.out_list[strongest_idx].rssi,
						app_req.u.wifi_ap_scan.out_list[strongest_idx].channel);
					u8g2_DrawStr(&m1_u8g2, 2, y_offset, prn_msg);
					y_offset += M1_GUI_FONT_HEIGHT;

					strncpy(prn_msg, wifi_scan_ssid_label(&app_req.u.wifi_ap_scan.out_list[strongest_idx]), 20);
					prn_msg[20] = '\0';
					u8g2_DrawStr(&m1_u8g2, 2, y_offset, prn_msg);
					y_offset += M1_GUI_FONT_HEIGHT;
				}

				if ( busiest_channel > 0 )
					snprintf(prn_msg, sizeof(prn_msg), "Busy:ch%d ap%d", busiest_channel, busiest_count);
				else
					snprintf(prn_msg, sizeof(prn_msg), "Busy: none");
				u8g2_DrawStr(&m1_u8g2, 2, y_offset, prn_msg);
				y_offset += M1_GUI_FONT_HEIGHT;

				u8g2_DrawStr(&m1_u8g2, 2, y_offset, "OK: Rescan");
				y_offset += M1_GUI_FONT_HEIGHT;
				u8g2_DrawStr(&m1_u8g2, 2, y_offset, "BACK: Exit");
				m1_u8g2_nextpage();
			}
			else
			{
				wifi_display_panel("2.4G Survey", "No APs found", "OK to rescan", "BACK to exit");
			}

			need_rescan = false;
		}

		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
		if (ret==pdTRUE && q_item.q_evt_type==Q_EVENT_KEYPAD)
		{
			ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
			if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK )
			{
				wifi_free_scan_results(&app_req);
				xQueueReset(main_q_hdl);
				m1_esp32_deinit();
				return;
			}
			else if ( this_button_status.event[BUTTON_OK_KP_ID]==BUTTON_EVENT_CLICK )
			{
				need_rescan = true;
			}
		}
	}
} // void wifi_survey_24g(void)



/*============================================================================*/
/**
  * @brief Displays all scanned AP list.
  * @param
  * @retval
  */
/*============================================================================*/
static uint16_t wifi_ap_list_print(ctrl_cmd_t *app_resp, bool up_dir)
{
	static uint16_t i;
	static wifi_ap_scan_list_t *w_scan_p;
	static wifi_scanlist_t *list;
	static bool init_done = false;
	char prn_msg[25];
	uint8_t y_offset;

	if ( !app_resp && !up_dir ) // reset condition?
	{
		init_done = false;
		return 0;
	} // if ( !app_resp && !up_dir )

	if ( !init_done )
	{
		init_done = true;
		w_scan_p = &app_resp->u.wifi_ap_scan;
		list = w_scan_p->out_list;

		if (!w_scan_p->count)
		{
			strcpy(prn_msg, "No 2.4G APs");
			M1_LOG_I(M1_LOGDB_TAG, "No AP found\n\r");
			init_done = false;
		}
		else if (!list)
		{
			strcpy(prn_msg, "Try again!");
			M1_LOG_I(M1_LOGDB_TAG, "Failed to get scanned AP list\n\r");
			init_done = false;
		}
		else
		{
			M1_LOG_I(M1_LOGDB_TAG, "Number of available APs is %d\n\r", w_scan_p->count);
		}

		if ( !init_done )
		{
			u8g2_DrawStr(&m1_u8g2, 6, 25 + M1_GUI_ROW_SPACING + M1_GUI_FONT_HEIGHT, prn_msg);
			m1_u8g2_nextpage(); // Update display RAM
			return 0;
		}
		// Display first AP in the list
		i = 1;
		up_dir = true; // Overwrite the up_dir for the AP to be displayed for the first time
	} // if ( !init_done )

	if ( up_dir )
	{
		if ( i )
			i--;
		else
			i = w_scan_p->count-1; // roll over
	}
	else
	{
		i++;
		if ( i >= w_scan_p->count )
			i = 0; // roll over
	}

#ifdef M1_APP_WIFI_CONNECT_ENABLE
	s_current_ap_index = i;
#endif

	m1_u8g2_firstpage();
	sprintf(prn_msg, "%d/%d", i + 1, w_scan_p->count); // Current AP
	m1_draw_header_bar(&m1_u8g2, "WiFi Scan", prn_msg);
	m1_draw_content_frame(&m1_u8g2, 2, 14, 124, 35);
	u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);

	y_offset = 24;
	if ( list[i].ssid[0]==0x00 ) // Hidden SSID?
		strcpy(prn_msg, "*hidden*");
	else
		strncpy(prn_msg, (char *)list[i].ssid, 22);
	prn_msg[22] = '\0';
	m1_draw_text(&m1_u8g2, 8, y_offset, 114, prn_msg, TEXT_ALIGN_LEFT);
	y_offset += 8;
	m1_draw_text(&m1_u8g2, 8, y_offset, 114, (char *)list[i].bssid, TEXT_ALIGN_LEFT);
	y_offset += 8;
	snprintf(prn_msg, sizeof(prn_msg), "RSSI:%ddBm  Ch:%d",
		list[i].rssi, list[i].channel);
	m1_draw_text(&m1_u8g2, 8, y_offset, 114, prn_msg, TEXT_ALIGN_LEFT);
	y_offset += 8;
	snprintf(prn_msg, sizeof(prn_msg), "Auth: %s",
		wifi_auth_mode_to_str(list[i].encryption_mode));
	m1_draw_text(&m1_u8g2, 8, y_offset, 114, prn_msg, TEXT_ALIGN_LEFT);

#ifdef M1_APP_WIFI_CONNECT_ENABLE
	m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Back", "Options", arrowright_8x8);
#else
	m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Back", "", NULL);
#endif

	m1_u8g2_nextpage(); // Update display RAM

	M1_LOG_D(M1_LOGDB_TAG, "%d) ssid \"%s\" bssid \"%s\" rssi \"%d\" channel \"%d\" auth mode \"%d\" \n\r",\
						i, list[i].ssid, list[i].bssid, list[i].rssi,
						list[i].channel, list[i].encryption_mode);

	return w_scan_p->count;
} // static uint16_t wifi_ap_list_print(ctrl_cmd_t *app_resp, bool up_dir)



/*============================================================================*/
/**
  * @brief Validates the AP list.
  */
/*============================================================================*/
static uint8_t wifi_ap_list_validation(ctrl_cmd_t *app_resp)
{
	if (!app_resp || (app_resp->msg_type != CTRL_RESP))
	{
		if (app_resp)
			M1_LOG_I(M1_LOGDB_TAG, "Msg type is not response[%u]\n\r", app_resp->msg_type);
		return false;
	}
	if (app_resp->resp_event_status != SUCCESS)
	{
		//process_failed_responses(app_resp);
		return false;
	}
	if (app_resp->msg_id != CTRL_RESP_GET_AP_SCAN_LIST)
	{
		M1_LOG_I(M1_LOGDB_TAG, "Invalid Response[%u] to parse\n\r", app_resp->msg_id);
		return false;
	}

	return true;
} // static uint8_t wifi_ap_list_validation(ctrl_cmd_t *app_resp)


/*============================================================================*/
/**
  * @brief Display a two-line message centered on screen
  */
/*============================================================================*/
static void wifi_display_msg(const char *line1, const char *line2)
{
	wifi_display_panel("WiFi 2.4G", line1, line2, NULL);
}


/*============================================================================*/
/**
  * @brief Display a framed WiFi panel with up to three lines
  */
/*============================================================================*/
static void wifi_display_panel(const char *title, const char *line1, const char *line2, const char *line3)
{
	m1_u8g2_firstpage();
	m1_draw_status_panel(&m1_u8g2, title ? title : "WiFi 2.4G", NULL,
					  NULL, 0, 0, line1, line2, line3);
	m1_u8g2_nextpage();
}


#ifdef M1_APP_WIFI_CONNECT_ENABLE

/*============================================================================*/
/**
  * @brief Returns the current AP index from the list display
  */
/*============================================================================*/
static uint16_t wifi_ap_list_get_index(void)
{
	return s_current_ap_index;
}


/*============================================================================*/
/**
  * @brief Display a message with hourglass icon
  */
/*============================================================================*/
static void wifi_display_busy(const char *msg)
{
	m1_u8g2_firstpage();
	m1_draw_status_panel(&m1_u8g2, "WiFi 2.4G", "Live",
					  hourglass_18x32, 18, 32,
					  msg, M1_WIFI_BAND_NOTE, "Please wait...");
	m1_u8g2_nextpage();
}


/*============================================================================*/
/**
  * @brief Execute WiFi connect sequence
  * @param ssid  - network SSID
  * @param password - network password (empty string for open networks)
  * @retval true on success
  */
/*============================================================================*/
static bool wifi_do_connect(const char *ssid, const char *password)
{
	ctrl_cmd_t conn_req = CTRL_CMD_DEFAULT_REQ();
	uint8_t ret;

	wifi_display_busy("Connecting...");

	/* Populate connect request */
	strncpy((char *)conn_req.u.wifi_ap_config.ssid, ssid, SSID_LENGTH - 1);
	conn_req.u.wifi_ap_config.ssid[SSID_LENGTH - 1] = '\0';
	strncpy((char *)conn_req.u.wifi_ap_config.pwd, password, PASSWORD_LENGTH - 1);
	conn_req.u.wifi_ap_config.pwd[PASSWORD_LENGTH - 1] = '\0';
	conn_req.cmd_timeout_sec = DEFAULT_CTRL_RESP_CONNECT_AP_TIMEOUT;
	conn_req.msg_id = CTRL_RESP_CONNECT_AP;

	ret = wifi_connect_ap(&conn_req);

	if ( ret == SUCCESS && conn_req.resp_event_status == SUCCESS )
	{
		s_wifi_connected = true;
		strncpy(s_connected_ssid, ssid, SSID_LENGTH - 1);
		s_connected_ssid[SSID_LENGTH - 1] = '\0';

		/* Get IP address to display */
		ctrl_cmd_t ip_req = CTRL_CMD_DEFAULT_REQ();
		ip_req.cmd_timeout_sec = 10;
		wifi_get_ip(&ip_req);

		m1_u8g2_firstpage();
		m1_draw_header_bar(&m1_u8g2, "WiFi 2.4G", "Online");
		m1_draw_content_frame(&m1_u8g2, 2, 14, 124, 35);
		u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
		m1_draw_text(&m1_u8g2, 8, 25, 114, "Connected!", TEXT_ALIGN_LEFT);
		m1_draw_text(&m1_u8g2, 8, 34, 114, ssid, TEXT_ALIGN_LEFT);
		if ( ip_req.u.wifi_ap_config.status[0] )
		{
			char ip_msg[25];
			snprintf(ip_msg, sizeof(ip_msg), "IP: %s", ip_req.u.wifi_ap_config.status);
			m1_draw_text(&m1_u8g2, 8, 43, 114, ip_msg, TEXT_ALIGN_LEFT);
		}
		else
		{
			m1_draw_text(&m1_u8g2, 8, 43, 114, M1_WIFI_BAND_NOTE, TEXT_ALIGN_LEFT);
		}
		m1_u8g2_nextpage();
		M1_LOG_I(M1_LOGDB_TAG, "Connected to %s, IP: %s\n\r", ssid, ip_req.u.wifi_ap_config.status);
		vTaskDelay(pdMS_TO_TICKS(2500));
		return true;
	}
	else
	{
		const char *err_msg1 = "WiFi Error:";
		const char *err_msg2 = "Connect failed!";
		if ( conn_req.resp_event_status == 2 )
			err_msg2 = "Wrong password!";
		else if ( conn_req.resp_event_status == 3 )
		{
			err_msg1 = "2.4GHz only";
			err_msg2 = "AP not found";
		}
		else if ( conn_req.resp_event_status == 1 )
			err_msg2 = "Timeout!";

		wifi_display_msg(err_msg1, err_msg2);
		M1_LOG_E(M1_LOGDB_TAG, "Connect failed: %s %s (code %ld)\n\r", err_msg1, err_msg2, conn_req.resp_event_status);
		vTaskDelay(pdMS_TO_TICKS(2500));
	}

	return false;
}


/*============================================================================*/
/**
  * @brief Prompt for an AP password when needed
  * @retval true when a usable password was collected
  */
/*============================================================================*/
static bool wifi_scan_ap_prompt_password(const wifi_scanlist_t *ap, char *password, uint16_t password_len)
{
	uint8_t pw_len;

	if (ap == NULL || password == NULL || password_len == 0)
		return false;

	password[0] = '\0';

	if (ap->ssid[0] == 0x00)
	{
		wifi_display_msg("Hidden network", "Can't use SSID");
		vTaskDelay(pdMS_TO_TICKS(1500));
		return false;
	}

	if (ap->encryption_mode == WIFI_AUTH_OPEN)
		return true;

	memset(password, 0, password_len);
	pw_len = m1_vkb_get_filename("Password:", "", password);
	if (pw_len == 0)
		return false;

	password[password_len - 1] = '\0';
	return true;
}


/*============================================================================*/
/**
  * @brief Connect to the selected AP using saved or prompted credentials
  * @retval true when the AP connects successfully
  */
/*============================================================================*/
static bool wifi_scan_ap_connect_selected(const wifi_scanlist_t *ap)
{
	wifi_credential_t cred;
	char password[WIFI_CRED_PASS_MAX_LEN];

	if (ap == NULL)
		return false;

	if (ap->ssid[0] == 0x00)
	{
		wifi_display_msg("Hidden network", "Use Saved menu");
		vTaskDelay(pdMS_TO_TICKS(1500));
		return false;
	}

	if (wifi_cred_find((const char *)ap->ssid, &cred))
	{
		strncpy(password, cred.password, sizeof(password) - 1);
		password[sizeof(password) - 1] = '\0';
		return wifi_do_connect((const char *)ap->ssid, password);
	}

	if (!wifi_scan_ap_prompt_password(ap, password, sizeof(password)))
		return false;

	return wifi_do_connect((const char *)ap->ssid, password);
}


/*============================================================================*/
/**
  * @brief Save or forget the selected AP credentials without connecting
  */
/*============================================================================*/
static void wifi_scan_ap_save_selected(const wifi_scanlist_t *ap)
{
	wifi_credential_t cred;
	char password[WIFI_CRED_PASS_MAX_LEN];
	bool saved;

	if (ap == NULL)
		return;

	if (ap->ssid[0] == 0x00)
	{
		wifi_display_msg("Hidden network", "Can't save SSID");
		vTaskDelay(pdMS_TO_TICKS(1500));
		return;
	}

	if (wifi_cred_find((const char *)ap->ssid, &cred))
	{
		wifi_cred_delete((const char *)ap->ssid);
		wifi_display_msg("Forgot network", (const char *)ap->ssid);
		vTaskDelay(pdMS_TO_TICKS(1500));
		return;
	}

	if (!wifi_scan_ap_prompt_password(ap, password, sizeof(password)))
		return;

	saved = wifi_cred_save((const char *)ap->ssid, password);
	wifi_display_msg(saved ? "Saved network" : "Save failed", (const char *)ap->ssid);
	vTaskDelay(pdMS_TO_TICKS(1500));
}


/*============================================================================*/
/**
  * @brief Display a detail card for the selected AP
  */
/*============================================================================*/
static void wifi_scan_ap_show_details(const wifi_scanlist_t *ap)
{
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	char line[25];

	if (ap == NULL)
		return;

	while (1)
	{
		m1_u8g2_firstpage();
		m1_draw_header_bar(&m1_u8g2, "AP Details", NULL);
		m1_draw_content_frame(&m1_u8g2, 2, 14, 124, 42);
		u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
		m1_draw_text(&m1_u8g2, 8, 23, 114, wifi_scan_ssid_label(ap), TEXT_ALIGN_LEFT);
		m1_draw_text(&m1_u8g2, 8, 31, 114, (const char *)ap->bssid, TEXT_ALIGN_LEFT);
		snprintf(line, sizeof(line), "RSSI:%ddBm  Ch:%d", ap->rssi, ap->channel);
		m1_draw_text(&m1_u8g2, 8, 39, 114, line, TEXT_ALIGN_LEFT);
		snprintf(line, sizeof(line), "Auth:%s %s",
			wifi_auth_mode_to_str(ap->encryption_mode),
			wifi_channel_band_to_str(ap->channel));
		m1_draw_text(&m1_u8g2, 8, 47, 114, line, TEXT_ALIGN_LEFT);
		m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Back", "Close", NULL);
		m1_u8g2_nextpage();

		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
		if (ret == pdTRUE && q_item.q_evt_type == Q_EVENT_KEYPAD)
		{
			ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
			if (this_button_status.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK ||
				this_button_status.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK ||
				this_button_status.event[BUTTON_RIGHT_KP_ID] == BUTTON_EVENT_CLICK)
			{
				return;
			}
		}
	}
}


/*============================================================================*/
/**
  * @brief Show safe per-AP actions for a scan result
  * @retval 1 when the caller should redraw the scan list
  */
/*============================================================================*/
static uint8_t wifi_scan_ap_options(const wifi_scanlist_t *ap)
{
	enum
	{
		WIFI_SCAN_ACT_CONNECT = 0,
		WIFI_SCAN_ACT_SAVE,
		WIFI_SCAN_ACT_DETAILS,
#ifdef M1_APP_WIFI_OFFENSIVE_ENABLE
		WIFI_SCAN_ACT_DEAUTH,
		WIFI_SCAN_ACT_ADD_TARGET,
#endif
		WIFI_SCAN_ACT_COUNT
	};

	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	wifi_credential_t cred;
	const char *save_label;
	const char *option_label;
	uint8_t selected = 0;
	uint8_t visible_start;
	uint8_t row_y;

	if (ap == NULL)
		return 1;

	save_label = wifi_cred_find((const char *)ap->ssid, &cred) ? "Forget" : "Save";

	while (1)
	{
		m1_u8g2_firstpage();
		m1_draw_header_bar(&m1_u8g2, "WiFi Options", wifi_scan_ssid_label(ap));
		m1_draw_content_frame(&m1_u8g2, 2, 14, 124, 35);
		u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);

		visible_start = (selected >= 2U) ? (uint8_t)(selected - 1U) : 0U;
		for (uint8_t row = visible_start; row < WIFI_SCAN_ACT_COUNT && row < (visible_start + 2U); row++)
		{
			if (row == WIFI_SCAN_ACT_CONNECT)
				option_label = "Connect";
			else if (row == WIFI_SCAN_ACT_SAVE)
				option_label = save_label;
			else if (row == WIFI_SCAN_ACT_DETAILS)
				option_label = "Details";
#ifdef M1_APP_WIFI_OFFENSIVE_ENABLE
			else if (row == WIFI_SCAN_ACT_DEAUTH)
				option_label = "Deauth";
			else if (row == WIFI_SCAN_ACT_ADD_TARGET)
				option_label = "Add Target";
#endif
			else
				option_label = "???";

			row_y = (uint8_t)(30 + ((row - visible_start) * 12U));
			if (row == selected)
			{
				u8g2_DrawBox(&m1_u8g2, 6, row_y - 7, 116, 11);
				u8g2_SetDrawColor(&m1_u8g2, 0);
				m1_draw_text(&m1_u8g2, 10, row_y, 108, option_label, TEXT_ALIGN_LEFT);
				u8g2_SetDrawColor(&m1_u8g2, 1);
			}
			else
			{
				u8g2_DrawFrame(&m1_u8g2, 6, row_y - 7, 116, 11);
				m1_draw_text(&m1_u8g2, 10, row_y, 108, option_label, TEXT_ALIGN_LEFT);
			}
		}

		m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Back", "Select", NULL);
		m1_u8g2_nextpage();

		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
		if (ret != pdTRUE || q_item.q_evt_type != Q_EVENT_KEYPAD)
			continue;

		ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
		if (this_button_status.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK)
		{
			return 1;
		}
		else if (this_button_status.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK)
		{
			if (selected > 0)
				selected--;
			else
				selected = WIFI_SCAN_ACT_COUNT - 1;
		}
		else if (this_button_status.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK)
		{
			selected++;
			if (selected >= WIFI_SCAN_ACT_COUNT)
				selected = 0;
		}
		else if (this_button_status.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK ||
				 this_button_status.event[BUTTON_RIGHT_KP_ID] == BUTTON_EVENT_CLICK)
		{
			if (selected == WIFI_SCAN_ACT_CONNECT)
			{
				wifi_scan_ap_connect_selected(ap);
				return 1;
			}
			else if (selected == WIFI_SCAN_ACT_SAVE)
			{
				wifi_scan_ap_save_selected(ap);
				return 1;
			}
			else if (selected == WIFI_SCAN_ACT_DETAILS)
			{
				wifi_scan_ap_show_details(ap);
				return 1;
			}
#ifdef M1_APP_WIFI_OFFENSIVE_ENABLE
			else if (selected == WIFI_SCAN_ACT_DEAUTH)
			{
				/* Deauth this AP immediately */
				char bssid_str[18];
				strncpy(bssid_str, (const char *)ap->bssid, sizeof(bssid_str) - 1);
				bssid_str[sizeof(bssid_str) - 1] = '\0';
				wifi_display_panel("Deauth", bssid_str, "Sending deauths", "BACK to stop");
				wifi_esp_deauth_start(bssid_str, ap->channel, NULL, 0);
				/* Wait for BACK to stop */
				while (1)
				{
					ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
					if (ret == pdTRUE && q_item.q_evt_type == Q_EVENT_KEYPAD)
					{
						xQueueReceive(button_events_q_hdl, &this_button_status, 0);
						if (this_button_status.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK)
						{
							wifi_esp_deauth_stop();
							return 1;
						}
					}
				}
			}
			else if (selected == WIFI_SCAN_ACT_ADD_TARGET)
			{
				/* Add this AP to the attack target list */
				wifi_attack_add_target(ap);
				wifi_display_msg("Target", "added");
				vTaskDelay(pdMS_TO_TICKS(1000));
				return 1;
			}
#endif
		}
	}
}


/*============================================================================*/
/**
  * @brief WiFi config menu - now replaced by Saved Networks
  *        Kept for backward compat with old menu structure
  */
/*============================================================================*/
void wifi_config(void)
{
	/* Redirect to saved networks manager */
	wifi_saved_networks();
}


/*============================================================================*/
/**
  * @brief Saved networks management screen
  *        Shows list of saved WiFi credentials, allows connect/delete
  */
/*============================================================================*/
void wifi_saved_networks(void)
{
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	wifi_credential_t creds[WIFI_CRED_MAX_STORED];
	uint8_t cred_count;
	uint8_t sel_idx = 0;
	char prn_msg[25];
	uint8_t y_offset;

	/* Load saved credentials */
	cred_count = wifi_cred_load_all(creds, WIFI_CRED_MAX_STORED);

	/* Display the list */
	u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);

	if ( cred_count == 0 )
	{
		wifi_display_msg("No saved", "networks");
		/* Wait for BACK button */
		while (1)
		{
			ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
			if (ret==pdTRUE && q_item.q_evt_type==Q_EVENT_KEYPAD)
			{
				ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
				if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK )
				{
					xQueueReset(main_q_hdl);
					return;
				}
			}
		}
	}

	/* Draw credential list */
	while (1)
	{
		m1_u8g2_firstpage();
		sprintf(prn_msg, "%d/%d", sel_idx + 1, cred_count);
		m1_draw_header_bar(&m1_u8g2, "Saved WiFi", prn_msg);
		m1_draw_content_frame(&m1_u8g2, 2, 14, 124, 35);
		u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);

		y_offset = 24;

		/* Show selected network SSID */
		strncpy(prn_msg, creds[sel_idx].ssid, 20);
		prn_msg[20] = '\0';
		m1_draw_text(&m1_u8g2, 8, y_offset, 114, prn_msg, TEXT_ALIGN_LEFT);
		y_offset += 8;

		/* Instructions */
		m1_draw_text(&m1_u8g2, 8, y_offset, 114, "OK connect", TEXT_ALIGN_LEFT);
		y_offset += 8;
		m1_draw_text(&m1_u8g2, 8, y_offset, 114, M1_WIFI_BAND_NOTE, TEXT_ALIGN_LEFT);
		y_offset += 8;
		m1_draw_text(&m1_u8g2, 8, y_offset, 114, "RIGHT delete", TEXT_ALIGN_LEFT);

		m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Back", "Delete", arrowright_8x8);

		m1_u8g2_nextpage();

		/* Wait for button input */
		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
		if (ret==pdTRUE && q_item.q_evt_type==Q_EVENT_KEYPAD)
		{
			ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
			if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK )
			{
				xQueueReset(main_q_hdl);
				return;
			}
			else if ( this_button_status.event[BUTTON_UP_KP_ID]==BUTTON_EVENT_CLICK )
			{
				if ( sel_idx > 0 )
					sel_idx--;
				else
					sel_idx = cred_count - 1;
			}
			else if ( this_button_status.event[BUTTON_DOWN_KP_ID]==BUTTON_EVENT_CLICK )
			{
				sel_idx++;
				if ( sel_idx >= cred_count )
					sel_idx = 0;
			}
			else if ( this_button_status.event[BUTTON_OK_KP_ID]==BUTTON_EVENT_CLICK )
			{
				/* Connect using saved credentials */
				u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
				if ( !wifi_ensure_esp32_ready() )
				{
					wifi_display_msg("ESP32", "not ready!");
					vTaskDelay(pdMS_TO_TICKS(2000));
				}
				else
				{
					wifi_do_connect(creds[sel_idx].ssid, creds[sel_idx].password);
				}
				/* Redraw list */
				u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
			}
			else if ( this_button_status.event[BUTTON_RIGHT_KP_ID]==BUTTON_EVENT_CLICK )
			{
				/* Delete credential */
				wifi_display_msg("Deleting...", creds[sel_idx].ssid);
				wifi_cred_delete(creds[sel_idx].ssid);
				vTaskDelay(pdMS_TO_TICKS(1000));

				/* Reload list */
				cred_count = wifi_cred_load_all(creds, WIFI_CRED_MAX_STORED);
				if ( cred_count == 0 )
				{
					wifi_display_msg("No saved", "networks");
					vTaskDelay(pdMS_TO_TICKS(1500));
					xQueueReset(main_q_hdl);
					return;
				}
				if ( sel_idx >= cred_count )
					sel_idx = cred_count - 1;
				u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
			}
		}
	} // while(1)
} // void wifi_saved_networks(void)



/*============================================================================*/
/**
  * @brief Show WiFi connection status - IP, SSID, RSSI, MAC
  */
/*============================================================================*/
void wifi_show_status(void)
{
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	ctrl_cmd_t ip_req;
	char prn_msg[25];
	uint8_t y_offset;

	u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);

	if ( !wifi_ensure_esp32_ready() )
	{
		wifi_display_msg("ESP32", "not ready!");
		/* Wait for BACK */
		while (1)
		{
			ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
			if (ret==pdTRUE && q_item.q_evt_type==Q_EVENT_KEYPAD)
			{
				ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
				if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK )
				{
					xQueueReset(main_q_hdl);
					m1_esp32_deinit();
					return;
				}
			}
		}
	}

	/* Query IP/MAC from ESP32 */
	wifi_display_busy("Getting status...");

	memset(&ip_req, 0, sizeof(ip_req));
	ip_req.msg_type = CTRL_REQ;
	ip_req.cmd_timeout_sec = 10;
	wifi_get_ip(&ip_req);

	/* Display status */
	m1_u8g2_firstpage();
	m1_draw_header_bar(&m1_u8g2, "WiFi Status", s_wifi_connected ? "Live" : "Idle");
	m1_draw_content_frame(&m1_u8g2, 2, 14, 124, 35);
	u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);

	y_offset = 24;

	if ( s_wifi_connected && s_connected_ssid[0] )
	{
		strncpy(prn_msg, s_connected_ssid, 20);
		prn_msg[20] = '\0';
		m1_draw_text(&m1_u8g2, 8, y_offset, 114, prn_msg, TEXT_ALIGN_LEFT);
	}
	else
	{
		m1_draw_text(&m1_u8g2, 8, y_offset, 114, "Not connected", TEXT_ALIGN_LEFT);
	}
	y_offset += 8;

	if ( ip_req.u.wifi_ap_config.status[0]
		&& strcmp(ip_req.u.wifi_ap_config.status, "0.0.0.0") != 0 )
	{
		snprintf(prn_msg, sizeof(prn_msg), "IP:%s", ip_req.u.wifi_ap_config.status);
		m1_draw_text(&m1_u8g2, 8, y_offset, 114, prn_msg, TEXT_ALIGN_LEFT);
		s_wifi_connected = true;
	}
	else
	{
		m1_draw_text(&m1_u8g2, 8, y_offset, 114, "IP: N/A", TEXT_ALIGN_LEFT);
		s_wifi_connected = false;
	}
	y_offset += 8;

	if ( ip_req.u.wifi_ap_config.out_mac[0] )
	{
		m1_draw_text(&m1_u8g2, 8, y_offset, 114, ip_req.u.wifi_ap_config.out_mac, TEXT_ALIGN_LEFT);
	}
	y_offset += 8;

	m1_draw_text(&m1_u8g2, 8, y_offset, 114, M1_WIFI_BAND_NOTE, TEXT_ALIGN_LEFT);
	m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Back", "OK", arrowright_8x8);
	m1_u8g2_nextpage();

	/* Wait for BACK button */
	while (1)
	{
		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
		if (ret==pdTRUE && q_item.q_evt_type==Q_EVENT_KEYPAD)
		{
			ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
			if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK )
			{
				xQueueReset(main_q_hdl);
				m1_esp32_deinit();
				return;
			}
		}
	}
} // void wifi_show_status(void)


/*============================================================================*/
/**
  * @brief Show current WiFi mode reported by ESP32 AT firmware
  */
/*============================================================================*/
void wifi_show_mode(void)
{
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	ctrl_cmd_t mode_req = CTRL_CMD_DEFAULT_REQ();
	char line1[24];
	const char *mode_text = "Unknown";

	u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);

	if ( !wifi_ensure_esp32_ready() )
	{
		wifi_display_msg("ESP32", "not ready!");
		vTaskDelay(pdMS_TO_TICKS(2000));
		xQueueReset(main_q_hdl);
		return;
	}

	wifi_display_busy("Getting mode...");
	mode_req.cmd_timeout_sec = 8;

	if ( wifi_get_mode(&mode_req) != SUCCESS )
	{
		wifi_display_msg("Mode query", "failed");
		vTaskDelay(pdMS_TO_TICKS(1500));
		m1_esp32_deinit();
		xQueueReset(main_q_hdl);
		return;
	}

	if ( strcmp(mode_req.u.wifi_ap_config.status, "STA") == 0 )
		mode_text = "Station";
	else if ( strcmp(mode_req.u.wifi_ap_config.status, "AP") == 0 )
		mode_text = "Soft AP";
	else if ( strcmp(mode_req.u.wifi_ap_config.status, "APSTA") == 0 )
		mode_text = "AP + STA";
	else if ( strcmp(mode_req.u.wifi_ap_config.status, "NULL") == 0 )
		mode_text = "WiFi Off";

	snprintf(line1, sizeof(line1), "Mode: %s", mode_text);

	m1_u8g2_firstpage();
	m1_draw_header_bar(&m1_u8g2, "WiFi Mode",
					  mode_req.u.wifi_ap_config.status[0] ? mode_req.u.wifi_ap_config.status : "OK");
	m1_draw_content_frame(&m1_u8g2, 2, 14, 124, 35);
	u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
	m1_draw_text(&m1_u8g2, 8, 24, 114, line1, TEXT_ALIGN_LEFT);
	m1_draw_text(&m1_u8g2, 8, 34, 114, "New ESP32 command", TEXT_ALIGN_LEFT);
	m1_draw_text(&m1_u8g2, 8, 44, 114, "path is responding", TEXT_ALIGN_LEFT);
	m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Back", "Refresh", arrowright_8x8);
	m1_u8g2_nextpage();

	while (1)
	{
		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
		if (ret==pdTRUE && q_item.q_evt_type==Q_EVENT_KEYPAD)
		{
			ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
			if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK )
			{
				xQueueReset(main_q_hdl);
				m1_esp32_deinit();
				return;
			}
			if ( this_button_status.event[BUTTON_OK_KP_ID]==BUTTON_EVENT_CLICK
				|| this_button_status.event[BUTTON_RIGHT_KP_ID]==BUTTON_EVENT_CLICK )
			{
				xQueueReset(main_q_hdl);
				wifi_show_mode();
				return;
			}
		}
	}
} // void wifi_show_mode(void)


/*============================================================================*/
/**
  * @brief Show extended WiFi stats from custom ESP32 AT command
  */
/*============================================================================*/
void wifi_show_stats(void)
{
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	ctrl_cmd_t stats_req;
	char body_lines[6][24];
	uint8_t scroll = 0U;
	bool needs_query = true;
	bool needs_redraw = true;

	u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);

	while (1)
	{
		if (needs_query)
		{
			memset(&stats_req, 0, sizeof(stats_req));
			stats_req.msg_type = CTRL_REQ;
			stats_req.cmd_timeout_sec = 8;

			if ( !wifi_ensure_esp32_ready() )
			{
				wifi_display_msg("ESP32", "not ready!");
				vTaskDelay(pdMS_TO_TICKS(2000));
				xQueueReset(main_q_hdl);
				return;
			}

			wifi_display_busy("Getting stats...");
			if ( wifi_get_stats(&stats_req) != SUCCESS )
			{
				wifi_display_msg("Stats query", "failed");
				vTaskDelay(pdMS_TO_TICKS(1500));
				m1_esp32_deinit();
				xQueueReset(main_q_hdl);
				return;
			}

			snprintf(body_lines[0], sizeof(body_lines[0]), "Link: %s",
			         (stats_req.u.wifi_ap_config.band_mode != 0) ? "Connected" : "Idle");
			snprintf(body_lines[1], sizeof(body_lines[1]), "Mode: %s",
			         stats_req.u.wifi_ap_config.status[0] ? stats_req.u.wifi_ap_config.status : "Unknown");
			snprintf(body_lines[2], sizeof(body_lines[2]), "RSSI: %d dBm",
			         stats_req.u.wifi_ap_config.rssi);
			snprintf(body_lines[3], sizeof(body_lines[3]), "Channel: %d",
			         stats_req.u.wifi_ap_config.channel);
			snprintf(body_lines[4], sizeof(body_lines[4]), "IP: %s",
			         stats_req.u.wifi_ap_config.out_mac[0] ? stats_req.u.wifi_ap_config.out_mac : "0.0.0.0");
			snprintf(body_lines[5], sizeof(body_lines[5]), "BSSID:%s",
			         stats_req.u.wifi_ap_config.bssid[0] ?
			             (const char *)stats_req.u.wifi_ap_config.bssid : "none");

			if (scroll > 2U)
			{
				scroll = 2U;
			}
			needs_query = false;
			needs_redraw = true;
		}

		if (needs_redraw)
		{
			wifi_draw_stats_page(body_lines, scroll);
			needs_redraw = false;
		}

		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
		if (ret==pdTRUE && q_item.q_evt_type==Q_EVENT_KEYPAD)
		{
			ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
			if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK )
			{
				xQueueReset(main_q_hdl);
				m1_esp32_deinit();
				return;
			}
			if ( this_button_status.event[BUTTON_UP_KP_ID]==BUTTON_EVENT_CLICK )
			{
				if (scroll > 0U)
				{
					scroll--;
					needs_redraw = true;
				}
			}
			if ( this_button_status.event[BUTTON_DOWN_KP_ID]==BUTTON_EVENT_CLICK )
			{
				if (scroll < 2U)
				{
					scroll++;
					needs_redraw = true;
				}
			}
			if ( this_button_status.event[BUTTON_OK_KP_ID]==BUTTON_EVENT_CLICK
				|| this_button_status.event[BUTTON_RIGHT_KP_ID]==BUTTON_EVENT_CLICK )
			{
				needs_query = true;
			}
		}
	}
} // void wifi_show_stats(void)


/*============================================================================*/
/**
  * @brief Draw cached WiFi stats without re-querying the ESP32
  */
/*============================================================================*/
static void wifi_draw_stats_page(char body_lines[][24], uint8_t scroll)
{
	char badge[16];

	if (scroll > 2U)
	{
		scroll = 2U;
	}

	snprintf(badge, sizeof(badge), "%u-%u/6",
	         (unsigned)(scroll + 1U), (unsigned)(scroll + 4U));

	m1_u8g2_firstpage();
	m1_draw_header_bar(&m1_u8g2, "WiFi Stats", badge);
	m1_draw_content_frame(&m1_u8g2, 2, 14, 124, 35);
	u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
	m1_draw_text(&m1_u8g2, 8, 22, 114, body_lines[scroll], TEXT_ALIGN_LEFT);
	m1_draw_text(&m1_u8g2, 8, 30, 114, body_lines[scroll + 1U], TEXT_ALIGN_LEFT);
	m1_draw_text(&m1_u8g2, 8, 38, 114, body_lines[scroll + 2U], TEXT_ALIGN_LEFT);
	m1_draw_text(&m1_u8g2, 8, 46, 114, body_lines[scroll + 3U], TEXT_ALIGN_LEFT);
	m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Back", "Refresh", arrowright_8x8);
	m1_u8g2_nextpage();
}


/*============================================================================*/
/**
  * @brief Disconnect from current WiFi network
  */
/*============================================================================*/
void wifi_disconnect(void)
{
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	BaseType_t ret;

	u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);

	if ( !wifi_ensure_esp32_ready() )
	{
		wifi_display_msg("ESP32", "not ready!");
		vTaskDelay(pdMS_TO_TICKS(2000));
		xQueueReset(main_q_hdl);
		return;
	}

	if ( !s_wifi_connected )
	{
		wifi_display_msg("WiFi", "Not connected");
		vTaskDelay(pdMS_TO_TICKS(2000));
		m1_esp32_deinit();
		xQueueReset(main_q_hdl);
		return;
	}

	wifi_display_busy("Disconnecting...");

	ctrl_cmd_t disc_req = CTRL_CMD_DEFAULT_REQ();
	disc_req.cmd_timeout_sec = 10;
	uint8_t result = wifi_disconnect_ap(&disc_req);

	if ( result == SUCCESS )
	{
		s_wifi_connected = false;
		s_connected_ssid[0] = '\0';
		wifi_display_msg("WiFi", "Disconnected");
		M1_LOG_I(M1_LOGDB_TAG, "WiFi disconnected\n\r");
	}
	else
	{
		wifi_display_msg("Disconnect", "failed!");
		M1_LOG_E(M1_LOGDB_TAG, "WiFi disconnect failed\n\r");
	}

	vTaskDelay(pdMS_TO_TICKS(2000));
	m1_esp32_deinit();

	/* Wait for any pending button press then return */
	xQueueReset(main_q_hdl);

	while (1)
	{
		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
		if (ret==pdTRUE && q_item.q_evt_type==Q_EVENT_KEYPAD)
		{
			ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
			if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK )
			{
				xQueueReset(main_q_hdl);
				return;
			}
		}
	}
} // void wifi_disconnect(void)


/*============================================================================*/
/**
  * @brief Sync system RTC with WiFi NTP
  * @retval 1 on success, 0 on failure
  */
/*============================================================================*/
uint8_t wifi_sync_rtc(void)
{
	char resp[128];
	char month_str[4];
	char day_name[4];
	int day, year, hour, min, sec;
	m1_time_t dt;
	const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", 
	                        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
	const char *days_names[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};

	if ( !wifi_ensure_esp32_ready() ) return 0;

	/* Configure SNTP: enable, timezone 0 (UTC), server — offset applied locally */
	spi_AT_send_recv("AT+CIPSNTPCFG=1,0,\"pool.ntp.org\"\r\n", resp, sizeof(resp), 2);

	/* Wait for sync (up to 5 seconds) */
	for (int i = 0; i < 5; i++)
	{
		vTaskDelay(pdMS_TO_TICKS(1000));
		memset(resp, 0, sizeof(resp));
		spi_AT_send_recv("AT+CIPSNTPTIME?\r\n", resp, sizeof(resp), 2);

		/* Expecting: +CIPSNTPTIME:Mon Mar 23 12:34:56 2026 */
		char *p = strstr(resp, "+CIPSNTPTIME:");
		if ( p )
		{
			p += 13;
			if ( sscanf(p, "%3s %3s %d %d:%d:%d %d", 
			            day_name, month_str, &day, &hour, &min, &sec, &year) == 7 )
			{
				dt.year = (uint16_t)year;
				dt.day = (uint8_t)day;
				dt.hour = (uint8_t)hour;
				dt.minute = (uint8_t)min;
				dt.second = (uint8_t)sec;
				
				dt.month = 0;
				for (int m = 0; m < 12; m++) {
					if (strcmp(month_str, months[m]) == 0) {
						dt.month = m + 1;
						break;
					}
				}

				dt.weekday = 0;
				for (int d = 0; d < 7; d++) {
					if (strcmp(day_name, days_names[d]) == 0) {
						dt.weekday = d + 1;
						break;
					}
				}

				if (dt.month > 0 && dt.weekday > 0) {
					m1_set_datetime(&dt);
					return 1;
				}
			}
		}
	}
	return 0;
}


/*============================================================================*/
/**
  * @brief User-facing WiFi RTC sync tool
  */
/*============================================================================*/
void wifi_sync_rtc_tool(void)
{
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	BaseType_t ret;

	u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
	wifi_display_busy("Syncing RTC...");

	if ( wifi_sync_rtc() )
		wifi_display_msg("RTC synced", "via WiFi");
	else
		wifi_display_msg("RTC sync", "failed");

	vTaskDelay(pdMS_TO_TICKS(2000));
	m1_esp32_deinit();
	xQueueReset(main_q_hdl);

	while (1)
	{
		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
		if (ret==pdTRUE && q_item.q_evt_type==Q_EVENT_KEYPAD)
		{
			ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
			if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK )
			{
				xQueueReset(main_q_hdl);
				return;
			}
		}
	}
} // void wifi_sync_rtc_tool(void)


/*============================================================================*/
/*  Offensive WiFi — Attack Target List                              */
/*============================================================================*/

#ifdef M1_APP_WIFI_OFFENSIVE_ENABLE

#define WIFI_ATTACK_MAX_TARGETS 16

typedef struct {
	char bssid[18];		/* "xx:xx:xx:xx:xx:xx" */
	char ssid[33];		/* SSID or "*hidden*" */
	uint8_t channel;
} wifi_attack_target_t;

static wifi_attack_target_t s_attack_targets[WIFI_ATTACK_MAX_TARGETS];
static uint8_t s_attack_target_count = 0;

/*============================================================================*/
/**
  * @brief Add a scanned AP to the attack target list
  */
/*============================================================================*/
static void wifi_attack_add_target(const wifi_scanlist_t *ap)
{
	if (ap == NULL || s_attack_target_count >= WIFI_ATTACK_MAX_TARGETS)
		return;

	/* Check for duplicates */
	char bssid_str[18];
	strncpy(bssid_str, (const char *)ap->bssid, sizeof(bssid_str) - 1);
	bssid_str[sizeof(bssid_str) - 1] = '\0';

	for (uint8_t i = 0; i < s_attack_target_count; i++) {
		if (strcmp(s_attack_targets[i].bssid, bssid_str) == 0)
			return;  /* already in list */
	}

	strncpy(s_attack_targets[s_attack_target_count].bssid, bssid_str, 17);
	s_attack_targets[s_attack_target_count].bssid[17] = '\0';

	if (ap->ssid[0] == 0x00)
		strncpy(s_attack_targets[s_attack_target_count].ssid, "*hidden*", 32);
	else {
		strncpy(s_attack_targets[s_attack_target_count].ssid, (const char *)ap->ssid, 32);
		s_attack_targets[s_attack_target_count].ssid[32] = '\0';
	}

	s_attack_targets[s_attack_target_count].channel = ap->channel;
	s_attack_target_count++;
}

/*============================================================================*/
/**
  * @brief Per-target offensive actions menu (opened from Attack List on OK)
  * @retval true if the target was removed from the list
  */
/*============================================================================*/
static bool wifi_attack_target_actions(uint8_t idx)
{
	enum
	{
		WIFI_ATK_ACT_DEAUTH = 0,
		WIFI_ATK_ACT_HS_CAPTURE,
		WIFI_ATK_ACT_PMKID,
		WIFI_ATK_ACT_BEACON_CLONE,
		WIFI_ATK_ACT_PROBE_SNIFF,
		WIFI_ATK_ACT_KARMA,
		WIFI_ATK_ACT_REMOVE,
		WIFI_ATK_ACT_COUNT
	};

	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	uint8_t selected = 0;
	uint8_t visible_start;
	uint8_t row_y;
	const char *option_label;
	bool removed = false;

	if (idx >= s_attack_target_count)
		return false;

	while (1)
	{
		m1_u8g2_firstpage();
		m1_draw_header_bar(&m1_u8g2, "Target Actions", s_attack_targets[idx].ssid);
		m1_draw_content_frame(&m1_u8g2, 2, 14, 124, 35);
		u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);

		visible_start = (selected >= 2U) ? (uint8_t)(selected - 1U) : 0U;
		for (uint8_t row = visible_start; row < WIFI_ATK_ACT_COUNT && row < (visible_start + 2U); row++)
		{
			if (row == WIFI_ATK_ACT_DEAUTH)
				option_label = "Deauth";
			else if (row == WIFI_ATK_ACT_HS_CAPTURE)
				option_label = "HS Capture";
			else if (row == WIFI_ATK_ACT_PMKID)
				option_label = "PMKID Capture";
			else if (row == WIFI_ATK_ACT_BEACON_CLONE)
				option_label = "Clone Beacon";
			else if (row == WIFI_ATK_ACT_PROBE_SNIFF)
				option_label = "Probe Sniff";
			else if (row == WIFI_ATK_ACT_KARMA)
				option_label = "Karma";
			else if (row == WIFI_ATK_ACT_REMOVE)
				option_label = "Remove Target";
			else
				option_label = "???";

			row_y = (uint8_t)(30 + ((row - visible_start) * 12U));
			if (row == selected)
			{
				u8g2_DrawBox(&m1_u8g2, 6, row_y - 7, 116, 11);
				u8g2_SetDrawColor(&m1_u8g2, 0);
				m1_draw_text(&m1_u8g2, 10, row_y, 108, option_label, TEXT_ALIGN_LEFT);
				u8g2_SetDrawColor(&m1_u8g2, 1);
			}
			else
			{
				u8g2_DrawFrame(&m1_u8g2, 6, row_y - 7, 116, 11);
				m1_draw_text(&m1_u8g2, 10, row_y, 108, option_label, TEXT_ALIGN_LEFT);
			}
		}

		m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Back", "Select", NULL);
		m1_u8g2_nextpage();

		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
		if (ret != pdTRUE || q_item.q_evt_type != Q_EVENT_KEYPAD)
			continue;

		xQueueReceive(button_events_q_hdl, &this_button_status, 0);

		if (this_button_status.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK)
			return removed;

		if (this_button_status.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK)
		{
			if (selected > 0) selected--;
			else selected = WIFI_ATK_ACT_COUNT - 1;
		}
		else if (this_button_status.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK)
		{
			selected++;
			if (selected >= WIFI_ATK_ACT_COUNT) selected = 0;
		}
		else if (this_button_status.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK ||
				 this_button_status.event[BUTTON_RIGHT_KP_ID] == BUTTON_EVENT_CLICK)
		{
			if (selected == WIFI_ATK_ACT_DEAUTH)
			{
				wifi_display_panel("Deauth",
					s_attack_targets[idx].bssid,
					"Sending deauths",
					"BACK to stop");
				wifi_esp_deauth_start(s_attack_targets[idx].bssid,
									  s_attack_targets[idx].channel, NULL, 0);
				while (1)
				{
					ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
					if (ret == pdTRUE && q_item.q_evt_type == Q_EVENT_KEYPAD)
					{
						xQueueReceive(button_events_q_hdl, &this_button_status, 0);
						if (this_button_status.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK)
						{
							wifi_esp_deauth_stop();
							break;
						}
					}
				}
			}
			else if (selected == WIFI_ATK_ACT_HS_CAPTURE)
			{
				wifi_display_panel("HS Capture",
					s_attack_targets[idx].bssid,
					"Deauthing &",
					"grabbing EAPOL...");
				if (wifi_esp_hscap_start(s_attack_targets[idx].bssid,
										 s_attack_targets[idx].channel, 5) == SUCCESS)
					wifi_display_msg("HS Capture", "Got frames!");
				else
					wifi_display_msg("HS Capture", "Timeout");
				vTaskDelay(pdMS_TO_TICKS(1500));
			}
			else if (selected == WIFI_ATK_ACT_BEACON_CLONE)
			{
				const char *ssids[1];
				ssids[0] = s_attack_targets[idx].ssid;
				wifi_display_panel("Clone Beacon",
					s_attack_targets[idx].ssid,
					"Broadcasting",
					"BACK to stop");
				if (wifi_esp_beacon_start(ssids, 1, s_attack_targets[idx].channel) == SUCCESS)
				{
					while (1)
					{
						ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
						if (ret == pdTRUE && q_item.q_evt_type == Q_EVENT_KEYPAD)
						{
							xQueueReceive(button_events_q_hdl, &this_button_status, 0);
							if (this_button_status.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK)
							{
								wifi_esp_beacon_stop();
								break;
							}
						}
					}
				}
				else
				{
					wifi_display_msg("Clone Beacon", "Start failed");
					vTaskDelay(pdMS_TO_TICKS(1500));
				}
			}
			else if (selected == WIFI_ATK_ACT_PMKID)
			{
				wifi_display_panel("PMKID Capture",
					s_attack_targets[idx].bssid,
					"Requesting PMKID",
					"Please wait...");
				if (wifi_esp_pmkid_capture(s_attack_targets[idx].bssid,
										   s_attack_targets[idx].channel) == SUCCESS)
					wifi_display_msg("PMKID", "Captured!");
				else
					wifi_display_msg("PMKID", "Failed");
				vTaskDelay(pdMS_TO_TICKS(1500));
			}
			else if (selected == WIFI_ATK_ACT_PROBE_SNIFF)
			{
				wifi_display_panel("Probe Sniff",
					s_attack_targets[idx].ssid,
					"Sniffing probes",
					"BACK to stop");
				if (wifi_esp_probe_start(s_attack_targets[idx].channel, 300) == SUCCESS)
				{
					while (1)
					{
						ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
						if (ret == pdTRUE && q_item.q_evt_type == Q_EVENT_KEYPAD)
						{
							xQueueReceive(button_events_q_hdl, &this_button_status, 0);
							if (this_button_status.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK)
							{
								wifi_esp_probe_stop();
								break;
							}
						}
					}
				}
				else
				{
					wifi_display_msg("Probe Sniff", "Start failed");
					vTaskDelay(pdMS_TO_TICKS(1500));
				}
			}
			else if (selected == WIFI_ATK_ACT_KARMA)
			{
				wifi_display_panel("Karma",
					s_attack_targets[idx].ssid,
					"Responding to probes",
					"BACK to stop");
				if (wifi_esp_karma_start(s_attack_targets[idx].channel) == SUCCESS)
				{
					while (1)
					{
						ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
						if (ret == pdTRUE && q_item.q_evt_type == Q_EVENT_KEYPAD)
						{
							xQueueReceive(button_events_q_hdl, &this_button_status, 0);
							if (this_button_status.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK)
							{
								wifi_esp_karma_stop();
								break;
							}
						}
					}
				}
				else
				{
					wifi_display_msg("Karma", "Start failed");
					vTaskDelay(pdMS_TO_TICKS(1500));
				}
			}
			else if (selected == WIFI_ATK_ACT_REMOVE)
			{
				for (uint8_t i = idx; i + 1 < s_attack_target_count; i++)
					s_attack_targets[i] = s_attack_targets[i + 1];
				s_attack_target_count--;
				removed = true;
				return removed;
			}
		}
	}
}


/*============================================================================*/
/**
  * @brief Attack List — manage deauth targets, start/stop attacks
  */
/*============================================================================*/
void wifi_attack_list(void)
{
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	uint8_t sel_idx = 0;
	bool attacking = false;
	char line1[24];
	char line2[24];

	/* Ensure ESP32 is ready */
	if (!wifi_ensure_esp32_ready()) {
		wifi_display_msg("ESP32", "not ready!");
		vTaskDelay(pdMS_TO_TICKS(2000));
		return;
	}

	u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);

	while (1)
	{
		m1_u8g2_firstpage();

		if (s_attack_target_count == 0)
		{
			m1_draw_header_bar(&m1_u8g2, "Attack List", "Empty");
			m1_draw_content_frame(&m1_u8g2, 2, 14, 124, 35);
			u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
			m1_draw_text(&m1_u8g2, 8, 30, 114, "Scan & Add targets", TEXT_ALIGN_LEFT);
			m1_draw_text(&m1_u8g2, 8, 40, 114, "from scan menu", TEXT_ALIGN_LEFT);
		}
		else
		{
			snprintf(line1, sizeof(line1), "%u/%u %s",
					sel_idx + 1, s_attack_target_count,
					attacking ? "ATTACK" : "");
			m1_draw_header_bar(&m1_u8g2, "Attack List", line1);
			m1_draw_content_frame(&m1_u8g2, 2, 14, 124, 35);
			u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);

			/* Show selected target */
			strncpy(line1, s_attack_targets[sel_idx].ssid, 20);
			line1[20] = '\0';
			m1_draw_text(&m1_u8g2, 8, 24, 114, line1, TEXT_ALIGN_LEFT);

			m1_draw_text(&m1_u8g2, 8, 34, 114,
				s_attack_targets[sel_idx].bssid, TEXT_ALIGN_LEFT);

			snprintf(line2, sizeof(line2), "Ch:%u",
				s_attack_targets[sel_idx].channel);
			m1_draw_text(&m1_u8g2, 8, 44, 114, line2, TEXT_ALIGN_LEFT);
		}

		m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Back",
							attacking ? "Stop" : "Actions", arrowright_8x8);
		m1_u8g2_nextpage();

		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
		if (ret != pdTRUE || q_item.q_evt_type != Q_EVENT_KEYPAD)
			continue;

		xQueueReceive(button_events_q_hdl, &this_button_status, 0);

		if (this_button_status.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK)
		{
			if (attacking) {
				wifi_esp_deauth_stop();
				attacking = false;
			}
			xQueueReset(main_q_hdl);
			return;
		}
		else if (this_button_status.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK)
		{
			if (s_attack_target_count > 0) {
				if (sel_idx > 0) sel_idx--;
				else sel_idx = s_attack_target_count - 1;
			}
		}
		else if (this_button_status.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK)
		{
			if (s_attack_target_count > 0) {
				sel_idx++;
				if (sel_idx >= s_attack_target_count) sel_idx = 0;
			}
		}
		else if (this_button_status.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK)
		{
			if (s_attack_target_count == 0) continue;

			if (attacking) {
				/* Stop any in-progress attack before opening actions menu */
				wifi_esp_deauth_stop();
				attacking = false;
			}
			/* Open per-target offensive actions menu */
			if (wifi_attack_target_actions(sel_idx)) {
				/* Target was removed — clamp index */
				if (s_attack_target_count == 0) sel_idx = 0;
				else if (sel_idx >= s_attack_target_count)
					sel_idx = s_attack_target_count - 1;
			}
		}
		else if (this_button_status.event[BUTTON_RIGHT_KP_ID] == BUTTON_EVENT_CLICK)
		{
			if (s_attack_target_count == 0) continue;

			if (!attacking) {
				/* Deauth ALL targets */
				for (uint8_t i = 0; i < s_attack_target_count; i++) {
					wifi_esp_deauth_start(s_attack_targets[i].bssid,
									  s_attack_targets[i].channel, NULL, 0);
					vTaskDelay(pdMS_TO_TICKS(100));
				}
				attacking = true;
			} else {
				wifi_esp_deauth_stop();
				attacking = false;
			}
		}
	}
}

/*============================================================================*/
/**
  * @brief Beacon Spam — broadcast fake AP SSIDs
  */
/*============================================================================*/
void wifi_beacon_spam(void)
{
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	bool active = false;
	uint8_t channel = 6;
	char ssid_input[33] = "FreeWiFi";
	uint8_t edit_pos = 0;
	enum { MODE_CONFIG, MODE_RUNNING } mode = MODE_CONFIG;

	/* Ensure ESP32 is ready */
	if (!wifi_ensure_esp32_ready()) {
		wifi_display_msg("ESP32", "not ready!");
		vTaskDelay(pdMS_TO_TICKS(2000));
		return;
	}

	u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);

	while (1)
	{
		if (mode == MODE_CONFIG)
		{
			m1_u8g2_firstpage();
			m1_draw_header_bar(&m1_u8g2, "Beacon Spam", active ? "ON" : "OFF");
			m1_draw_content_frame(&m1_u8g2, 2, 14, 124, 35);
			u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);

			char ch_line[24];
			snprintf(ch_line, sizeof(ch_line), "Ch:%u SSID:", channel);
			m1_draw_text(&m1_u8g2, 8, 24, 114, ch_line, TEXT_ALIGN_LEFT);
			m1_draw_text(&m1_u8g2, 8, 34, 114, ssid_input, TEXT_ALIGN_LEFT);

			m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Back", "Start", arrowright_8x8);
			m1_u8g2_nextpage();

			ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
			if (ret != pdTRUE || q_item.q_evt_type != Q_EVENT_KEYPAD) continue;
			xQueueReceive(button_events_q_hdl, &this_button_status, 0);

			if (this_button_status.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) {
				if (active) { wifi_esp_beacon_stop(); active = false; }
				xQueueReset(main_q_hdl); return;
			}
			else if (this_button_status.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK) {
				if (channel < 14) channel++; else channel = 1;
			}
			else if (this_button_status.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK) {
				if (channel > 1) channel--; else channel = 14;
			}
			else if (this_button_status.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK) {
				/* Start beacon spam */
				const char *ssids[] = {ssid_input};
				if (wifi_esp_beacon_start(ssids, 1, channel) == SUCCESS)
					active = true;
				else
					wifi_display_msg("Beacon", "start failed");
			}
			else if (this_button_status.event[BUTTON_RIGHT_KP_ID] == BUTTON_EVENT_CLICK) {
				if (active) { wifi_esp_beacon_stop(); active = false; }
			}
		}
	}
}

/*============================================================================*/
/**
  * @brief Probe Request Sniffer
  */
/*============================================================================*/
void wifi_probe_sniff(void)
{
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	bool active = false;
	uint8_t channel = 6;
	int32_t duration = 30;
	char line1[24];

	/* Ensure ESP32 is ready */
	if (!wifi_ensure_esp32_ready()) {
		wifi_display_msg("ESP32", "not ready!");
		vTaskDelay(pdMS_TO_TICKS(2000));
		return;
	}

	u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);

	while (1)
	{
		m1_u8g2_firstpage();
		m1_draw_header_bar(&m1_u8g2, "Probe Sniff", active ? "LIVE" : "Idle");
		m1_draw_content_frame(&m1_u8g2, 2, 14, 124, 35);
		u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);

		snprintf(line1, sizeof(line1), "Ch:%u Dur:%lds", channel, (long)duration);
		m1_draw_text(&m1_u8g2, 8, 24, 114, line1, TEXT_ALIGN_LEFT);
		m1_draw_text(&m1_u8g2, 8, 36, 114,
			active ? "Sniffing probes..." : "OK to start", TEXT_ALIGN_LEFT);

		m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Back",
							active ? "Stop" : "Start", NULL);
		m1_u8g2_nextpage();

		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
		if (ret != pdTRUE || q_item.q_evt_type != Q_EVENT_KEYPAD) continue;
		xQueueReceive(button_events_q_hdl, &this_button_status, 0);

		if (this_button_status.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) {
			if (active) { wifi_esp_probe_stop(); active = false; }
			xQueueReset(main_q_hdl); return;
		}
		else if (this_button_status.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK) {
			if (!active) { if (channel < 14) channel++; else channel = 1; }
		}
		else if (this_button_status.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK) {
			if (!active) { if (channel > 1) channel--; else channel = 14; }
		}
		else if (this_button_status.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK) {
			if (!active) {
				if (wifi_esp_probe_start(channel, duration) == SUCCESS)
					active = true;
			} else {
				wifi_esp_probe_stop(); active = false;
			}
		}
	}
}

/*============================================================================*/
/**
  * @brief PMKID Capture
  */
/*============================================================================*/
void wifi_pmkid_capture(void)
{
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	char bssid_input[18] = "";
	uint8_t channel = 6;
	uint8_t input_pos = 0;
	enum { MODE_INPUT, MODE_RUNNING } mode = MODE_INPUT;

	/* Ensure ESP32 is ready */
	if (!wifi_ensure_esp32_ready()) {
		wifi_display_msg("ESP32", "not ready!");
		vTaskDelay(pdMS_TO_TICKS(2000));
		return;
	}

	/* Auto-fill with first Attack List target if available */
	if (s_attack_target_count > 0) {
		strncpy(bssid_input, s_attack_targets[0].bssid, sizeof(bssid_input)-1);
		bssid_input[sizeof(bssid_input)-1] = '\0';
		channel = s_attack_targets[0].channel;
	}

	u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);

	while (1)
	{
		m1_u8g2_firstpage();

		if (mode == MODE_INPUT)
		{
			m1_draw_header_bar(&m1_u8g2, "PMKID Capture", "Input");
			m1_draw_content_frame(&m1_u8g2, 2, 14, 124, 35);
			u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);

			char ch_line[24];
			snprintf(ch_line, sizeof(ch_line), "Ch:%u BSSID:", channel);
			m1_draw_text(&m1_u8g2, 8, 24, 114, ch_line, TEXT_ALIGN_LEFT);
			m1_draw_text(&m1_u8g2, 8, 36, 114,
				bssid_input[0] ? bssid_input : "(use scan)", TEXT_ALIGN_LEFT);

			m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Back", "Start", NULL);
		}
		else
		{
			m1_draw_header_bar(&m1_u8g2, "PMKID Capture", "Running");
			m1_draw_content_frame(&m1_u8g2, 2, 14, 124, 35);
			u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
			m1_draw_text(&m1_u8g2, 8, 30, 114, "Capturing...", TEXT_ALIGN_LEFT);
			m1_draw_text(&m1_u8g2, 8, 42, 114, "Wait up to 15s", TEXT_ALIGN_LEFT);
			m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Cancel", NULL, NULL);
		}
		m1_u8g2_nextpage();

		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
		if (ret != pdTRUE || q_item.q_evt_type != Q_EVENT_KEYPAD) continue;
		xQueueReceive(button_events_q_hdl, &this_button_status, 0);

		if (this_button_status.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) {
			xQueueReset(main_q_hdl); return;
		}
		else if (this_button_status.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK && mode == MODE_INPUT) {
			if (channel < 14) channel++; else channel = 1;
		}
		else if (this_button_status.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK && mode == MODE_INPUT) {
			if (channel > 1) channel--; else channel = 14;
		}
		else if (this_button_status.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK && mode == MODE_INPUT) {
if (bssid_input[0] == ' ') {
					/* BSSID empty - open keyboard */
					char bssid_prompt[32];
					snprintf(bssid_prompt, sizeof(bssid_prompt), "BSSID (AA:BB:CC:DD:EE:FF)");
					/* Pre-fill with default MAC so cursor works */
					if (bssid_input[0] == ' ') {
						strcpy(bssid_input, "00:00:00:00:00:00");
					}
					uint8_t len = m1_vkbs_get_data(bssid_prompt, bssid_input);
					if (len == 0) continue; /* User cancelled */
					/* Convert to uppercase */
					for (uint8_t i = 0; i < len && i < sizeof(bssid_input)-1; i++) {
						if (bssid_input[i] >= 'a' && bssid_input[i] <= 'f') {
							bssid_input[i] = bssid_input[i] - 'a' + 'A';
						}
					}
					/* Format as MAC address with colons if needed */
					if (len == 12) { /* AABBCCDDEEFF format */
						char formatted[18];
						snprintf(formatted, sizeof(formatted), "%c%c:%c%c:%c%c:%c%c:%c%c:%c%c",
							bssid_input[0], bssid_input[1], bssid_input[2], bssid_input[3],
							bssid_input[4], bssid_input[5], bssid_input[6], bssid_input[7],
							bssid_input[8], bssid_input[9], bssid_input[10], bssid_input[11]);
						strcpy(bssid_input, formatted);
					}
					continue;
			}
			mode = MODE_RUNNING;
			if (wifi_esp_pmkid_capture(bssid_input, channel) == SUCCESS)
				wifi_display_msg("PMKID", "Captured!");
			else
				wifi_display_msg("PMKID", "Not found");
			vTaskDelay(pdMS_TO_TICKS(2000));
			mode = MODE_INPUT;
		}
	}
}

/*============================================================================*/
/**
  * @brief Karma Attack
  */
/*============================================================================*/
void wifi_karma_attack(void)
{
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	bool active = false;
	uint8_t channel = 6;

	/* Ensure ESP32 is ready */
	if (!wifi_ensure_esp32_ready()) {
		wifi_display_msg("ESP32", "not ready!");
		vTaskDelay(pdMS_TO_TICKS(2000));
		return;
	}

	u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);

	while (1)
	{
		m1_u8g2_firstpage();
		m1_draw_header_bar(&m1_u8g2, "Karma", active ? "ACTIVE" : "Idle");
		m1_draw_content_frame(&m1_u8g2, 2, 14, 124, 35);
		u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);

		char ch_line[24];
		snprintf(ch_line, sizeof(ch_line), "Ch:%u", channel);
		m1_draw_text(&m1_u8g2, 8, 24, 114, ch_line, TEXT_ALIGN_LEFT);
		m1_draw_text(&m1_u8g2, 8, 36, 114,
			active ? "Responding..." : "OK to start", TEXT_ALIGN_LEFT);

		m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Back",
							active ? "Stop" : "Start", NULL);
		m1_u8g2_nextpage();

		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
		if (ret != pdTRUE || q_item.q_evt_type != Q_EVENT_KEYPAD) continue;
		xQueueReceive(button_events_q_hdl, &this_button_status, 0);

		if (this_button_status.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) {
			if (active) { wifi_esp_karma_stop(); active = false; }
			xQueueReset(main_q_hdl); return;
		}
		else if (this_button_status.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK && !active) {
			if (channel < 14) channel++; else channel = 1;
		}
		else if (this_button_status.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK && !active) {
			if (channel > 1) channel--; else channel = 14;
		}
		else if (this_button_status.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK) {
			if (!active) {
				if (wifi_esp_karma_start(channel) == SUCCESS)
					active = true;
			} else {
				wifi_esp_karma_stop(); active = false;
			}
		}
	}
}

/*============================================================================*/
/**
  * @brief Handshake Capture (deauth + EAPOL grab)
  */
/*============================================================================*/
void wifi_handshake_capture(void)
{
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	char bssid_input[18] = "";
	uint8_t channel = 6;
	int32_t deauth_count = 5;
	enum { MODE_INPUT, MODE_RUNNING } mode = MODE_INPUT;

	/* Ensure ESP32 is ready */
	if (!wifi_ensure_esp32_ready()) {
		wifi_display_msg("ESP32", "not ready!");
		vTaskDelay(pdMS_TO_TICKS(2000));
		return;
	}

	/* Auto-fill with first Attack List target if available */
	if (s_attack_target_count > 0) {
		strncpy(bssid_input, s_attack_targets[0].bssid, sizeof(bssid_input)-1);
		bssid_input[sizeof(bssid_input)-1] = '\0';
		channel = s_attack_targets[0].channel;
	}

	u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);

	while (1)
	{
		m1_u8g2_firstpage();

		if (mode == MODE_INPUT)
		{
			m1_draw_header_bar(&m1_u8g2, "HS Capture", "Setup");
			m1_draw_content_frame(&m1_u8g2, 2, 14, 124, 35);
			u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);

			char line[24];
			snprintf(line, sizeof(line), "Ch:%u Deauth:%ld", channel, (long)deauth_count);
			m1_draw_text(&m1_u8g2, 8, 24, 114, line, TEXT_ALIGN_LEFT);
			m1_draw_text(&m1_u8g2, 8, 36, 114,
				bssid_input[0] ? bssid_input : "BSSID: (scan)", TEXT_ALIGN_LEFT);

			m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Back", "Start", arrowright_8x8);
		}
		else
		{
			m1_draw_header_bar(&m1_u8g2, "HS Capture", "Capturing");
			m1_draw_content_frame(&m1_u8g2, 2, 14, 124, 35);
			u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
			m1_draw_text(&m1_u8g2, 8, 28, 114, "Deauthing &", TEXT_ALIGN_LEFT);
			m1_draw_text(&m1_u8g2, 8, 38, 114, "grabbing EAPOL...", TEXT_ALIGN_LEFT);
			m1_draw_text(&m1_u8g2, 8, 48, 114, "Wait up to 30s", TEXT_ALIGN_LEFT);
			m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Cancel", NULL, NULL);
		}
		m1_u8g2_nextpage();

		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
		if (ret != pdTRUE || q_item.q_evt_type != Q_EVENT_KEYPAD) continue;
		xQueueReceive(button_events_q_hdl, &this_button_status, 0);

		if (this_button_status.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) {
			xQueueReset(main_q_hdl); return;
		}
		else if (this_button_status.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK && mode == MODE_INPUT) {
			if (channel < 14) channel++; else channel = 1;
		}
		else if (this_button_status.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK && mode == MODE_INPUT) {
			if (channel > 1) channel--; else channel = 14;
		}
		else if (this_button_status.event[BUTTON_RIGHT_KP_ID] == BUTTON_EVENT_CLICK && mode == MODE_INPUT) {
			/* Adjust deauth count */
			deauth_count += 5;
			if (deauth_count > 50) deauth_count = 1;
		}
		else if (this_button_status.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK && mode == MODE_INPUT) {
if (bssid_input[0] == ' ') {
					/* BSSID empty - open keyboard */
					char bssid_prompt[32];
					snprintf(bssid_prompt, sizeof(bssid_prompt), "BSSID (AA:BB:CC:DD:EE:FF)");
					/* Pre-fill with default MAC so cursor works */
					if (bssid_input[0] == ' ') {
						strcpy(bssid_input, "00:00:00:00:00:00");
					}
					uint8_t len = m1_vkbs_get_data(bssid_prompt, bssid_input);
					if (len == 0) continue; /* User cancelled */
					/* Convert to uppercase */
					for (uint8_t i = 0; i < len && i < sizeof(bssid_input)-1; i++) {
						if (bssid_input[i] >= 'a' && bssid_input[i] <= 'f') {
							bssid_input[i] = bssid_input[i] - 'a' + 'A';
						}
					}
					/* Format as MAC address with colons if needed */
					if (len == 12) { /* AABBCCDDEEFF format */
						char formatted[18];
						snprintf(formatted, sizeof(formatted), "%c%c:%c%c:%c%c:%c%c:%c%c:%c%c",
							bssid_input[0], bssid_input[1], bssid_input[2], bssid_input[3],
							bssid_input[4], bssid_input[5], bssid_input[6], bssid_input[7],
							bssid_input[8], bssid_input[9], bssid_input[10], bssid_input[11]);
						strcpy(bssid_input, formatted);
					}
					continue;
			}
			mode = MODE_RUNNING;
			if (wifi_esp_hscap_start(bssid_input, channel, deauth_count) == SUCCESS)
				wifi_display_msg("HS Capture", "Got frames!");
			else
				wifi_display_msg("HS Capture", "Timeout");
			vTaskDelay(pdMS_TO_TICKS(2000));
			mode = MODE_INPUT;
		}
	}
}

/*============================================================================*/
/**
  * @brief Standalone Deauth Flood tool
  */
/*============================================================================*/
void wifi_deauth_flood(void)
{
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	bool active = false;
	char bssid_input[18] = "";
	uint8_t channel = 6;
	uint8_t target_idx = 0; /* Which Attack List target is selected */
	enum { MODE_INPUT, MODE_RUNNING } mode = MODE_INPUT;
	
	/* Ensure ESP32 is ready */
	if (!wifi_ensure_esp32_ready()) {
		wifi_display_msg("ESP32", "not ready!");
		vTaskDelay(pdMS_TO_TICKS(2000));
		return;
	}
	
	/* Auto-fill with first Attack List target if available */
	if (s_attack_target_count > 0) {
		target_idx = 0;
		strncpy(bssid_input, s_attack_targets[target_idx].bssid, sizeof(bssid_input)-1);
		bssid_input[sizeof(bssid_input)-1] = '\0';
		channel = s_attack_targets[target_idx].channel;
	}

	u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);

	while (1)
	{
		m1_u8g2_firstpage();

		if (mode == MODE_INPUT)
		{
			m1_draw_header_bar(&m1_u8g2, "Deauth Flood", "Setup");
			m1_draw_content_frame(&m1_u8g2, 2, 14, 124, 35);
			u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);

			char ch_line[24];
			snprintf(ch_line, sizeof(ch_line), "Ch:%u BSSID:", channel);
			m1_draw_text(&m1_u8g2, 8, 24, 114, ch_line, TEXT_ALIGN_LEFT);
			m1_draw_text(&m1_u8g2, 8, 36, 114,
				bssid_input[0] ? bssid_input : "(use scan)", TEXT_ALIGN_LEFT);

			m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Back", "Start", NULL);
		}
		else
		{
			m1_draw_header_bar(&m1_u8g2, "Deauth Flood", "ACTIVE");
			m1_draw_content_frame(&m1_u8g2, 2, 14, 124, 35);
			u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
			m1_draw_text(&m1_u8g2, 8, 28, 114, bssid_input, TEXT_ALIGN_LEFT);
			m1_draw_text(&m1_u8g2, 8, 40, 114, "Flooding...", TEXT_ALIGN_LEFT);
			m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Stop", NULL, NULL);
		}
		m1_u8g2_nextpage();

		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
		if (ret != pdTRUE || q_item.q_evt_type != Q_EVENT_KEYPAD) continue;
		xQueueReceive(button_events_q_hdl, &this_button_status, 0);

		if (this_button_status.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) {
			if (active) { wifi_esp_deauth_stop(); active = false; }
			xQueueReset(main_q_hdl); return;
		}
		else if (this_button_status.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK && mode == MODE_INPUT) {
			if (channel < 14) channel++; else channel = 1;
		}
		else if (this_button_status.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK && mode == MODE_INPUT) {
			if (channel > 1) channel--; else channel = 14;
		}
		else if (this_button_status.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK) {
			if (mode == MODE_INPUT) {
				if (bssid_input[0] == '\0') {
					/* BSSID empty - open keyboard */
					char bssid_prompt[32];
					snprintf(bssid_prompt, sizeof(bssid_prompt), "BSSID (AA:BB:CC:DD:EE:FF)");
					/* Pre-fill with default MAC so cursor works */
					if (bssid_input[0] == '\0') {
						strcpy(bssid_input, "00:00:00:00:00:00");
					}
					uint8_t len = m1_vkbs_get_data(bssid_prompt, bssid_input);
					if (len == 0) continue; /* User cancelled */
					/* Convert to uppercase */
					for (uint8_t i = 0; i < len && i < sizeof(bssid_input)-1; i++) {
						if (bssid_input[i] >= 'a' && bssid_input[i] <= 'f') {
							bssid_input[i] = bssid_input[i] - 'a' + 'A';
						}
					}
					/* Format as MAC address with colons if needed */
					if (len == 12) { /* AABBCCDDEEFF format */
						char formatted[18];
						snprintf(formatted, sizeof(formatted), "%c%c:%c%c:%c%c:%c%c:%c%c:%c%c",
							bssid_input[0], bssid_input[1], bssid_input[2], bssid_input[3],
							bssid_input[4], bssid_input[5], bssid_input[6], bssid_input[7],
							bssid_input[8], bssid_input[9], bssid_input[10], bssid_input[11]);
						strcpy(bssid_input, formatted);
					}
					continue;
				}
				wifi_esp_deauth_start(bssid_input, channel, NULL, 0);
				active = true;
				mode = MODE_RUNNING;
			} else {
				wifi_esp_deauth_stop(); active = false;
				mode = MODE_INPUT;
			}
		}
	}
}

#endif /* M1_APP_WIFI_OFFENSIVE_ENABLE */

#endif /* M1_APP_WIFI_CONNECT_ENABLE */
