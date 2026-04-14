/* See COPYING.txt for license details. */

/*
*
*  m1_menu.c
*
*  M1 menu handler
*
* M1 Project
*
*/
/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include <stdbool.h>
#include "stm32h5xx_hal.h"
#include "main.h"
//#include "mui.h"
//#include "u8x8.h"
//#include "U8g2lib.h"
#include "m1_gpio.h"
#include "m1_infrared.h"
#include "m1_nfc.h"
#include "m1_rfid.h"
#include "m1_settings.h"
#include "m1_sub_ghz.h"
#include "m1_power_ctl.h"
#include "m1_fw_update.h"
#include "m1_esp32_fw_update.h"
#include "m1_storage.h"
#include "m1_wifi.h"
#include "m1_bt.h"
#include "m1_802154.h"
#include "m1_esp32_hal.h"
#include "esp_app_main.h"
#include "m1_compile_cfg.h"

#ifdef M1_APP_FILE_IMPORT_ENABLE
#include "m1_flipper_integration.h"
#endif

#include "m1_branding.h"
#include "m1_main_menu.h"

/*************************** D E F I N E S ************************************/

//************************** C O N S T A N T **********************************/

/************************** S T R U C T U R E S *******************************/

/*----------------------------- > Sub-GHz ------------------------------------*/

S_M1_Menu_t menu_Sub_GHz_Record =
{
    "Read", sub_ghz_record, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Sub_GHz_Replay =
{
    "Replay", sub_ghz_replay, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Sub_GHz_Frequency_Reader =
{
    "Frequency Reader", sub_ghz_frequency_reader, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Sub_GHz_Regional_Information =
{
    "Regional Information", sub_ghz_regional_information, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Sub_GHz_Radio_Settings =
{
    "Radio Settings", sub_ghz_radio_settings, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Sub_GHz_Spectrum =
{
    "Spectrum Analyzer", sub_ghz_spectrum_analyzer, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Sub_GHz_Weather =
{
    "Weather Station", sub_ghz_weather_station, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Sub_GHz_BruteForce =
{
    "Brute Force", sub_ghz_brute_force, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Sub_GHz_RSSI =
{
    "RSSI Meter", sub_ghz_rssi_meter, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Sub_GHz_FreqScanner =
{
    "Freq Scanner", sub_ghz_freq_scanner, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Sub_GHz_JamDetector =
{
    "Jam Detect", sub_ghz_jam_detector, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Sub_GHz_JamLog =
{
    "Jam Log", sub_ghz_jam_log_viewer, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Sub_GHz_Read =
{
    "Read", sub_ghz_read, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Sub_GHz_Saved =
{
    "Saved", sub_ghz_saved, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Sub_GHz_AddManually =
{
    "Add Manually", sub_ghz_add_manually, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Sub_GHz =
{
    "Sub-GHz", NULL, NULL, NULL, 13, 0, menu_m1_icon_wave, NULL,
    {&menu_Sub_GHz_Record, &menu_Sub_GHz_Saved,
     &menu_Sub_GHz_AddManually,
     &menu_Sub_GHz_Frequency_Reader, &menu_Sub_GHz_Spectrum, &menu_Sub_GHz_RSSI,
     &menu_Sub_GHz_FreqScanner, &menu_Sub_GHz_JamDetector, &menu_Sub_GHz_JamLog, &menu_Sub_GHz_Weather,
     &menu_Sub_GHz_BruteForce, &menu_Sub_GHz_Regional_Information, &menu_Sub_GHz_Radio_Settings}
};

/*----------------------------- > 125KHz RFID --------------------------------*/

S_M1_Menu_t menu_125KHz_RFID_Read =
{
    "Read", rfid_125khz_read, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_125KHz_RFID_Saved =
{
    "Saved", rfid_125khz_saved, NULL, NULL, 0, 0, NULL, NULL, NULL
};


S_M1_Menu_t menu_125KHz_RFID_Add_Manually =
{
    "Add", rfid_125khz_add_manually, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_125KHz_RFID_Utilities =
{
    "Utilities", rfid_125khz_utilities, NULL, NULL, 0, 0, NULL, NULL, NULL
};

#ifdef M1_APP_FILE_IMPORT_ENABLE
S_M1_Menu_t menu_125KHz_RFID_Import =
{
    "Import .rfid", rfid_import_flipper, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_125KHz_RFID =
{
    "RFID", menu_125khz_rfid_init, menu_125khz_rfid_deinit, NULL, 5, 0, menu_m1_icon_rfid, NULL,
    {&menu_125KHz_RFID_Read, &menu_125KHz_RFID_Saved, &menu_125KHz_RFID_Add_Manually, &menu_125KHz_RFID_Import, &menu_125KHz_RFID_Utilities}
};
#else
S_M1_Menu_t menu_125KHz_RFID =
{
    "RFID", menu_125khz_rfid_init, menu_125khz_rfid_deinit, NULL, 4, 0, menu_m1_icon_rfid, NULL,
    {&menu_125KHz_RFID_Read, &menu_125KHz_RFID_Saved, &menu_125KHz_RFID_Add_Manually, &menu_125KHz_RFID_Utilities}
};
#endif

/*-------------------------------- > NFC -------------------------------------*/

S_M1_Menu_t menu_NFC_Read =
{
    "Read", nfc_read, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_NFC_Saved =
{
    "Saved", nfc_saved, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_NFC_Add_Manually =
{
    "Add Manually", nfc_add_manually, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_NFC_Extra_Actions =
{
    "Advanced", nfc_extra_actions, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_NFC_Tools =
{
    "Tools", nfc_tools, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_NFC_Detect_Reader =
{
    "MFKey Detect", nfc_detect_reader, NULL, NULL, 0, 0, NULL, NULL, NULL
};

#ifdef M1_APP_FILE_IMPORT_ENABLE
S_M1_Menu_t menu_NFC_Import =
{
    "Import .nfc", nfc_import_flipper, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_NFC =
{
    "NFC", &menu_nfc_init, menu_nfc_deinit, NULL, 7, 0, menu_m1_icon_nfc, NULL,
    {&menu_NFC_Read, &menu_NFC_Saved, &menu_NFC_Add_Manually, &menu_NFC_Import,
     &menu_NFC_Extra_Actions, &menu_NFC_Tools, &menu_NFC_Detect_Reader}
};
#else
S_M1_Menu_t menu_NFC =
{
    "NFC", &menu_nfc_init, menu_nfc_deinit, NULL, 6, 0, menu_m1_icon_nfc, NULL,
    {&menu_NFC_Read, &menu_NFC_Saved, &menu_NFC_Add_Manually,
     &menu_NFC_Extra_Actions, &menu_NFC_Tools, &menu_NFC_Detect_Reader}
};
#endif

/*----------------------------- > Infrared -----------------------------------*/

S_M1_Menu_t menu_Infrared_Universal_Remotes =
{
    "Universal Remotes", infrared_universal_remotes, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Infrared_Learn_New_Remote =
{
    "Learn", infrared_learn_new_remote, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Infrared_Saved_Remotes =
{
    "Replay", infrared_saved_remotes, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Infrared =
{
    "Infrared", menu_infrared_init, NULL, NULL, 3, 0, menu_m1_icon_infrared, NULL,
    {&menu_Infrared_Universal_Remotes, &menu_Infrared_Learn_New_Remote, &menu_Infrared_Saved_Remotes}
};

/*------------------------------- > GPIO -------------------------------------*/

S_M1_Menu_t menu_GPIO_GPIO_Manual_Control =
{
    "GPIO Control", gpio_manual_control, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_GPIO_3_3V_On_GPIO =
{
    "3.3V power", gpio_3_3v_on_gpio, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_GPIO_5V_On_GPIO =
{
    "5V power", gpio_5v_on_gpio, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_GPIO_USB_UART =
{
    "USB-UART bridge", gpio_usb_uart_bridge, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_GPIO =
{
    "GPIO", menu_gpio_init, menu_gpio_exit, gpio_xkey_handler, 4, 0, menu_m1_icon_gpio, gpio_gui_update,
    {&menu_GPIO_GPIO_Manual_Control, &menu_GPIO_3_3V_On_GPIO, &menu_GPIO_5V_On_GPIO, &menu_GPIO_USB_UART}
};

/*------------------------------- > Settings ---------------------------------*/

/*------------------------- > Settings-Storage -------------------------------*/

S_M1_Menu_t menu_Setting_Storage_About =
{
    "About SD Card", storage_about, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Setting_Storage_Explore =
{
    "Explore SD Card", storage_explore, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Setting_Storage_Mount =
{
    "Mount SD Card", storage_mount, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Setting_Storage_Unmount =
{
    "Unmount SD Card", storage_unmount, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Setting_Storage_Format =
{
    "Format SD Card", storage_format, NULL, NULL, 0, 0, NULL, NULL, NULL
};

/*------------------------- > Settings-Storage-End ---------------------------*/

/*------------------------- > Settings-Power ---------------------------------*/

S_M1_Menu_t menu_Setting_Power_Info =
{
    "Battery Info", power_battery_info, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Setting_Power_Reboot =
{
    "Reboot", power_reboot, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Setting_Power_Off =
{
    "Power Off", power_off, NULL, NULL, 0, 0, NULL, NULL, NULL
};

/*----------------------- > Settings-Power-End -------------------------------*/


/*---------------------- > Settings-Firmware Update --------------------------*/

S_M1_Menu_t menu_Setting_Firmware_Update_Image_File =
{
    "Image File", firmware_update_get_image_file, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Setting_Firmware_Update_Start =
{
    "Firmware update", firmware_update_start, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Setting_Firmware_Swap_Banks =
{
    "Swap Banks", firmware_swap_banks, NULL, NULL, 0, 0, NULL, NULL, NULL
};


/*---------------------- > Settings-Firmware Update-End ----------------------*/


/*---------------------- > Settings-ESP32 Update --------------------------*/

S_M1_Menu_t menu_Setting_ESP32_Image_File =
{
    "Image File", setting_esp32_image_file, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Setting_ESP32_Start_Address =
{
    "Start Address", setting_esp32_start_address, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Setting_ESP32_Firmware_Update =
{
    "Firmware Update", setting_esp32_firmware_update, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Setting_ESP32_C6_Reset =
{
    "ESP32-C6 Reset", setting_esp32_c6_reset, NULL, NULL, 0, 0, NULL, NULL, NULL
};

/*---------------------- > Settings-ESP32 Update-End ----------------------*/

S_M1_Menu_t menu_Settings_LCD_and_Notifications =
{
    "LCD and Notifications", settings_lcd_and_notifications, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Settings_System =
{
    "System", settings_system, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Setting_Firmware_Update =
{
    "Firmware update", firmware_update_init, firmware_update_exit, NULL, 3, 0, NULL, firmware_update_gui_update, {&menu_Setting_Firmware_Update_Image_File, &menu_Setting_Firmware_Update_Start, &menu_Setting_Firmware_Swap_Banks}
};

S_M1_Menu_t menu_Setting_ESP32 =
{
    "ESP32 update", setting_esp32_init, setting_esp32_exit, setting_esp32_xkey_handler, 4, 0, NULL, setting_esp32_gui_update, {&menu_Setting_ESP32_Image_File, &menu_Setting_ESP32_Start_Address, &menu_Setting_ESP32_Firmware_Update, &menu_Setting_ESP32_C6_Reset}
};

S_M1_Menu_t menu_Settings_About =
{
    "About", settings_about, NULL, NULL, 0, 0, NULL, NULL, NULL
};

/*------------------------------ > 802.15.4 ----------------------------------*/

S_M1_Menu_t menu_802154_Zigbee =
{
    "Zigbee Scan", zigbee_scan, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_802154_Thread =
{
    "Thread Scan", thread_scan, NULL, NULL, 0, 0, NULL, NULL, NULL
};

/*--------------------------------- > Wifi -----------------------------------*/

S_M1_Menu_t menu_Wifi_Scan_AP =
{
    "Scan 2.4G WiFi", wifi_scan_ap, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Wifi_Survey =
{
    "2.4G Survey", wifi_survey_24g, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Wifi_Health =
{
    "2.4G Health", wifi_health_24g, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Wifi_Config =
{
    "Saved Networks", wifi_config, NULL, NULL, 0, 0, NULL, NULL, NULL
};

#ifdef M1_APP_WIFI_CONNECT_ENABLE
S_M1_Menu_t menu_Wifi_Sync_RTC =
{
    "Sync RTC", wifi_sync_rtc_tool, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Wifi_Status =
{
    "Status", wifi_show_status, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Wifi_Disconnect =
{
    "Disconnect", wifi_disconnect, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Wifi =
{
    "WiFi 2.4G", menu_wifi_init, NULL, NULL, 9, 0, menu_m1_icon_wifi, NULL,
    {&menu_Wifi_Scan_AP, &menu_Wifi_Survey, &menu_Wifi_Health, &menu_802154_Zigbee,
     &menu_802154_Thread, &menu_Wifi_Config, &menu_Wifi_Sync_RTC,
     &menu_Wifi_Status, &menu_Wifi_Disconnect}
};
#else
S_M1_Menu_t menu_Wifi =
{
    "WiFi 2.4G", menu_wifi_init, NULL, NULL, 6, 0, menu_m1_icon_wifi, NULL,
    {&menu_Wifi_Scan_AP, &menu_Wifi_Survey, &menu_Wifi_Health,
     &menu_802154_Zigbee, &menu_802154_Thread, &menu_Wifi_Config}
};
#endif

/*--------------------------------- > BT ------------------------------------*/
S_M1_Menu_t menu_Bluetooth_Scan =
{
    "Scan", bluetooth_scan, NULL, NULL, 0, 0, NULL, NULL, NULL
};

#ifdef M1_APP_BT_MANAGE_ENABLE
S_M1_Menu_t menu_Bluetooth_Saved =
{
    "Saved", bluetooth_saved_devices, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Bluetooth_Advertise =
{
    "Advertise", bluetooth_advertise, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Bluetooth_Info =
{
    "BT Info", bluetooth_info, NULL, NULL, 0, 0, NULL, NULL, NULL
};

#ifdef M1_APP_BADBT_ENABLE
#include "m1_badbt.h"

S_M1_Menu_t menu_Bluetooth_BadBT =
{
    "Bad-BT", badbt_run, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Bluetooth_BTName =
{
    "BT Name", bluetooth_set_badbt_name, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Bluetooth =
{
    "Bluetooth", menu_bluetooth_init, NULL, NULL, 6, 0, menu_m1_icon_bluetooth, NULL,
    {&menu_Bluetooth_Scan, &menu_Bluetooth_Saved, &menu_Bluetooth_Advertise, &menu_Bluetooth_BadBT, &menu_Bluetooth_BTName, &menu_Bluetooth_Info}
};
#else
S_M1_Menu_t menu_Bluetooth =
{
    "Bluetooth", menu_bluetooth_init, NULL, NULL, 4, 0, menu_m1_icon_bluetooth, NULL,
    {&menu_Bluetooth_Scan, &menu_Bluetooth_Saved, &menu_Bluetooth_Advertise, &menu_Bluetooth_Info}
};
#endif /* M1_APP_BADBT_ENABLE */

#else
S_M1_Menu_t menu_Bluetooth_Config =
{
    "Config", bluetooth_config, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Bluetooth_Advertise =
{
    "Advertise", bluetooth_advertise, NULL, NULL, 0, 0, NULL, NULL, NULL
};

S_M1_Menu_t menu_Bluetooth =
{
    "Bluetooth", menu_bluetooth_init, NULL, NULL, 3, 0, menu_m1_icon_bluetooth, NULL,
    {&menu_Bluetooth_Config, &menu_Bluetooth_Scan, &menu_Bluetooth_Advertise}
};
#endif /* M1_APP_BT_MANAGE_ENABLE */

/*-------------------------------- > BadUSB ----------------------------------*/
#ifdef M1_APP_BADUSB_ENABLE
#include "m1_badusb.h"

S_M1_Menu_t menu_BadUSB =
{
    "BadUSB", badusb_main_menu, NULL, NULL, 0, 0, menu_m1_icon_badusb, NULL, NULL
};
#endif /* M1_APP_BADUSB_ENABLE */

/*-------------------------------- > Games -----------------------------------*/
#ifdef M1_APP_GAMES_ENABLE
#include "m1_games.h"

S_M1_Menu_t menu_Snake   = { "Snake",       game_snake_run,  NULL, NULL, 0, 0, NULL, NULL, {NULL} };
S_M1_Menu_t menu_Tetris  = { "Tetris",      game_tetris_run, NULL, NULL, 0, 0, NULL, NULL, {NULL} };
S_M1_Menu_t menu_TRex    = { "T-Rex Runner",game_trex_run,   NULL, NULL, 0, 0, NULL, NULL, {NULL} };
S_M1_Menu_t menu_Pong    = { "Pong",        game_pong_run,   NULL, NULL, 0, 0, NULL, NULL, {NULL} };
S_M1_Menu_t menu_Dice    = { "Dice Roll",   game_dice_run,   NULL, NULL, 0, 0, NULL, NULL, {NULL} };

S_M1_Menu_t menu_Games =
{
    "Games", NULL, NULL, NULL, 5, 0, menu_m1_icon_games, NULL,
    {&menu_Snake, &menu_Tetris, &menu_TRex, &menu_Pong, &menu_Dice}
};
#endif /* M1_APP_GAMES_ENABLE */

/*-------------------------------- > Apps ------------------------------------*/
#ifdef M1_APP_APPS_ENABLE
#include "m1_builtin_apps.h"
#include "m1_app_manager.h"

S_M1_Menu_t menu_DabTimer =
{
    "Dab Timer", app_dab_timer_run, NULL, NULL, 0, 0, NULL, NULL, {NULL}
};

S_M1_Menu_t menu_DvdLogo =
{
    "DVD Logo", app_dvd_logo_run, NULL, NULL, 0, 0, NULL, NULL, {NULL}
};

S_M1_Menu_t menu_SystemDashboard =
{
    "System Dashboard", app_system_dashboard_run, NULL, NULL, 0, 0, NULL, NULL, {NULL}
};

S_M1_Menu_t menu_FileTools =
{
    "File Tools", app_file_tools_run, NULL, NULL, 0, 0, NULL, NULL, {NULL}
};

S_M1_Menu_t menu_Clock =
{
    "Clock", app_clock_run, NULL, NULL, 0, 0, NULL, NULL, {NULL}
};

S_M1_Menu_t menu_HexViewer =
{
    "Hex Viewer", app_hex_viewer_run, NULL, NULL, 0, 0, NULL, NULL, {NULL}
};

S_M1_Menu_t menu_Apps_Browser =
{
    "Apps Browser", game_apps_browser_run, NULL, NULL, 0, 0, NULL, NULL, {NULL}
};

S_M1_Menu_t menu_RGBBacklight =
{
    "RGB Backlight", app_rgb_backlight_run, NULL, NULL, 0, 0, NULL, NULL, {NULL}
};

S_M1_Menu_t menu_Apps =
{
    "Apps", NULL, NULL, NULL, 8, 0, menu_m1_icon_apps, NULL,
    {&menu_DabTimer, &menu_DvdLogo, &menu_SystemDashboard, &menu_FileTools,
     &menu_Clock, &menu_HexViewer, &menu_RGBBacklight, &menu_Apps_Browser}
};
#endif /* M1_APP_APPS_ENABLE */

/***************************** V A R I A B L E S ******************************/

static S_M1_Menu_Control_t		menu_ctl;
static const S_M1_Menu_t 		*pthis_submenu;
TaskHandle_t					subfunc_handler_task_hdl;
TaskHandle_t					menu_main_handler_task_hdl;

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

static void menu_main_init(void);
void menu_main_handler_task(void *param);
void subfunc_handler_task(void *param);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/*============================================================================*/
/*
 * This function initializes the main menu.
*/
/*============================================================================*/
static void menu_main_init(void)
{
    menu_ctl.menu_level = 0; // main menu
    menu_ctl.menu_item_active = 0; // first menu item
    menu_ctl.last_selected_items[0] = 0; // last selected item is the current active item
    menu_ctl.main_menu_ptr[0] = &menu_Main;
    menu_ctl.num_menu_items = menu_ctl.main_menu_ptr[0]->num_submenu_items;

    assert(menu_ctl.num_menu_items >= 3);

	pthis_submenu = menu_ctl.main_menu_ptr[menu_ctl.menu_level];
    menu_ctl.this_func = pthis_submenu->sub_func;

    m1_gui_init();

} // static void menu_main_init(void)



/*============================================================================*/
/*
 * This function handles all tasks of the M1 main menu.
*/
/*============================================================================*/
void menu_main_handler_task(void *param)
{
	uint8_t key, sel_item, n_items;
	uint8_t menu_update_stat;
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	BaseType_t ret;

	settings_load_from_sd();  /* Load southpaw and other user settings (needs stack > sys_init) */

	if (m1_esp32_auto_init)
	{
		if (!m1_esp32_get_init_status())
			m1_esp32_init();
		if (!get_esp32_main_init_status())
			esp32_main_init();
	}

	menu_main_init();
	m1_device_stat.op_mode = M1_OPERATION_MODE_MENU_ON;
	m1_device_stat.active_timestamp = HAL_GetTick();
	m1_gui_menu_update(pthis_submenu, 0, MENU_UPDATE_INIT);
	while(1)
	{
		menu_update_stat = MENU_UPDATE_NONE;
		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
		if ( ret!=pdTRUE )
			continue;
		if ( q_item.q_evt_type!=Q_EVENT_KEYPAD )
			continue;
		ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
		if ( ret!=pdTRUE ) // This should never happen!
			continue; // Wait for a new notification when the attempt to read the button event fails

		m1_device_stat.active_timestamp = HAL_GetTick();

		if (m1_shutdown_prompt_take())
		{
			xQueueReset(main_q_hdl);
			xQueueReset(button_events_q_hdl);
			power_off_hold_prompt();

			if (m1_device_stat.op_mode == M1_OPERATION_MODE_DISPLAY_ON)
			{
				startup_info_screen_display(M1_PRODUCT_HOME_SUBTITLE);
			}
			else
			{
				m1_gui_menu_update(pthis_submenu, menu_ctl.menu_item_active, MENU_UPDATE_REFRESH);
			}
			continue;
		}

		if (this_button_status.event[BUTTON_BACK_KP_ID] != BUTTON_EVENT_IDLE &&
		    this_button_status.event[BUTTON_UP_KP_ID] != BUTTON_EVENT_IDLE)
		{
			m1_power_down();
		}

		for (key=0; key<NUM_BUTTONS_MAX; key++)
	    {
	    	if ( this_button_status.event[key]!=BUTTON_EVENT_IDLE )
	        {
	    		switch( key )
	    		{
	    			case BUTTON_OK_KP_ID:
	    				if ( this_button_status.event[BUTTON_OK_KP_ID]==BUTTON_EVENT_CLICK )
	    				{
	    					if ( m1_device_stat.op_mode==M1_OPERATION_MODE_MENU_ON )
	    					{
	                            n_items = pthis_submenu->submenu[menu_ctl.menu_item_active]->num_submenu_items;  // get the number of submenu items of the selected item
	                            menu_ctl.last_selected_items[menu_ctl.menu_level] = menu_ctl.menu_item_active; // save the selected item before going the next menu level
	                            if ( n_items != 0 ) // This menu item has another submenu?
	                            {
	                                menu_ctl.menu_level++; // go to next menu level
	                                menu_ctl.main_menu_ptr[menu_ctl.menu_level] = pthis_submenu->submenu[menu_ctl.menu_item_active];
	                                pthis_submenu = menu_ctl.main_menu_ptr[menu_ctl.menu_level];
	                                menu_ctl.this_func = pthis_submenu->sub_func;
	                            	menu_ctl.num_menu_items = n_items; // update this field
	                                menu_ctl.menu_item_active = 0; // default for new submenu
	                                sel_item = 0;
	                                if ( menu_ctl.this_func != NULL )
	                                {
	                                    menu_ctl.this_func(); // run the function of the selected submenu item to initialize it
	                                    // This function should complete quickly after initializing the display!!!
	                                } // if ( menu_ctl.this_func != NULL )
	                                menu_update_stat = MENU_UPDATE_RESET;
	                            } // if ( n_items != 0 )

	                            else if ( pthis_submenu->submenu[menu_ctl.menu_item_active]->sub_func != NULL ) // This menu item has no submenu. Does it have a function to run?
	                            {
	                                m1_device_stat.op_mode = M1_OPERATION_MODE_SUB_FUNC_RUNNING;
	                                m1_device_stat.sub_func = pthis_submenu->submenu[menu_ctl.menu_item_active]->sub_func; // let schedule to run the function of the selected submenu item
	                                //this_button_status.event[key].event = BUTTON_EVENT_IDLE; // clear before return
	                                // Notify the sub-function handler
	                                xTaskNotify(subfunc_handler_task_hdl, 0, eNoAction);
	                                // Wait for the sub-function to complete and notify this task from subfunc_handler_task
	                                xTaskNotifyWait(0, 0, NULL, portMAX_DELAY);
	                        		m1_device_stat.op_mode = M1_OPERATION_MODE_MENU_ON;
	                                // Return from sub-function. Let update GUI.
	                                sel_item = menu_ctl.menu_item_active;
	                                menu_update_stat = MENU_UPDATE_REFRESH; // The sub-function may have changed the GUI. It needs update.
	                            } // else if ( menu_ctl.this_function != NULL )

	                            else // This case should never happen. It doesn't exist!
	                            {
	                            	assert(("num_menu_items=0, this_func=NULL", FALSE));
	                            }
	                            key = NUM_BUTTONS_MAX; // Exit condition to stop checking other buttons!
	    					} // if ( m1_device_stat.op_mode==M1_OPERATION_MODE_MENU_ON )
	    					else if ( m1_device_stat.op_mode==M1_OPERATION_MODE_DISPLAY_ON )
	    					{
	    						menu_main_init();
	    						sel_item = 0;
	    						menu_update_stat = MENU_UPDATE_INIT;
	    						m1_device_stat.op_mode = M1_OPERATION_MODE_MENU_ON;
	    					} // else if ( m1_device_stat.op_mode==M1_OPERATION_MODE_DISPLAY_ON )
	    				} // if ( this_button_status.event[BUTTON_OK_KP_ID]==BUTTON_EVENT_CLICK )
	                    break;

	    			case BUTTON_UP_KP_ID:
						if ( m1_device_stat.op_mode!=M1_OPERATION_MODE_MENU_ON )
							break;
						if ( this_button_status.event[BUTTON_DOWN_KP_ID]==BUTTON_EVENT_CLICK ) // UP and DOWN pressed at the same time?
							break; // Do nothing
						sel_item = menu_ctl.menu_item_active;
						if ( sel_item==0 )
							sel_item = menu_ctl.num_menu_items - 1;
						else
							sel_item--;
						menu_ctl.menu_item_active = sel_item;
						menu_update_stat = MENU_UPDATE_MOVE_UP;
	    				break;

	    			case BUTTON_DOWN_KP_ID:
						if ( m1_device_stat.op_mode!=M1_OPERATION_MODE_MENU_ON )
							break;
						if ( this_button_status.event[BUTTON_UP_KP_ID]==BUTTON_EVENT_CLICK ) // UP and DOWN pressed at the same time?
							break; // Do nothing
						sel_item = menu_ctl.menu_item_active;
						if ( sel_item==(menu_ctl.num_menu_items - 1) )
							sel_item = 0;
						else
							sel_item++;
						menu_ctl.menu_item_active = sel_item;
						menu_update_stat = MENU_UPDATE_MOVE_DOWN;
	    				break;

	    			case BUTTON_LEFT_KP_ID:
						if ( m1_device_stat.op_mode!=M1_OPERATION_MODE_MENU_ON )
							break;
						if ( pthis_submenu->xkey_handler )
							pthis_submenu->xkey_handler(this_button_status.event[BUTTON_LEFT_KP_ID], BUTTON_LEFT_KP_ID, sel_item);
	    				break;

	    			case BUTTON_RIGHT_KP_ID:
						if ( m1_device_stat.op_mode!=M1_OPERATION_MODE_MENU_ON )
							break;
						if ( pthis_submenu->xkey_handler )
							pthis_submenu->xkey_handler(this_button_status.event[BUTTON_RIGHT_KP_ID], BUTTON_RIGHT_KP_ID, sel_item);
	    				break;

	    			case BUTTON_BACK_KP_ID:
						if ( m1_device_stat.op_mode!=M1_OPERATION_MODE_MENU_ON )
							break;
						if ( menu_update_stat!=MENU_UPDATE_NONE ) // Other buttons pressed?
							break; // Do nothing, let other buttons take their higher priority!
						if ( menu_ctl.menu_level==0 ) // already at main menu screen?
						{
							startup_info_screen_display(M1_PRODUCT_HOME_SUBTITLE);
							// op_mode is now DISPLAY_ON; pressing OK will re-init menu
						}
						else
						{
							if ( menu_ctl.num_menu_items ) // Submenu with active items?
							{
								if ( pthis_submenu->deinit_func )
									pthis_submenu->deinit_func();
							}
							menu_ctl.menu_level--;
							menu_ctl.menu_item_active = menu_ctl.last_selected_items[menu_ctl.menu_level];
							pthis_submenu = menu_ctl.main_menu_ptr[menu_ctl.menu_level];
							menu_ctl.this_func = pthis_submenu->sub_func;
							menu_ctl.num_menu_items = pthis_submenu->num_submenu_items;
							n_items = menu_ctl.num_menu_items;
							sel_item = menu_ctl.menu_item_active;
							if ( menu_ctl.this_func != NULL )
								menu_ctl.this_func();
							menu_update_stat = MENU_UPDATE_RESTORE;
						}
	    				break;

	    			default: // undefined buttons, or buttons do not exist.
	    				break;
	    		} // switch( key )

	        } // if ( this_button_status.event[key]!=BUTTON_EVENT_IDLE )
	    } // for (key=0; key<NUM_BUTTONS_MAX; key++)

	    if ( menu_update_stat!=MENU_UPDATE_NONE )
	    	m1_gui_menu_update(pthis_submenu, sel_item, menu_update_stat);
	} // while(1)

} // void menu_main_handler_task(void *param)



/*============================================================================*/
/*
 * This task handles the execution of any sub-function
*/
/*============================================================================*/
void subfunc_handler_task(void *param)
{
	while(1)
	{
		// Waiting for notification from menu_main_handler_task,
		// or from button_event_handler_task
		xTaskNotifyWait(0, 0, NULL, portMAX_DELAY);
		assert(m1_device_stat.sub_func!=NULL);
		// Run the sub-function
		m1_device_stat.sub_func();
		// Sub-function completes, let notify menu_main_handler_task
		xTaskNotify(menu_main_handler_task_hdl, 0, eNoAction);
	} // while(1)
} // void subfunc_handler_task(void *param)
