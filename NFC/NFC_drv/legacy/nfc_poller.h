/* See COPYING.txt for license details. */

#ifndef NFC_POLLER_H_
#define NFC_POLLER_H_

#include <stdbool.h>
#include <stdint.h>
#include "rfal_nfc.h"

/**
 * @brief ReadIni - Initialize NFC poller (READ-ONLY mode)
 *
 * @retval true Initialization successful
 * @retval false Initialization failed
 */
bool ReadIni(void);

/**
 * @brief ReadCycle - Main NFC poller processing loop
 *
 * @retval None
 */
void ReadCycle(void);

typedef enum
{
    NFC_POLL_PROFILE_NORMAL = 0,
    NFC_POLL_PROFILE_FAST_A,
} nfc_poll_profile_t;

void nfc_poller_set_profile(nfc_poll_profile_t profile);
nfc_poll_profile_t nfc_poller_get_profile(void);

/* --- Extern wrappers for static poller helpers --- */

/** Check if SAK indicates a MIFARE Classic variant */
bool nfc_poller_is_classic_sak(uint8_t sak);

/** Read all sectors of a MIFARE Classic card using key dictionary */
void nfc_poller_read_mfc(const rfalNfcDevice *dev);

/** Read Type 2 Tag (NTAG/Ultralight) pages */
void nfc_poller_read_t2t(const rfalNfcDevice *dev);

/** Send PWD_AUTH to NTAG/Ultralight; on success pack[2] is filled */
ReturnCode nfc_poller_pwd_auth(const uint8_t pwd[4], uint8_t pack[2]);

/** Send GET_VERSION to NTAG; fills rxBuf with 8-byte version */
ReturnCode nfc_poller_get_version(uint8_t *rxBuf, uint16_t rxBufLen, uint16_t *rcvLen);

/**
 * @brief Test function: Enable continuous 13.56 MHz carrier (sine wave)
 * 
 * Enables pure carrier transmission for NFC range boosting.
 * Based on Flipper Zero's NFC implementation.
 * 
 * @param duration_ms Duration to transmit (0 = infinite)
 * @return true if successful
 */
bool test_nfc_continuous_carrier(uint32_t duration_ms);

/**
 * @brief MAX POWER continuous carrier transmission
 * 
 * Uses maximum modulation (82%%) and minimum driver resistance
 * for the STRONGEST possible sinewave output.
 * WARNING: May exceed regulatory limits!
 * 
 * @param duration_ms How long to transmit carrier
 * @return true if successful
 */
bool test_nfc_max_power_carrier(uint32_t duration_ms);

/**
 * @brief Stop continuous carrier transmission
 * 
 * @return true if successful
 */
bool test_nfc_stop_continuous_carrier(void);

/**
 * @brief Test boosted NFC read with carrier pre-activation
 * 
 * Demonstrates the "boosting" technique for improved range.
 * 
 * @param boost_duration_ms Carrier duration before reading
 * @return true if tag detected
 */
bool test_nfc_boosted_read(uint32_t boost_duration_ms);

/* Advanced NFC scanning with distance capture */
typedef struct {
    bool detected;
    uint8_t tag_type;
    uint8_t uid[10];
    uint8_t uid_len;
    int16_t rssi;
    uint32_t read_time_ms;
    uint32_t boost_duration_ms;
    float estimated_distance_cm;
    uint8_t scan_attempts;
} nfc_scan_result_t;

/** Pulsed carrier for efficient tag wake-up */
bool nfc_pulsed_carrier(uint32_t total_ms, uint32_t pulse_on_ms, uint32_t pulse_off_ms);

/** Get RSSI for distance estimation */
int16_t nfc_get_rssi(void);

/** Estimate distance from RSSI */
float nfc_estimate_distance_from_rssi(int16_t rssi);

/** Optimize receiver for weak signals */
void nfc_optimize_receiver(void);

/** Fast scan for nearby tags */
bool nfc_fast_scan(nfc_scan_result_t *result);

/** Long-range scan with carrier boosting */
bool nfc_long_range_scan(nfc_scan_result_t *result, uint32_t boost_ms);

/** Adaptive scan (tries multiple modes) */
bool nfc_adaptive_scan(nfc_scan_result_t *result);

/** Test different boost configurations */
void nfc_test_boost_configurations(void);

/** Test pulsed carrier patterns */
void nfc_test_pulsed_carrier(void);

/*============================================================================*/
/* Non-blocking Carrier Functions */
/*============================================================================*/

/**
 * @brief Start non-blocking carrier transmission
 * 
 * Uses FreeRTOS task to keep device responsive during long tests.
 * 
 * @param duration_ms Duration in milliseconds (1000-120000)
 * @return true if successful
 */
bool nfc_start_carrier_nonblocking(uint32_t duration_ms);

/**
 * @brief Start pulsed carrier pattern
 * 
 * @param on_ms ON duration per pulse
 * @param off_ms OFF duration per pulse
 * @param cycles Number of cycles
 * @return true if successful
 */
bool nfc_start_pulsed_carrier(uint32_t on_ms, uint32_t off_ms, uint32_t cycles);

/**
 * @brief Stop any active carrier transmission
 */
void nfc_stop_carrier(void);

/**
 * @brief Check if carrier is active
 * @return true if carrier is active
 */
bool nfc_is_carrier_active(void);

/**
 * @brief Get remaining time for current test
 * @return Remaining time in milliseconds, 0 if not active or pulsed mode
 */
uint32_t nfc_get_remaining_time(void);

#endif /* NFC_POLLER_H_ */
