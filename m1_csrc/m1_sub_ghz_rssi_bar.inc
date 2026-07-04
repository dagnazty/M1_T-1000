/* See COPYING.txt for license details. */

/*
 * m1_sub_ghz_rssi_bar.inc
 *
 * Pure geometry helpers for the SubGHz live-RSSI level bar (SPEC: "SubGHz
 * Reading Feedback — Live RSSI Indicator").
 *
 * This is an #include snippet, NOT a translation unit: m1_sub_ghz.c includes it
 * inline so it adds no new object file and no CMake change, and the host test
 * (tools/host_test/test_subghz_rssi.c) includes the very same source so the math
 * has a single source of truth. It is header-free by contract — the includer
 * must have <stdint.h> visible first. Keep it free of firmware headers so it
 * stays host-verifiable.
 *
 * Dynamic range (see plan-subghz.md open question #2):
 *   empty  = SUBGHZ_RSSI_BAR_MIN_DBM  (noise floor, matches NOISE_FLOOR_RSSI_THRESHOLD)
 *   full   = SUBGHZ_RSSI_BAR_MAX_DBM
 *   mark   = SUBGHZ_RSSI_BAR_THRESHOLD_DBM (noise floor + SNR detection margin)
 */

#ifndef M1_SUB_GHZ_RSSI_BAR_INC
#define M1_SUB_GHZ_RSSI_BAR_INC

#define SUBGHZ_RSSI_BAR_MIN_DBM        (-110) /* empty bar: noise floor (dBm) */
#define SUBGHZ_RSSI_BAR_MAX_DBM         (-30) /* full bar (dBm) */
#define SUBGHZ_RSSI_BAR_THRESHOLD_DBM   (-90) /* detection mark: floor + 20 dB SNR */

/* Inner fill width (px) of the on-screen ACTIVE-record bar. Shared with the
 * host test so the rendered pixel columns are pinned in one place. */
#define SUBGHZ_RSSI_BAR_FILL_W           114

/*
 * Map an RSSI reading (dBm) to a fill width in [0, bar_w] pixels.
 * Saturating at both ends, monotonic in between, and overflow-safe for any
 * int16_t input (widened to int32_t for the multiply). static inline so an
 * unused instance in firmware raises no -Wunused-function warning.
 */
static inline uint8_t subghz_rssi_to_bar(int16_t dbm, uint8_t bar_w)
{
	if (dbm <= SUBGHZ_RSSI_BAR_MIN_DBM)
		return 0;
	if (dbm >= SUBGHZ_RSSI_BAR_MAX_DBM)
		return bar_w;

	const int32_t span = SUBGHZ_RSSI_BAR_MAX_DBM - SUBGHZ_RSSI_BAR_MIN_DBM;
	int32_t fill = ((int32_t)(dbm - SUBGHZ_RSSI_BAR_MIN_DBM) * (int32_t)bar_w) / span;

	if (fill < 0)
		fill = 0;
	if (fill > (int32_t)bar_w)
		fill = bar_w;
	return (uint8_t)fill;
}

/*
 * Pixel column (0..bar_w) of the static detection-threshold mark within a bar
 * of width bar_w. Derived from the same mapping so the mark always lines up
 * with where a threshold-strength signal would fill to.
 */
static inline uint8_t subghz_rssi_threshold_px(uint8_t bar_w)
{
	return subghz_rssi_to_bar(SUBGHZ_RSSI_BAR_THRESHOLD_DBM, bar_w);
}

#endif /* M1_SUB_GHZ_RSSI_BAR_INC */
