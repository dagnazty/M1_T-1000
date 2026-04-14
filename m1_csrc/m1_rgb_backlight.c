/* See COPYING.txt for license details. */

/*
 *
 * m1_rgb_backlight.c
 *
 * SK6805 (NeoPixel) RGB backlight driver for M1
 *
 * Drives SK6805-1515 LEDs via PD3 bit-bang at ~800kHz.
 *
 * M1 Project
 *
 */

/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "stm32h5xx_hal.h"
#include "m1_rgb_backlight.h"
#include "m1_io_defs.h"
#include "m1_display.h"

/*************************** D E F I N E S ************************************/

/* SK6805 timing (approximate, in CPU cycles at 250MHz) */
#define SK6805_T1H_CYCLES    60    /* ~0.8us high for '1' bit */
#define SK6805_T1L_CYCLES    25    /* ~0.45us low for '1' bit */
#define SK6805_T0H_CYCLES    20    /* ~0.4us high for '0' bit */
#define SK6805_T0L_CYCLES    55    /* ~0.85us low for '0' bit */
#define SK6805_RESET_US      60    /* >50us low for reset */

/* RGB backlight data pin — change this to whichever pad is accessible.
 * PD3 is labeled "spare" in firmware but may not be routed on the PCB.
 * Other candidates: PE2, PE4, PE5, PE6 (GPIOE user pins).
 */
#define RGB_BL_PIN           GPIO_PIN_3
#define RGB_BL_PORT          GPIOD
#define RGB_BL_PIN_POS       3U

/************************** S T R U C T U R E S *******************************/

typedef struct
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
} rgb_color_t;

/**************************** V A R I A B L E S *******************************/

static rgb_color_t    g_rgb_leds[RGB_BL_LED_COUNT];
static rgb_bl_mode_t  g_rgb_mode  = RGB_MODE_WHITE;
static rgb_bl_effect_t g_rgb_effect = RGB_EFFECT_STATIC;
static uint8_t        g_rgb_brightness = 128;
static bool           g_rgb_on   = false;

/* Predefined colors per mode */
static const rgb_color_t rgb_mode_colors[RGB_MODE_COUNT] = {
    {255, 255, 255}, /* WHITE    */
    {255,   0,   0}, /* RED      */
    {  0, 255,   0}, /* GREEN    */
    {  0,   0, 255}, /* BLUE     */
    {  0, 255, 255}, /* CYAN     */
    {255,   0, 255}, /* MAGENTA  */
    {255, 255,   0}, /* YELLOW   */
    {255, 128,   0}, /* ORANGE   */
    {128,   0, 255}, /* PURPLE   */
    {  0,   0,   0}, /* RAINBOW  — handled specially */
    {  0,   0,   0}, /* CUSTOM   — set via rgb_bl_set_custom */
};

static const char *rgb_mode_names[RGB_MODE_COUNT] = {
    "White", "Red", "Green", "Blue", "Cyan",
    "Magenta", "Yellow", "Orange", "Purple",
    "Rainbow", "Custom"
};

static const char *rgb_effect_names[RGB_EFFECT_COUNT] = {
    "Static", "Breathe", "Color Cycle"
};

/* Breathe state */
static uint8_t  g_breathe_step = 0;
static int8_t   g_breathe_dir  = 1;

/* Color cycle state */
static uint8_t  g_cycle_hue = 0;

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

static void rgb_bl_write_leds(void);
static void rgb_bl_send_byte(uint8_t byte);
static void rgb_bl_send_reset(void);
static inline void rgb_bl_delay_cycles(uint32_t cycles);
static void rgb_bl_apply_mode_colors(void);
static uint8_t rgb_hue_to_r(uint8_t hue);
static uint8_t rgb_hue_to_g(uint8_t hue);
static uint8_t rgb_hue_to_b(uint8_t hue);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

static inline void rgb_bl_delay_cycles(uint32_t cycles)
{
    __asm volatile (
        "1: subs %[c], %[c], #3 \n"
        "   bhi 1b              \n"
        : [c] "+r" (cycles)
        :
    );
}

/*============================================================================*/
/**
  * @brief  Send one byte to SK6805 chain (MSB first)
  */
/*============================================================================*/
static void rgb_bl_send_byte(uint8_t byte)
{
    uint32_t pin_set   = 1U << RGB_BL_PIN_POS;
    uint32_t pin_clear = 1U << RGB_BL_PIN_POS;

    for (int8_t bit = 7; bit >= 0; bit--)
    {
        if (byte & (1U << bit))
        {
            /* '1' bit: long high, short low */
            RGB_BL_PORT->BSRR = pin_set;
            rgb_bl_delay_cycles(SK6805_T1H_CYCLES);
            RGB_BL_PORT->BSRR = pin_clear << 16U;
            rgb_bl_delay_cycles(SK6805_T1L_CYCLES);
        }
        else
        {
            /* '0' bit: short high, long low */
            RGB_BL_PORT->BSRR = pin_set;
            rgb_bl_delay_cycles(SK6805_T0H_CYCLES);
            RGB_BL_PORT->BSRR = pin_clear << 16U;
            rgb_bl_delay_cycles(SK6805_T0L_CYCLES);
        }
    }
}

/*============================================================================*/
/**
  * @brief  Send reset (long low) to latch data
  */
/*============================================================================*/
static void rgb_bl_send_reset(void)
{
    HAL_GPIO_WritePin(RGB_BL_PORT, RGB_BL_PIN, GPIO_PIN_RESET);
    /* ~60us delay — use HAL since timing isn't critical here */
    uint32_t start = DWT->CYCCNT;
    while ((DWT->CYCCNT - start) < (250U * SK6805_RESET_US)) {}
}

/*============================================================================*/
/**
  * @brief  Push current LED buffer to SK6805 chain
  */
/*============================================================================*/
static void rgb_bl_write_leds(void)
{
    __disable_irq();

    for (uint8_t i = 0; i < RGB_BL_LED_COUNT; i++)
    {
        /* SK6805 expects G-R-B order */
        uint8_t scale = g_rgb_brightness;
        uint8_t g = (uint16_t)g_rgb_leds[i].g * scale / 255U;
        uint8_t r = (uint16_t)g_rgb_leds[i].r * scale / 255U;
        uint8_t b = (uint16_t)g_rgb_leds[i].b * scale / 255U;

        rgb_bl_send_byte(g);
        rgb_bl_send_byte(r);
        rgb_bl_send_byte(b);
    }

    __enable_irq();

    rgb_bl_send_reset();
}

/*============================================================================*/
/**
  * @brief  HSV-like hue to RGB (simple 6-segment wheel, S=255, V=255)
  */
/*============================================================================*/
static uint8_t rgb_hue_to_r(uint8_t hue)
{
    uint8_t region = hue / 43U;
    uint8_t remainder = (hue - region * 43U) * 6U;
    switch (region)
    {
        case 0: return 255U;
        case 1: return 255U - remainder;
        case 2: return 0U;
        case 3: return 0U;
        case 4: return remainder;
        case 5: return 255U;
        default: return 0U;
    }
}

static uint8_t rgb_hue_to_g(uint8_t hue)
{
    uint8_t region = hue / 43U;
    uint8_t remainder = (hue - region * 43U) * 6U;
    switch (region)
    {
        case 0: return remainder;
        case 1: return 255U;
        case 2: return 255U;
        case 3: return 255U - remainder;
        case 4: return 0U;
        case 5: return 0U;
        default: return 0U;
    }
}

static uint8_t rgb_hue_to_b(uint8_t hue)
{
    uint8_t region = hue / 43U;
    uint8_t remainder = (hue - region * 43U) * 6U;
    switch (region)
    {
        case 0: return 0U;
        case 1: return 0U;
        case 2: return remainder;
        case 3: return 255U;
        case 4: return 255U;
        case 5: return 255U - remainder;
        default: return 0U;
    }
}

/*============================================================================*/
/**
  * @brief  Apply current mode colors to LED buffer
  */
/*============================================================================*/
static void rgb_bl_apply_mode_colors(void)
{
    if (g_rgb_mode == RGB_MODE_RAINBOW)
    {
        for (uint8_t i = 0; i < RGB_BL_LED_COUNT; i++)
        {
            uint8_t hue = (g_cycle_hue + i * (256U / RGB_BL_LED_COUNT)) & 0xFFU;
            g_rgb_leds[i].r = rgb_hue_to_r(hue);
            g_rgb_leds[i].g = rgb_hue_to_g(hue);
            g_rgb_leds[i].b = rgb_hue_to_b(hue);
        }
        return;
    }

    const rgb_color_t *c = &rgb_mode_colors[g_rgb_mode];
    for (uint8_t i = 0; i < RGB_BL_LED_COUNT; i++)
    {
        g_rgb_leds[i] = *c;
    }
}

/*============================================================================*/
/* Public API                                                                 */
/*============================================================================*/

void rgb_bl_init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* Enable GPIOD clock */
    __HAL_RCC_GPIOD_CLK_ENABLE();

    /* Configure PD3 as push-pull output */
    HAL_GPIO_WritePin(RGB_BL_PORT, RGB_BL_PIN, GPIO_PIN_RESET);
    GPIO_InitStruct.Pin   = RGB_BL_PIN;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(RGB_BL_PORT, &GPIO_InitStruct);

    /* Enable DWT for delay cycles */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    memset(g_rgb_leds, 0, sizeof(g_rgb_leds));
    g_rgb_on = true;
    rgb_bl_apply_mode_colors();
    rgb_bl_write_leds();
}


void rgb_bl_deinit(void)
{
    rgb_bl_off();
    /* Revert PD3 to analog (safe default) */
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin   = RGB_BL_PIN;
    GPIO_InitStruct.Mode  = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull  = GPIO_PULLDOWN;
    HAL_GPIO_Init(RGB_BL_PORT, &GPIO_InitStruct);
}


void rgb_bl_set_mode(rgb_bl_mode_t mode)
{
    if (mode < RGB_MODE_COUNT)
    {
        g_rgb_mode = mode;
        rgb_bl_apply_mode_colors();
        if (g_rgb_on) rgb_bl_write_leds();
    }
}


void rgb_bl_set_effect(rgb_bl_effect_t effect)
{
    if (effect < RGB_EFFECT_COUNT)
    {
        g_rgb_effect = effect;
        g_breathe_step = 0;
        g_breathe_dir = 1;
        g_cycle_hue = 0;
    }
}


void rgb_bl_set_brightness(uint8_t brightness)
{
    g_rgb_brightness = brightness;
    if (g_rgb_on) rgb_bl_write_leds();
}


void rgb_bl_set_custom(uint8_t r, uint8_t g, uint8_t b)
{
    g_rgb_mode = RGB_MODE_CUSTOM;
    for (uint8_t i = 0; i < RGB_BL_LED_COUNT; i++)
    {
        g_rgb_leds[i].r = r;
        g_rgb_leds[i].g = g;
        g_rgb_leds[i].b = b;
    }
    if (g_rgb_on) rgb_bl_write_leds();
}


void rgb_bl_on(void)
{
    g_rgb_on = true;
    rgb_bl_apply_mode_colors();
    rgb_bl_write_leds();
}


void rgb_bl_off(void)
{
    g_rgb_on = false;
    memset(g_rgb_leds, 0, sizeof(g_rgb_leds));
    rgb_bl_write_leds();
}


void rgb_bl_update(void)
{
    if (!g_rgb_on) return;

    switch (g_rgb_effect)
    {
        case RGB_EFFECT_BREATHE:
        {
            g_breathe_step += g_breathe_dir * 2U;
            if (g_breathe_step >= 250U) { g_breathe_dir = -1; g_breathe_step = 250U; }
            if (g_breathe_step <= 5U)    { g_breathe_dir =  1; g_breathe_step = 5U; }
            uint8_t saved = g_rgb_brightness;
            g_rgb_brightness = g_breathe_step;
            rgb_bl_write_leds();
            g_rgb_brightness = saved;
            break;
        }

        case RGB_EFFECT_CYCLE:
        {
            g_cycle_hue += 1U;
            rgb_bl_apply_mode_colors();
            rgb_bl_write_leds();
            break;
        }

        case RGB_EFFECT_STATIC:
        default:
            break;
    }
}


rgb_bl_mode_t rgb_bl_get_mode(void)        { return g_rgb_mode; }
rgb_bl_effect_t rgb_bl_get_effect(void)    { return g_rgb_effect; }
uint8_t rgb_bl_get_brightness(void)        { return g_rgb_brightness; }
uint8_t rgb_bl_is_on(void)                 { return g_rgb_on ? 1U : 0U; }

const char *rgb_bl_mode_name(rgb_bl_mode_t mode)
{
    return (mode < RGB_MODE_COUNT) ? rgb_mode_names[mode] : "?";
}

const char *rgb_bl_effect_name(rgb_bl_effect_t effect)
{
    return (effect < RGB_EFFECT_COUNT) ? rgb_effect_names[effect] : "?";
}
