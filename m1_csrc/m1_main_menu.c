/* See COPYING.txt for license details. */

/*
*
* m1_main_menu.c
*
* Top-level menu layout definition
*
* Defines the main menu with Storage, Power, and System as separate
* top-level categories.  The menu navigation engine lives in m1_menu.c.
*
*/

/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include "m1_branding.h"
#include "m1_compile_cfg.h"
#include "m1_display.h"
#include "m1_menu.h"
#include "m1_system.h"

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

static void action_startup_splash(void);

extern S_M1_Menu_t menu_Sub_GHz;
extern S_M1_Menu_t menu_125KHz_RFID;
extern S_M1_Menu_t menu_NFC;
extern S_M1_Menu_t menu_Infrared;
extern S_M1_Menu_t menu_GPIO;
extern S_M1_Menu_t menu_Wifi;
extern S_M1_Menu_t menu_Bluetooth;
extern S_M1_Menu_t menu_Setting_Storage_About;
extern S_M1_Menu_t menu_Setting_Storage_Explore;
extern S_M1_Menu_t menu_Setting_Storage_Mount;
extern S_M1_Menu_t menu_Setting_Storage_Unmount;
extern S_M1_Menu_t menu_Setting_Storage_Format;
extern S_M1_Menu_t menu_Setting_Power_Info;
extern S_M1_Menu_t menu_Setting_Power_Reboot;
extern S_M1_Menu_t menu_Setting_Power_Off;
extern S_M1_Menu_t menu_Settings_LCD_and_Notifications;
extern S_M1_Menu_t menu_Settings_Backlight;
extern S_M1_Menu_t menu_Settings_System;
extern S_M1_Menu_t menu_Settings_About;
extern S_M1_Menu_t menu_Setting_Firmware_Update;
extern S_M1_Menu_t menu_Setting_ESP32;
extern void menu_setting_storage_init(void);
extern void menu_setting_power_init(void);

#ifdef M1_APP_BADUSB_ENABLE
extern S_M1_Menu_t menu_BadUSB;
#endif
#ifdef M1_APP_GAMES_ENABLE
extern S_M1_Menu_t menu_Games;
#endif
#ifdef M1_APP_APPS_ENABLE
extern S_M1_Menu_t menu_Apps;
#endif

/***************************** V A R I A B L E S ******************************/

static S_M1_Menu_t menu_Startup_Splash =
{
    "Startup Splash", action_startup_splash, NULL, NULL, 0, 0, NULL, NULL, {NULL}
};

static S_M1_Menu_t menu_System =
{
    "System", NULL, NULL, NULL, 7, 0, menu_m1_icon_setting, NULL,
    {&menu_Settings_Backlight, &menu_Settings_LCD_and_Notifications, &menu_Settings_System,
     &menu_Setting_Firmware_Update, &menu_Setting_ESP32,
     &menu_Settings_About, &menu_Startup_Splash}
};

static S_M1_Menu_t menu_Storage =
{
    "Storage", menu_setting_storage_init, NULL, NULL, 5, 0, menu_m1_icon_apps, NULL,
    {&menu_Setting_Storage_About, &menu_Setting_Storage_Explore, &menu_Setting_Storage_Mount,
     &menu_Setting_Storage_Unmount, &menu_Setting_Storage_Format}
};

static S_M1_Menu_t menu_Power =
{
    "Power", menu_setting_power_init, NULL, NULL, 3, 0, menu_m1_icon_setting, NULL,
    {&menu_Setting_Power_Info, &menu_Setting_Power_Reboot, &menu_Setting_Power_Off}
};

/*
 * Main menu — base 10 items + optional BadUSB, Games, Apps.
 * Optional items are inserted between Bluetooth and Storage.
 */

#ifdef M1_APP_BADUSB_ENABLE
#define OPT_BADUSB  1
#define ENT_BADUSB  &menu_BadUSB,
#else
#define OPT_BADUSB  0
#define ENT_BADUSB
#endif

#ifdef M1_APP_GAMES_ENABLE
#define OPT_GAMES   1
#define ENT_GAMES   &menu_Games,
#else
#define OPT_GAMES   0
#define ENT_GAMES
#endif

#ifdef M1_APP_APPS_ENABLE
#define OPT_APPS    1
#define ENT_APPS    &menu_Apps,
#else
#define OPT_APPS    0
#define ENT_APPS
#endif

#define MAIN_MENU_COUNT  (10 + OPT_BADUSB + OPT_GAMES + OPT_APPS)

S_M1_Menu_t menu_Main =
{
    "Main Menu", NULL, NULL, NULL, MAIN_MENU_COUNT, 0, NULL, NULL,
    {&menu_Sub_GHz, &menu_125KHz_RFID, &menu_NFC, &menu_Infrared, &menu_GPIO,
     &menu_Wifi, &menu_Bluetooth,
     ENT_BADUSB
     ENT_GAMES
     ENT_APPS
     &menu_Storage, &menu_Power, &menu_System}
};

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

static void action_startup_splash(void)
{
    startup_info_screen_display(M1_PRODUCT_HOME_SUBTITLE);
}
