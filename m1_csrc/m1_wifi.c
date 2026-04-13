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
void wifi_disconnect(void);
static uint16_t wifi_ap_list_get_index(void);
static bool wifi_do_connect(const char *ssid, const char *password);
#endif

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

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
	wifi_credential_t cred;
	char password[WIFI_CRED_PASS_MAX_LEN];
	bool do_connect;
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
				else if ( this_button_status.event[BUTTON_OK_KP_ID]==BUTTON_EVENT_CLICK )
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

					do_connect = false;
					password[0] = '\0';

					/* Check if we have saved credentials */
					if ( wifi_cred_find((const char *)list[s_current_ap_index].ssid, &cred) )
					{
						/* Use saved password */
						strncpy(password, cred.password, WIFI_CRED_PASS_MAX_LEN - 1);
						password[WIFI_CRED_PASS_MAX_LEN - 1] = '\0';
						do_connect = true;
					}
					else if ( list[s_current_ap_index].encryption_mode == 0 )
					{
						/* Open network - no password needed */
						do_connect = true;
					}
					else
					{
						/* Prompt for password using virtual keyboard */
						memset(password, 0, sizeof(password));
						uint8_t pw_len = m1_vkb_get_filename("Password:",
							"", password);
						if ( pw_len > 0 )
						{
							do_connect = true;
						}
						else
						{
							/* User cancelled - redraw AP list */
							u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
							wifi_ap_list_print(NULL, false); /* reset state */
							list_count = wifi_ap_list_print(&app_req, true);
						}
					}

					if ( do_connect )
					{
						bool ok = wifi_do_connect(
							(const char *)list[s_current_ap_index].ssid,
							password);

						if ( ok )
						{
							/* Offer to save credentials if not already saved */
							if ( !wifi_cred_find((const char *)list[s_current_ap_index].ssid, &cred)
								&& password[0] != '\0' )
							{
								wifi_cred_save(
									(const char *)list[s_current_ap_index].ssid,
									password);
								wifi_display_msg("Credentials", "saved!");
								vTaskDelay(pdMS_TO_TICKS(1500));
							}
						}

						/* Redraw the scan list */
						u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
						wifi_ap_list_print(NULL, false); /* reset state */
						list_count = wifi_ap_list_print(&app_req, true);
					}
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
	m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Back", "Connect", arrowright_8x8);
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
		m1_draw_text(&m1_u8g2, 8, y_offset, 114, "OK to connect", TEXT_ALIGN_LEFT);
		y_offset += 8;
		m1_draw_text(&m1_u8g2, 8, y_offset, 114, M1_WIFI_BAND_NOTE, TEXT_ALIGN_LEFT);
		y_offset += 8;
		m1_draw_text(&m1_u8g2, 8, y_offset, 114, "RIGHT to delete", TEXT_ALIGN_LEFT);

		m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Back", "Connect", arrowright_8x8);

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

	/* Configure SNTP: enable, timezone 0, server */
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


#else /* M1_APP_WIFI_CONNECT_ENABLE not defined */

/*============================================================================*/
/**
  * @brief WiFi config - stub when connect feature not enabled
  */
/*============================================================================*/
void wifi_config(void)
{
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	BaseType_t ret;

	m1_gui_let_update_fw();

	while (1 )
	{
		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
		if (ret==pdTRUE)
		{
			if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
			{
				ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
				if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK )
				{
					xQueueReset(main_q_hdl);
					break;
				}
			}
		}
	}
} // void wifi_config(void)

#endif /* M1_APP_WIFI_CONNECT_ENABLE */
