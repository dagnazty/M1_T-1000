/* See COPYING.txt for license details. */

/*
 * test_flipper_ir.c  (HOST TEST)
 *
 * Round-trip tests for the Flipper .ir file layer (Task 1: append + rewrite).
 * Compiles the real m1_csrc/flipper_file.c + flipper_ir.c against the host FatFs
 * shim and verifies:
 *   1. A parsed and a raw signal appended to an existing header-only file
 *      re-parse with matching name/protocol/address/command/sample_count.
 *   2. flipper_ir_rewrite() can rename one signal, drop another, and preserve
 *      the rest — the foundation for the button rename/delete edit tasks.
 *
 * M1 Project — host test harness
 */

#include "flipper_ir.h"
#include "flipper_file.h"

#include <stdio.h>
#include <string.h>

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

#define CHECK_EQ_STR(actual, expected, msg)                                    \
	do {                                                                       \
		g_checks++;                                                            \
		if (strcmp((actual), (expected)) != 0) {                              \
			g_failures++;                                                      \
			printf("  FAIL: %s  (got \"%s\", want \"%s\")  (%s:%d)\n",         \
			       (msg), (actual), (expected), __FILE__, __LINE__);           \
		}                                                                      \
	} while (0)

/*************************** F I X T U R E S *********************************/

#define TEST_PATH   "m1_flipper_roundtrip.ir"

/* Known raw sample stream (alternating mark/space microseconds). */
static const int32_t k_raw_samples[] = {
	9024, -4512, 579, -552, 579, -552, 579, -1665
};
#define K_RAW_COUNT ((uint16_t)(sizeof(k_raw_samples) / sizeof(k_raw_samples[0])))

static void make_parsed(flipper_ir_signal_t *s, const char *name,
                        uint8_t proto, uint16_t addr, uint16_t cmd)
{
	memset(s, 0, sizeof(*s));
	strncpy(s->name, name, FLIPPER_IR_NAME_MAX_LEN - 1);
	s->type = FLIPPER_IR_SIGNAL_PARSED;
	s->valid = true;
	s->parsed.protocol = proto;
	s->parsed.address = addr;
	s->parsed.command = cmd;
}

static void make_raw(flipper_ir_signal_t *s, const char *name)
{
	memset(s, 0, sizeof(*s));
	strncpy(s->name, name, FLIPPER_IR_NAME_MAX_LEN - 1);
	s->type = FLIPPER_IR_SIGNAL_RAW;
	s->valid = true;
	s->raw.frequency = 38000;
	s->raw.duty_cycle = 0.33f;
	s->raw.sample_count = K_RAW_COUNT;
	memcpy(s->raw.samples, k_raw_samples, sizeof(k_raw_samples));
}

/* Read every signal in the file into caller storage; returns the count. */
static uint16_t read_all(const char *path, flipper_ir_signal_t *out, uint16_t max)
{
	flipper_file_t ff;
	uint16_t n = 0;

	if (!flipper_ir_open(&ff, path))
		return 0;

	while (n < max && flipper_ir_read_signal(&ff, &out[n]))
		n++;

	ff_close(&ff);
	return n;
}

/*************************** T E S T S ***************************************/

/* Task 1a: append a parsed + a raw signal to a header-only file, re-parse. */
static void test_append_roundtrip(void)
{
	flipper_file_t ff;
	flipper_ir_signal_t sig;
	flipper_ir_signal_t got[8];
	uint16_t n;

	printf("test_append_roundtrip\n");

	f_unlink(TEST_PATH);

	/* Seed: header-only file, then one parsed signal via the normal write path. */
	CHECK(ff_open_write(&ff, TEST_PATH), "seed: open_write");
	CHECK(flipper_ir_write_header(&ff), "seed: write header");
	make_parsed(&sig, "Power", IRMP_NEC_PROTOCOL, 0x0007, 0x0002);
	CHECK(flipper_ir_write_signal(&ff, &sig), "seed: write Power");
	ff_close(&ff);

	/* Append a second parsed signal WITHOUT rewriting the header. */
	CHECK(flipper_ir_open_append(&ff, TEST_PATH), "append: open parsed");
	make_parsed(&sig, "Vol_Up", IRMP_SAMSUNG32_PROTOCOL, 0x0707, 0x00E0);
	CHECK(flipper_ir_write_signal(&ff, &sig), "append: write Vol_Up");
	ff_close(&ff);

	/* Append a raw signal (IRMP-undecodable fallback shape). */
	CHECK(flipper_ir_open_append(&ff, TEST_PATH), "append: open raw");
	make_raw(&sig, "RawBtn");
	CHECK(flipper_ir_write_signal(&ff, &sig), "append: write RawBtn");
	ff_close(&ff);

	/* Re-parse and assert all three survived with correct fields. */
	n = read_all(TEST_PATH, got, 8);
	CHECK_EQ_INT(n, 3, "reparse: signal count");

	CHECK_EQ_STR(got[0].name, "Power", "sig0 name");
	CHECK_EQ_INT(got[0].type, FLIPPER_IR_SIGNAL_PARSED, "sig0 type");
	CHECK_EQ_INT(got[0].parsed.protocol, IRMP_NEC_PROTOCOL, "sig0 protocol");
	CHECK_EQ_INT(got[0].parsed.address, 0x0007, "sig0 address");
	CHECK_EQ_INT(got[0].parsed.command, 0x0002, "sig0 command");

	CHECK_EQ_STR(got[1].name, "Vol_Up", "sig1 name");
	CHECK_EQ_INT(got[1].type, FLIPPER_IR_SIGNAL_PARSED, "sig1 type");
	CHECK_EQ_INT(got[1].parsed.protocol, IRMP_SAMSUNG32_PROTOCOL, "sig1 protocol");
	CHECK_EQ_INT(got[1].parsed.address, 0x0707, "sig1 address");
	CHECK_EQ_INT(got[1].parsed.command, 0x00E0, "sig1 command");

	CHECK_EQ_STR(got[2].name, "RawBtn", "sig2 name");
	CHECK_EQ_INT(got[2].type, FLIPPER_IR_SIGNAL_RAW, "sig2 type");
	CHECK_EQ_INT(got[2].raw.frequency, 38000, "sig2 frequency");
	CHECK_EQ_INT(got[2].raw.sample_count, K_RAW_COUNT, "sig2 sample_count");
	CHECK(memcmp(got[2].raw.samples, k_raw_samples, sizeof(k_raw_samples)) == 0,
	      "sig2 samples match");

	f_unlink(TEST_PATH);
}

/* Rewrite callback: rename signal 0, delete signal 1, keep the rest. */
static bool edit_cb(uint16_t index, flipper_ir_signal_t *sig, void *user)
{
	(void)user;
	if (index == 0) {
		strncpy(sig->name, "PowerToggle", FLIPPER_IR_NAME_MAX_LEN - 1);
		sig->name[FLIPPER_IR_NAME_MAX_LEN - 1] = '\0';
		return true;   /* keep, renamed */
	}
	if (index == 1)
		return false;  /* drop Vol_Up */
	return true;       /* keep RawBtn */
}

/* Task 1b: rewrite a 3-signal file -> rename one, drop one, preserve the rest. */
static void test_rewrite_edit(void)
{
	flipper_file_t ff;
	flipper_ir_signal_t sig;
	flipper_ir_signal_t got[8];
	uint16_t n;

	printf("test_rewrite_edit\n");

	f_unlink(TEST_PATH);

	/* Build a 3-signal file: Power (parsed), Vol_Up (parsed), RawBtn (raw). */
	CHECK(ff_open_write(&ff, TEST_PATH), "seed: open_write");
	CHECK(flipper_ir_write_header(&ff), "seed: write header");
	make_parsed(&sig, "Power", IRMP_NEC_PROTOCOL, 0x0007, 0x0002);
	CHECK(flipper_ir_write_signal(&ff, &sig), "seed: write Power");
	make_parsed(&sig, "Vol_Up", IRMP_SAMSUNG32_PROTOCOL, 0x0707, 0x00E0);
	CHECK(flipper_ir_write_signal(&ff, &sig), "seed: write Vol_Up");
	make_raw(&sig, "RawBtn");
	CHECK(flipper_ir_write_signal(&ff, &sig), "seed: write RawBtn");
	ff_close(&ff);

	/* Rewrite: rename #0, drop #1, keep #2. Atomic temp-file replace. */
	CHECK(flipper_ir_rewrite(TEST_PATH, edit_cb, NULL), "rewrite returns true");

	n = read_all(TEST_PATH, got, 8);
	CHECK_EQ_INT(n, 2, "rewrite: signal count drops to 2");

	CHECK_EQ_STR(got[0].name, "PowerToggle", "rewrite: sig0 renamed");
	CHECK_EQ_INT(got[0].type, FLIPPER_IR_SIGNAL_PARSED, "rewrite: sig0 type");
	CHECK_EQ_INT(got[0].parsed.protocol, IRMP_NEC_PROTOCOL, "rewrite: sig0 protocol");
	CHECK_EQ_INT(got[0].parsed.address, 0x0007, "rewrite: sig0 address");
	CHECK_EQ_INT(got[0].parsed.command, 0x0002, "rewrite: sig0 command");

	CHECK_EQ_STR(got[1].name, "RawBtn", "rewrite: sig1 is RawBtn");
	CHECK_EQ_INT(got[1].type, FLIPPER_IR_SIGNAL_RAW, "rewrite: sig1 type");
	CHECK_EQ_INT(got[1].raw.sample_count, K_RAW_COUNT, "rewrite: sig1 sample_count");
	CHECK(memcmp(got[1].raw.samples, k_raw_samples, sizeof(k_raw_samples)) == 0,
	      "rewrite: sig1 samples preserved");

	/* The temp file must not be left behind. */
	CHECK_EQ_INT(flipper_ir_count_signals(TEST_PATH ".tmp"), 0, "temp file removed");

	f_unlink(TEST_PATH);
}

/* Task 3: a freshly created remote is a valid, empty, appendable .ir. This is
 * the file-level contract behind ir_custom_create_empty() — the UI/VKB/sanitize
 * layers are firmware-coupled and verified on-device. */
static void test_create_empty_remote(void)
{
	flipper_file_t ff;
	flipper_ir_signal_t sig;
	flipper_ir_signal_t got[4];
	uint16_t n;

	printf("test_create_empty_remote\n");

	f_unlink(TEST_PATH);

	/* Create an empty remote: header only, no signals. */
	CHECK(ff_open_write(&ff, TEST_PATH), "create: open_write");
	CHECK(flipper_ir_write_header(&ff), "create: write header");
	ff_close(&ff);

	/* A brand-new remote must parse as a valid .ir with zero buttons. */
	CHECK(flipper_ir_open(&ff, TEST_PATH), "create: reopens as valid .ir");
	ff_close(&ff);
	CHECK_EQ_INT(flipper_ir_count_signals(TEST_PATH), 0, "create: 0 signals");

	/* And it must be immediately appendable (the learn flow adds buttons). */
	CHECK(flipper_ir_open_append(&ff, TEST_PATH), "create: open_append");
	make_parsed(&sig, "Power", IRMP_NEC_PROTOCOL, 0x0007, 0x0002);
	CHECK(flipper_ir_write_signal(&ff, &sig), "create: append first button");
	ff_close(&ff);

	CHECK_EQ_INT(flipper_ir_count_signals(TEST_PATH), 1, "create: 1 signal after append");
	n = read_all(TEST_PATH, got, 4);
	CHECK_EQ_INT(n, 1, "create: reparse count");
	CHECK_EQ_STR(got[0].name, "Power", "create: appended button name");

	f_unlink(TEST_PATH);
}

/* Task 7: raw edge accumulator builds a signed mark/space sample stream that
 * round-trips through the writer/reader. This is the math behind the raw learn
 * fallback (the RX-event -> edge conversion is firmware, verified on-device). */
static void test_raw_accumulate(void)
{
	flipper_ir_signal_t sig;
	flipper_ir_signal_t got[2];
	flipper_file_t ff;
	uint16_t n, i;

	static const uint32_t durs[]   = { 9000, 4500, 560, 560, 560, 1690 };
	static const bool     marks[]  = { true, false, true, false, true, false };
	static const int32_t  expect[] = { 9000, -4500, 560, -560, 560, -1690 };
	const uint16_t ecount = 6;

	printf("test_raw_accumulate\n");
	f_unlink(TEST_PATH);

	flipper_ir_raw_begin(&sig, "RawLearned");
	for (i = 0; i < ecount; i++)
		CHECK(flipper_ir_raw_add_edge(&sig, durs[i], marks[i]), "raw: add edge");
	CHECK(flipper_ir_raw_finish(&sig, 38000, 0.33f), "raw: finish valid");

	CHECK_EQ_INT(sig.type, FLIPPER_IR_SIGNAL_RAW, "raw: type");
	CHECK_EQ_STR(sig.name, "RawLearned", "raw: name");
	CHECK_EQ_INT(sig.raw.frequency, 38000, "raw: frequency");
	CHECK_EQ_INT(sig.raw.sample_count, ecount, "raw: sample_count");
	CHECK(memcmp(sig.raw.samples, expect, sizeof(expect)) == 0, "raw: signed samples");

	/* Round-trip through the writer/reader. */
	CHECK(ff_open_write(&ff, TEST_PATH), "raw: open_write");
	CHECK(flipper_ir_write_header(&ff), "raw: header");
	CHECK(flipper_ir_write_signal(&ff, &sig), "raw: write signal");
	ff_close(&ff);

	n = read_all(TEST_PATH, got, 2);
	CHECK_EQ_INT(n, 1, "raw: reparse count");
	CHECK_EQ_INT(got[0].raw.sample_count, ecount, "raw: reparse sample_count");
	CHECK(memcmp(got[0].raw.samples, expect, sizeof(expect)) == 0, "raw: reparse samples");

	/* Capacity guard: edges beyond FLIPPER_IR_RAW_MAX_SAMPLES are rejected. */
	flipper_ir_raw_begin(&sig, "Full");
	for (i = 0; i < FLIPPER_IR_RAW_MAX_SAMPLES; i++)
		flipper_ir_raw_add_edge(&sig, 100, (i % 2) == 0);
	CHECK(!flipper_ir_raw_add_edge(&sig, 100, true), "raw: rejects past capacity");
	CHECK_EQ_INT(sig.raw.sample_count, FLIPPER_IR_RAW_MAX_SAMPLES, "raw: capped count");

	f_unlink(TEST_PATH);
}

/* Task 6: raw learn fallback frame-boundary logic. flipper_ir_raw_feed()
 * accumulates mark/space edges and, on the inter-frame timeout marker, either
 * finalizes the signal (>= min_samples) or discards it as noise. This is the
 * IRMP-UNKNOWN capture path minus the on-device RX event source (which supplies
 * the edge durations and the timeout marker). */
static void test_raw_feed_frame(void)
{
	flipper_ir_signal_t sig;
	uint16_t i;

	/* Alternating mark/space: a rising edge terminates a mark (stored +us),
	 * a falling edge terminates a space (stored -us). */
	static const uint32_t durs[] = { 9000, 4500, 560, 560, 560, 1690, 560 };
	static const bool     mark[] = { true, false, true, false, true, false, true };
	static const int32_t  exp[]  = { 9000, -4500, 560, -560, 560, -1690, 560 };
	const uint16_t ecount = 7;

	printf("test_raw_feed_frame\n");

	/* Too few edges before the timeout marker -> discarded as noise. */
	flipper_ir_raw_begin(&sig, "Btn");
	for (i = 0; i < 3; i++)
		CHECK_EQ_INT(flipper_ir_raw_feed(&sig, durs[i], mark[i], false, 8, 38000, 0.33f),
		             FLIPPER_IR_RAW_EDGE_ACCUMULATED, "feed: accumulate");
	CHECK_EQ_INT(flipper_ir_raw_feed(&sig, 0, false, true, 8, 38000, 0.33f),
	             FLIPPER_IR_RAW_FRAME_NOISE, "feed: noise on short frame");
	CHECK_EQ_INT(sig.raw.sample_count, 0, "feed: noise resets count");
	CHECK_EQ_STR(sig.name, "Btn", "feed: noise keeps name");
	CHECK(!sig.valid, "feed: noise not valid");

	/* Enough edges, then the timeout marker finalizes the signal. */
	flipper_ir_raw_begin(&sig, "Power");
	for (i = 0; i < ecount; i++)
		CHECK_EQ_INT(flipper_ir_raw_feed(&sig, durs[i], mark[i], false, 6, 38000, 0.33f),
		             FLIPPER_IR_RAW_EDGE_ACCUMULATED, "feed: accumulate valid");
	CHECK_EQ_INT(flipper_ir_raw_feed(&sig, 15501, true, true, 6, 38000, 0.33f),
	             FLIPPER_IR_RAW_FRAME_COMPLETE, "feed: complete on timeout");
	CHECK(sig.valid, "feed: complete valid");
	CHECK_EQ_INT(sig.raw.sample_count, ecount, "feed: complete count");
	CHECK_EQ_INT(sig.raw.frequency, 38000, "feed: complete frequency");
	CHECK(memcmp(sig.raw.samples, exp, sizeof(exp)) == 0, "feed: signed samples");

	/* Edges past the buffer are dropped without corrupting the count. */
	flipper_ir_raw_begin(&sig, "Full");
	for (i = 0; i < FLIPPER_IR_RAW_MAX_SAMPLES; i++)
		flipper_ir_raw_feed(&sig, 100, (i % 2) == 0, false, 6, 38000, 0.33f);
	CHECK_EQ_INT(flipper_ir_raw_feed(&sig, 100, true, false, 6, 38000, 0.33f),
	             FLIPPER_IR_RAW_EDGE_DROPPED, "feed: drop past capacity");
	CHECK_EQ_INT(sig.raw.sample_count, FLIPPER_IR_RAW_MAX_SAMPLES, "feed: capped");
}

/* Task 8: rename the button at a target index, preserving every other signal
 * (name/type/data/order). Reusable wrapper over flipper_ir_rewrite used by the
 * custom-remote button editor. */
static void test_rename_signal(void)
{
	flipper_file_t ff;
	flipper_ir_signal_t sig;
	flipper_ir_signal_t got[8];
	uint16_t n;

	printf("test_rename_signal\n");
	f_unlink(TEST_PATH);

	/* Seed: Power (parsed), Vol_Up (parsed), RawBtn (raw). */
	CHECK(ff_open_write(&ff, TEST_PATH), "rename seed: open_write");
	CHECK(flipper_ir_write_header(&ff), "rename seed: header");
	make_parsed(&sig, "Power", IRMP_NEC_PROTOCOL, 0x0007, 0x0002);
	CHECK(flipper_ir_write_signal(&ff, &sig), "rename seed: Power");
	make_parsed(&sig, "Vol_Up", IRMP_SAMSUNG32_PROTOCOL, 0x0707, 0x00E0);
	CHECK(flipper_ir_write_signal(&ff, &sig), "rename seed: Vol_Up");
	make_raw(&sig, "RawBtn");
	CHECK(flipper_ir_write_signal(&ff, &sig), "rename seed: RawBtn");
	ff_close(&ff);

	/* Rename the middle button; all others must be untouched. */
	CHECK(flipper_ir_rename_signal(TEST_PATH, 1, "Volume+"), "rename returns true");

	n = read_all(TEST_PATH, got, 8);
	CHECK_EQ_INT(n, 3, "rename: count unchanged");
	CHECK_EQ_STR(got[0].name, "Power", "rename: sig0 name intact");
	CHECK_EQ_STR(got[1].name, "Volume+", "rename: sig1 renamed");
	CHECK_EQ_INT(got[1].parsed.protocol, IRMP_SAMSUNG32_PROTOCOL, "rename: sig1 proto intact");
	CHECK_EQ_INT(got[1].parsed.address, 0x0707, "rename: sig1 addr intact");
	CHECK_EQ_INT(got[1].parsed.command, 0x00E0, "rename: sig1 cmd intact");
	CHECK_EQ_STR(got[2].name, "RawBtn", "rename: sig2 name intact");
	CHECK_EQ_INT(got[2].raw.sample_count, K_RAW_COUNT, "rename: sig2 samples intact");

	/* Out-of-range index is a no-op that still succeeds (file unchanged). */
	CHECK(flipper_ir_rename_signal(TEST_PATH, 9, "Nope"), "rename: OOB still true");
	n = read_all(TEST_PATH, got, 8);
	CHECK_EQ_STR(got[1].name, "Volume+", "rename: OOB left file unchanged");

	f_unlink(TEST_PATH);
}

/* Task 9: delete the button at a target index, preserving every other signal
 * and the file order; deleting the last remaining button leaves a valid,
 * header-only .ir. Reusable wrapper over flipper_ir_rewrite. */
static void test_delete_signal(void)
{
	flipper_file_t ff;
	flipper_ir_signal_t sig;
	flipper_ir_signal_t got[8];
	uint16_t n;

	printf("test_delete_signal\n");
	f_unlink(TEST_PATH);

	/* Seed: Power (parsed), Vol_Up (parsed), RawBtn (raw). */
	CHECK(ff_open_write(&ff, TEST_PATH), "delete seed: open_write");
	CHECK(flipper_ir_write_header(&ff), "delete seed: header");
	make_parsed(&sig, "Power", IRMP_NEC_PROTOCOL, 0x0007, 0x0002);
	CHECK(flipper_ir_write_signal(&ff, &sig), "delete seed: Power");
	make_parsed(&sig, "Vol_Up", IRMP_SAMSUNG32_PROTOCOL, 0x0707, 0x00E0);
	CHECK(flipper_ir_write_signal(&ff, &sig), "delete seed: Vol_Up");
	make_raw(&sig, "RawBtn");
	CHECK(flipper_ir_write_signal(&ff, &sig), "delete seed: RawBtn");
	ff_close(&ff);

	/* Drop the middle button; count falls by 1, neighbours intact + reordered. */
	CHECK(flipper_ir_delete_signal(TEST_PATH, 1), "delete returns true");
	n = read_all(TEST_PATH, got, 8);
	CHECK_EQ_INT(n, 2, "delete: count drops to 2");
	CHECK_EQ_STR(got[0].name, "Power", "delete: sig0 preserved");
	CHECK_EQ_INT(got[0].parsed.command, 0x0002, "delete: sig0 cmd intact");
	CHECK_EQ_STR(got[1].name, "RawBtn", "delete: sig1 is now RawBtn");
	CHECK_EQ_INT(got[1].raw.sample_count, K_RAW_COUNT, "delete: raw samples intact");

	/* Out-of-range index is a no-op (still true, nothing removed). */
	CHECK(flipper_ir_delete_signal(TEST_PATH, 9), "delete: OOB still true");
	CHECK_EQ_INT(flipper_ir_count_signals(TEST_PATH), 2, "delete: OOB count unchanged");

	/* Delete down to zero: the file stays a valid, appendable header-only .ir. */
	CHECK(flipper_ir_delete_signal(TEST_PATH, 0), "delete: remove first of two");
	CHECK(flipper_ir_delete_signal(TEST_PATH, 0), "delete: remove last button");
	CHECK_EQ_INT(flipper_ir_count_signals(TEST_PATH), 0, "delete: empty remote");
	CHECK(flipper_ir_open(&ff, TEST_PATH), "delete: empty file still valid");
	ff_close(&ff);
	CHECK(flipper_ir_open_append(&ff, TEST_PATH), "delete: empty file appendable");
	make_parsed(&sig, "Re", IRMP_NEC_PROTOCOL, 0x0001, 0x0002);
	CHECK(flipper_ir_write_signal(&ff, &sig), "delete: append after empty");
	ff_close(&ff);
	CHECK_EQ_INT(flipper_ir_count_signals(TEST_PATH), 1, "delete: re-append works");

	f_unlink(TEST_PATH);
}

int main(void)
{
	printf("== Flipper .ir host round-trip tests ==\n");

	test_append_roundtrip();
	test_rewrite_edit();
	test_create_empty_remote();
	test_raw_accumulate();
	test_raw_feed_frame();
	test_rename_signal();
	test_delete_signal();

	printf("== %d checks, %d failures ==\n", g_checks, g_failures);
	return (g_failures == 0) ? 0 : 1;
}
