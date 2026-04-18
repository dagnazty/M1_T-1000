/* See COPYING.txt for license details. */

/*
 *
 * m1_rgb_backlight.h
 *
 * SK6805 (NeoPixel) RGB backlight driver for M1
 *
 * Uses PD3 as data pin. Bit-banged at ~800kHz.
 *
 * M1 Project
 *
 */

#ifndef M1_RGB_BACKLIGHT_H_
#define M1_RGB_BACKLIGHT_H_

#include <stdint.h>

/* Number of SK6805 LEDs in the backlight strip — RGB mod uses 2 LEDs */
#define RGB_BL_LED_COUNT   2U

/* Color mode enumeration */
typedef enum
{
    RGB_MODE_WHITE = 0,
    RGB_MODE_RED,
    RGB_MODE_GREEN,
    RGB_MODE_BLUE,
    RGB_MODE_CYAN,
    RGB_MODE_MAGENTA,
    RGB_MODE_YELLOW,
    RGB_MODE_ORANGE,
    RGB_MODE_PURPLE,
    RGB_MODE_RAINBOW,
    RGB_MODE_CUSTOM,
    RGB_MODE_COUNT
} rgb_bl_mode_t;

/* Effect enumeration */
typedef enum
{
    RGB_EFFECT_STATIC = 0,
    RGB_EFFECT_BREATHE,
    RGB_EFFECT_CYCLE,
    RGB_EFFECT_COUNT
} rgb_bl_effect_t;

void rgb_bl_init(void);
void rgb_bl_deinit(void);
uint8_t rgb_bl_detect(void);  /* Returns 1 if RGB mod hardware detected (PD3 pulled up) */
uint8_t rgb_bl_is_available(void); /* Returns 1 if RGB mod was detected at boot */
void rgb_bl_set_mode(rgb_bl_mode_t mode);
void rgb_bl_set_effect(rgb_bl_effect_t effect);
void rgb_bl_set_brightness(uint8_t brightness);
void rgb_bl_set_custom(uint8_t r, uint8_t g, uint8_t b);
void rgb_bl_on(void);
void rgb_bl_off(void);
void rgb_bl_update(void);       /* Call periodically for effects (breathe/cycle) */

/* Accessors for menu */
rgb_bl_mode_t    rgb_bl_get_mode(void);
rgb_bl_effect_t  rgb_bl_get_effect(void);
uint8_t          rgb_bl_get_brightness(void);
uint8_t          rgb_bl_is_on(void);

const char *rgb_bl_mode_name(rgb_bl_mode_t mode);
const char *rgb_bl_effect_name(rgb_bl_effect_t effect);

/* Unified backlight API — routes to LP5814 or RGB mod automatically */
void m1_backlight_on(uint8_t brightness);
void m1_backlight_off(void);

#endif /* M1_RGB_BACKLIGHT_H_ */
