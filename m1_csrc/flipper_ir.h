/* See COPYING.txt for license details. */

/*
 * flipper_ir.h
 *
 * Flipper Zero .ir file format parser for IR signals
 *
 * M1 Project
 */

#ifndef FLIPPER_IR_H_
#define FLIPPER_IR_H_

#include "flipper_file.h"
#include "irmp.h"

#define FLIPPER_IR_RAW_MAX_SAMPLES  512
#define FLIPPER_IR_NAME_MAX_LEN     32

typedef enum {
	FLIPPER_IR_SIGNAL_PARSED = 0,
	FLIPPER_IR_SIGNAL_RAW
} flipper_ir_signal_type_t;

typedef struct {
	char name[FLIPPER_IR_NAME_MAX_LEN];
	flipper_ir_signal_type_t type;
	bool valid;
	union {
		struct {
			uint8_t  protocol;     /* IRMP protocol ID */
			uint16_t address;
			uint16_t command;
			uint8_t  flags;
		} parsed;
		struct {
			uint32_t frequency;    /* Hz, e.g. 38000 */
			float    duty_cycle;   /* e.g. 0.33 */
			int32_t  samples[FLIPPER_IR_RAW_MAX_SAMPLES];
			uint16_t sample_count;
		} raw;
	};
} flipper_ir_signal_t;

/* Open a .ir file and validate header */
bool flipper_ir_open(flipper_file_t *ctx, const char *path);

/* Read next signal from an open .ir file. Returns false at EOF */
bool flipper_ir_read_signal(flipper_file_t *ctx, flipper_ir_signal_t *out);

/* Write a .ir file header */
bool flipper_ir_write_header(flipper_file_t *ctx);

/* Open an existing .ir file to append signals (no header written; the file must
 * already contain a valid header). Ready for flipper_ir_write_signal(). */
bool flipper_ir_open_append(flipper_file_t *ctx, const char *path);

/* Write a signal to .ir file */
bool flipper_ir_write_signal(flipper_file_t *ctx, const flipper_ir_signal_t *sig);

/* Per-signal callback for flipper_ir_rewrite(). Called once per signal in file
 * order. The signal may be mutated in place (e.g. renamed). Return true to keep
 * it (as-is or mutated), false to drop it. */
typedef bool (*flipper_ir_rewrite_cb_t)(uint16_t index, flipper_ir_signal_t *sig, void *user);

/* Rewrite a .ir file by streaming every signal through cb into a temp file,
 * then atomically replacing the original. Stack buffers only, one signal in
 * flight. Used for button rename/delete/edit. Returns true on success. */
bool flipper_ir_rewrite(const char *path, flipper_ir_rewrite_cb_t cb, void *user);

/* Raw signal accumulator (for the raw learn fallback when IRMP can't decode).
 * begin() resets sig to an empty named raw signal; add_edge() appends one
 * mark (stored +us) or space (stored -us), returning false when the sample
 * buffer is full; finish() records frequency/duty_cycle and marks valid. */
void flipper_ir_raw_begin(flipper_ir_signal_t *sig, const char *name);
bool flipper_ir_raw_add_edge(flipper_ir_signal_t *sig, uint32_t duration_us, bool is_mark);
bool flipper_ir_raw_finish(flipper_ir_signal_t *sig, uint32_t frequency, float duty_cycle);

/* Outcome of feeding one RX edge event into a raw capture in progress. */
typedef enum {
	FLIPPER_IR_RAW_EDGE_ACCUMULATED = 0, /* edge appended, frame continuing */
	FLIPPER_IR_RAW_EDGE_DROPPED,         /* sample buffer full, edge ignored */
	FLIPPER_IR_RAW_FRAME_COMPLETE,       /* timeout marker, signal finalized (valid) */
	FLIPPER_IR_RAW_FRAME_NOISE           /* timeout marker, too few edges -> reset */
} flipper_ir_raw_feed_result_t;

/* Feed one received edge into a raw capture (the raw learn fallback used when
 * IRMP cannot decode a signal). Ordinary edges are accumulated via add_edge
 * (is_mark: true for a mark / carrier-on interval, false for a space). When
 * frame_end is true (the inter-frame timeout marker), duration_us/is_mark are
 * ignored and the frame is closed: if at least min_samples edges were captured
 * the signal is finished (frequency/duty_cycle applied, marked valid) and
 * FRAME_COMPLETE is returned; otherwise the accumulator is reset — keeping the
 * name — as noise and FRAME_NOISE is returned. */
flipper_ir_raw_feed_result_t flipper_ir_raw_feed(flipper_ir_signal_t *sig,
                                                 uint32_t duration_us, bool is_mark,
                                                 bool frame_end, uint16_t min_samples,
                                                 uint32_t frequency, float duty_cycle);

/* Rename the signal at target_index in a .ir file, preserving every other
 * signal's name/type/data and the file order. An out-of-range index is a no-op
 * (still returns true). Built on flipper_ir_rewrite (stack-only, atomic). */
bool flipper_ir_rename_signal(const char *path, uint16_t target_index, const char *new_name);

/* Delete the signal at target_index from a .ir file, preserving every other
 * signal and the file order. Deleting the last signal leaves a valid
 * header-only file. Out-of-range index is a no-op (still returns true). */
bool flipper_ir_delete_signal(const char *path, uint16_t target_index);

/* Map Flipper protocol name string to IRMP protocol ID */
uint8_t flipper_ir_proto_to_irmp(const char *name);

/* Map IRMP protocol ID to Flipper protocol name string */
const char *flipper_ir_irmp_to_proto(uint8_t irmp_id);

/* Count signals in a .ir file without loading them all */
uint16_t flipper_ir_count_signals(const char *path);

#endif /* FLIPPER_IR_H_ */
