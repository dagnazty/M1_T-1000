/* See COPYING.txt for license details. */

/*
*
* m1_kb_layout.h
*
* Shared ASCII -> HID keyboard layout for BadUSB and Bad-BT.
*
* Defaults to the built-in US layout. A Flipper-compatible ".kl" layout file
* on the SD card can replace it, so DuckyScript payloads type correctly on
* non-US keyboards. The layout is a single global setting used by both the
* USB and Bluetooth scripting engines.
*
* .kl file format: 128 x uint16 little-endian, indexed by ASCII code.
*   low byte  = HID scancode
*   high byte = HID modifier bitmask (0x02 = LeftShift, 0x40 = RightAlt/AltGr)
* A zero entry means the character is not typeable on that layout.
*
* M1 Project
*
*/

#ifndef M1_KB_LAYOUT_H_
#define M1_KB_LAYOUT_H_

#include <stdint.h>
#include <stdbool.h>

#define KB_LAYOUT_DIR             "0:/BadUSB/layouts"
#define KB_LAYOUT_EXT             ".kl"
#define KB_LAYOUT_ENTRIES         128U
#define KB_LAYOUT_FILE_SIZE       (KB_LAYOUT_ENTRIES * 2U)
#define KB_LAYOUT_NAME_MAX        20U
#define KB_LAYOUT_PATH_MAX        128U

/* HID modifier bitmask. usbd_hid.h defines the same values for the USB path;
 * these guards let the Bluetooth path (which does not include it) share them. */
#ifndef HID_MOD_LCTRL
#define HID_MOD_LCTRL             0x01U
#define HID_MOD_LSHIFT            0x02U
#define HID_MOD_LALT              0x04U
#define HID_MOD_LGUI              0x08U
#define HID_MOD_RCTRL             0x10U
#define HID_MOD_RSHIFT            0x20U
#define HID_MOD_RALT              0x40U
#define HID_MOD_RGUI              0x80U
#endif

/* HID Keyboard scancodes */
#define KEY_NONE                  0x00
#define KEY_A                     0x04
#define KEY_B                     0x05
#define KEY_C                     0x06
#define KEY_D                     0x07
#define KEY_E                     0x08
#define KEY_F                     0x09
#define KEY_G                     0x0A
#define KEY_H                     0x0B
#define KEY_I                     0x0C
#define KEY_J                     0x0D
#define KEY_K                     0x0E
#define KEY_L                     0x0F
#define KEY_M                     0x10
#define KEY_N                     0x11
#define KEY_O                     0x12
#define KEY_P                     0x13
#define KEY_Q                     0x14
#define KEY_R                     0x15
#define KEY_S                     0x16
#define KEY_T                     0x17
#define KEY_U                     0x18
#define KEY_V                     0x19
#define KEY_W                     0x1A
#define KEY_X                     0x1B
#define KEY_Y                     0x1C
#define KEY_Z                     0x1D
#define KEY_1                     0x1E
#define KEY_2                     0x1F
#define KEY_3                     0x20
#define KEY_4                     0x21
#define KEY_5                     0x22
#define KEY_6                     0x23
#define KEY_7                     0x24
#define KEY_8                     0x25
#define KEY_9                     0x26
#define KEY_0                     0x27
#define KEY_ENTER                 0x28
#define KEY_ESCAPE                0x29
#define KEY_BACKSPACE             0x2A
#define KEY_TAB                   0x2B
#define KEY_SPACE                 0x2C
#define KEY_MINUS                 0x2D
#define KEY_EQUAL                 0x2E
#define KEY_LEFTBRACE             0x2F
#define KEY_RIGHTBRACE            0x30
#define KEY_BACKSLASH             0x31
#define KEY_SEMICOLON             0x33
#define KEY_APOSTROPHE            0x34
#define KEY_GRAVE                 0x35
#define KEY_COMMA                 0x36
#define KEY_DOT                   0x37
#define KEY_SLASH                 0x38
#define KEY_CAPSLOCK              0x39
#define KEY_F1                    0x3A
#define KEY_F2                    0x3B
#define KEY_F3                    0x3C
#define KEY_F4                    0x3D
#define KEY_F5                    0x3E
#define KEY_F6                    0x3F
#define KEY_F7                    0x40
#define KEY_F8                    0x41
#define KEY_F9                    0x42
#define KEY_F10                   0x43
#define KEY_F11                   0x44
#define KEY_F12                   0x45
#define KEY_PRINTSCREEN           0x46
#define KEY_SCROLLLOCK            0x47
#define KEY_PAUSE                 0x48
#define KEY_INSERT                0x49
#define KEY_HOME                  0x4A
#define KEY_PAGEUP                0x4B
#define KEY_DELETE                0x4C
#define KEY_END                   0x4D
#define KEY_PAGEDOWN              0x4E
#define KEY_RIGHT                 0x4F
#define KEY_LEFT                  0x50
#define KEY_DOWN                  0x51
#define KEY_UP                    0x52
#define KEY_NUMLOCK               0x53
#define KEY_MENU                  0x65

/*
 * Look up the keycode and modifier for an ASCII character.
 * Returns false if the character has no mapping on the active layout, in
 * which case *keycode and *modifier are left untouched.
 */
bool kb_layout_lookup(char c, uint8_t *keycode, uint8_t *modifier);

/*
 * Load a .kl layout file from SD. On any failure the built-in US layout is
 * restored and false is returned.
 */
bool kb_layout_load(const char *path);

/* Revert to the built-in US layout. */
void kb_layout_reset(void);

/* Display name of the active layout, e.g. "US (built-in)" or "de_DE". */
const char *kb_layout_name(void);

/* Path of the loaded .kl file, or "" when the built-in layout is active.
 * Used by the settings file to persist the selection. */
const char *kb_layout_path(void);

#endif /* M1_KB_LAYOUT_H_ */
