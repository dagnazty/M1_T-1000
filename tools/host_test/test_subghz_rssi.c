/* See COPYING.txt for license details. */

/*
 * test_subghz_rssi.c  (HOST TEST)
 *
 * Unit tests for the pure SubGHz live-RSSI-bar geometry helpers (Task 1).
 * The helpers live in m1_csrc/m1_sub_ghz_rssi_bar.inc as header-free static
 * functions; m1_sub_ghz.c #includes that same snippet, so there is a single
 * source of truth and no firmware translation unit / CMake change.
 *
 * The snippet is header-free by contract: it assumes <stdint.h> is already
 * visible, which we provide here before the #include.
 *
 * Verifies:
 *   - dbm <= min -> 0, dbm >= max -> bar_w (saturating endpoints)
 *   - clamped and overflow-safe for extreme inputs (-255, 0)
 *   - monotonic non-decreasing across the range
 *   - midpoint maps to the documented column
 *   - the noise-floor threshold mark maps to the documented pixel column
 *
 * M1 Project — host test harness
 */

#include <stdint.h>
#include <stdio.h>

#include "m1_sub_ghz_rssi_bar.inc"

/*************************** T E S T   H A R N E S S **************************/

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond, msg)                                                       \
	do {                                                                       \
		g_checks++;                                                            \
		if (!(cond)) {                                                         \
			g_failures++;                                                      \
			printf("  FAIL: %s  (%s:%d)\n", (msg), __FILE__, __LINE__);        \
		}                                                                      \
	} while (0)

#define CHECK_EQ_INT(actual, expected, msg)                                    \
	do {                                                                       \
		g_checks++;                                                            \
		long _a = (long)(actual);                                             \
		long _e = (long)(expected);                                           \
		if (_a != _e) {                                                        \
			g_failures++;                                                      \
			printf("  FAIL: %s  (got %ld, want %ld)  (%s:%d)\n",               \
			       (msg), _a, _e, __FILE__, __LINE__);                         \
		}                                                                      \
	} while (0)

/*************************** T E S T S ***************************************/

/* Endpoints saturate: floor -> 0, ceiling -> full width. */
static void test_endpoints(void)
{
	printf("test_endpoints\n");
	CHECK_EQ_INT(subghz_rssi_to_bar(SUBGHZ_RSSI_BAR_MIN_DBM, 120), 0,
	             "min dBm -> empty bar");
	CHECK_EQ_INT(subghz_rssi_to_bar(SUBGHZ_RSSI_BAR_MAX_DBM, 120), 120,
	             "max dBm -> full bar");
	/* One step past each endpoint still saturates, never wraps. */
	CHECK_EQ_INT(subghz_rssi_to_bar(SUBGHZ_RSSI_BAR_MIN_DBM - 1, 120), 0,
	             "below min -> empty bar");
	CHECK_EQ_INT(subghz_rssi_to_bar(SUBGHZ_RSSI_BAR_MAX_DBM + 1, 120), 120,
	             "above max -> full bar");
}

/* Extreme inputs never underflow/overflow the uint8_t result. */
static void test_clamp_extremes(void)
{
	printf("test_clamp_extremes\n");
	CHECK_EQ_INT(subghz_rssi_to_bar(-255, 120), 0, "-255 dBm clamps to 0");
	CHECK_EQ_INT(subghz_rssi_to_bar(0, 120), 120, "0 dBm clamps to full");
	CHECK_EQ_INT(subghz_rssi_to_bar(-255, 255), 0, "-255 dBm, wide bar -> 0");
	CHECK_EQ_INT(subghz_rssi_to_bar(0, 255), 255, "0 dBm, wide bar -> 255");
	/* Degenerate zero-width bar is always empty. */
	CHECK_EQ_INT(subghz_rssi_to_bar(-70, 0), 0, "zero-width bar -> 0");
}

/* Fill is non-decreasing as the signal gets stronger. */
static void test_monotonic(void)
{
	printf("test_monotonic\n");
	uint8_t prev = subghz_rssi_to_bar(SUBGHZ_RSSI_BAR_MIN_DBM - 5, 120);
	for (int16_t dbm = SUBGHZ_RSSI_BAR_MIN_DBM - 5;
	     dbm <= SUBGHZ_RSSI_BAR_MAX_DBM + 5; dbm++) {
		uint8_t cur = subghz_rssi_to_bar(dbm, 120);
		CHECK(cur >= prev, "bar width is monotonic non-decreasing");
		CHECK(cur <= 120, "bar width never exceeds bar_w");
		prev = cur;
	}
}

/* Documented interior columns: midpoint and threshold mark. */
static void test_documented_columns(void)
{
	printf("test_documented_columns\n");
	/* Midpoint of the -110..-30 range is -70 -> half of a 120px bar = 60. */
	CHECK_EQ_INT(subghz_rssi_to_bar(-70, 120), 60, "midpoint -70 dBm -> 60px");
	/* Threshold mark (floor + SNR = -90 dBm) is a quarter up a 120px bar. */
	CHECK_EQ_INT(subghz_rssi_threshold_px(120), 30, "threshold mark -> 30px");
	CHECK_EQ_INT(subghz_rssi_threshold_px(120),
	             subghz_rssi_to_bar(SUBGHZ_RSSI_BAR_THRESHOLD_DBM, 120),
	             "threshold px agrees with to_bar of threshold dBm");
	/* Threshold sits strictly above the floor and below the ceiling. */
	CHECK(subghz_rssi_threshold_px(120) > 0, "threshold mark above floor");
	CHECK(subghz_rssi_threshold_px(120) < 120, "threshold mark below ceiling");
}

/* The concrete on-screen bar width maps to fixed, documented pixel columns.
 * This pins the geometry the ACTIVE-screen widget renders (Task 2) so the
 * device draw code and this test share one source of truth for the fill width. */
static void test_onscreen_bar_geometry(void)
{
	printf("test_onscreen_bar_geometry\n");
	CHECK_EQ_INT(subghz_rssi_to_bar(SUBGHZ_RSSI_BAR_MIN_DBM, SUBGHZ_RSSI_BAR_FILL_W),
	             0, "floor -> empty on-screen bar");
	CHECK_EQ_INT(subghz_rssi_to_bar(SUBGHZ_RSSI_BAR_MAX_DBM, SUBGHZ_RSSI_BAR_FILL_W),
	             SUBGHZ_RSSI_BAR_FILL_W, "ceiling -> full on-screen bar");
	/* Midpoint -70 dBm -> half of the 114px inner bar. */
	CHECK_EQ_INT(subghz_rssi_to_bar(-70, SUBGHZ_RSSI_BAR_FILL_W), 57,
	             "midpoint -> 57px on-screen");
	/* Threshold mark (-90 dBm) sits a quarter of the way up the 114px bar. */
	CHECK_EQ_INT(subghz_rssi_threshold_px(SUBGHZ_RSSI_BAR_FILL_W), 28,
	             "threshold mark -> 28px on-screen");
	CHECK(subghz_rssi_threshold_px(SUBGHZ_RSSI_BAR_FILL_W) > 0 &&
	      subghz_rssi_threshold_px(SUBGHZ_RSSI_BAR_FILL_W) < SUBGHZ_RSSI_BAR_FILL_W,
	      "on-screen threshold mark strictly inside the bar");
}

int main(void)
{
	printf("== test_subghz_rssi ==\n");
	test_endpoints();
	test_clamp_extremes();
	test_monotonic();
	test_documented_columns();
	test_onscreen_bar_geometry();

	printf("\n%d checks, %d failures\n", g_checks, g_failures);
	if (g_failures) {
		printf("RESULT: FAIL\n");
		return 1;
	}
	printf("RESULT: PASS\n");
	return 0;
}
