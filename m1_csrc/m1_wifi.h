/* See COPYING.txt for license details. */

/*
*
* m1_wifi.h
*
* Library for M1 Wifi
*
* M1 Project
*
*/


#ifndef M1_WIFI_H_
#define M1_WIFI_H_

#include "m1_compile_cfg.h"

void menu_wifi_init(void);
void menu_wifi_exit(void);
#ifdef M1_APP_WIFI_OFFENSIVE_ENABLE
void menu_wifi_offensive_init(void);
#endif

void wifi_scan_ap(void);
void wifi_survey_24g(void);
void wifi_health_24g(void);
void wifi_config(void);
uint8_t wifi_is_connected(void);
int wifi_get_rssi(void);

#ifdef M1_APP_WIFI_CONNECT_ENABLE
void wifi_saved_networks(void);
void wifi_show_status(void);
void wifi_show_mode(void);
void wifi_show_stats(void);
void wifi_disconnect(void);
uint8_t wifi_sync_rtc(void);
void wifi_sync_rtc_tool(void);
#endif

/* Offensive WiFi tools */
void wifi_attack_list(void);
void wifi_beacon_spam(void);
void wifi_probe_sniff(void);
void wifi_pmkid_capture(void);
void wifi_karma_attack(void);
void wifi_handshake_capture(void);
void wifi_deauth_flood(void);

#endif /* M1_WIFI_H_ */
