/* See COPYING.txt for license details. */

/*
 * flipper_ir.c
 *
 * Flipper Zero .ir file format parser for IR signals
 *
 * M1 Project
 */

/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "flipper_ir.h"

/*************************** D E F I N E S ************************************/

#define FLIPPER_IR_FILETYPE     "IR signals file"
#define FLIPPER_IR_MIN_VERSION  1

//************************** S T R U C T U R E S *******************************

/**
 * @brief  Protocol mapping entry: Flipper name <-> IRMP protocol ID
 */
typedef struct {
	const char *flipper_name;
	uint8_t     irmp_id;
} ir_proto_map_t;

/***************************** V A R I A B L E S ******************************/

/**
 * Protocol mapping table between Flipper Zero protocol names and IRMP IDs.
 * The IRMP protocol IDs come from irmpprotocols.h.
 */
static const ir_proto_map_t ir_proto_table[] = {
	{ "NEC",        IRMP_NEC_PROTOCOL },         /* 2  */
	{ "NECext",     IRMP_NEC_PROTOCOL },          /* 2  (extended addressing mode) */
	{ "NEC42",      IRMP_NEC42_PROTOCOL },        /* 28 */
	{ "NEC16",      IRMP_NEC16_PROTOCOL },        /* 27 */
	{ "Samsung32",  IRMP_SAMSUNG32_PROTOCOL },    /* 10 */
	{ "RC5",        IRMP_RC5_PROTOCOL },          /* 7  */
	{ "RC5X",       IRMP_RC5_PROTOCOL },          /* 7  (RC5 extended, same decoder) */
	{ "RC6",        IRMP_RC6_PROTOCOL },          /* 9  */
	{ "SIRC",       IRMP_SIRCS_PROTOCOL },        /* 1  */
	{ "SIRC15",     IRMP_SIRCS_PROTOCOL },        /* 1  (15-bit mode) */
	{ "SIRC20",     IRMP_SIRCS_PROTOCOL },        /* 1  (20-bit mode) */
	{ "Kaseikyo",   IRMP_KASEIKYO_PROTOCOL },     /* 5  */
	{ "RCA",        IRMP_RCCAR_PROTOCOL },         /* 19 */
	{ "Pioneer",    IRMP_NEC_PROTOCOL },           /* Pioneer uses NEC encoding */
	{ "Denon",      IRMP_DENON_PROTOCOL },         /* 8  */
	{ "JVC",        IRMP_JVC_PROTOCOL },           /* 20 */
	{ "Sharp",      IRMP_DENON_PROTOCOL },         /* 8  (Sharp uses same as Denon) */
	{ "Panasonic",  IRMP_KASEIKYO_PROTOCOL },      /* 5  (Panasonic uses Kaseikyo) */
	{ "LG",         IRMP_LGAIR_PROTOCOL },         /* 40 */
	{ "Samsung",    IRMP_SAMSUNG32_PROTOCOL },     /* 10 (Flipper has no separate Samsung; all are Samsung32) */
	{ "Apple",      IRMP_APPLE_PROTOCOL },         /* 11 */
	{ "Nokia",      IRMP_NOKIA_PROTOCOL },         /* 16 */
	{ "Bose",       IRMP_BOSE_PROTOCOL },          /* 31 */
	{ "Samsung48",  IRMP_SAMSUNG48_PROTOCOL },    /* 41 */
	{ "RCMM",       IRMP_RCMM32_PROTOCOL },       /* 36 */
	{ NULL,         IRMP_UNKNOWN_PROTOCOL }
};

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

static int ff_strcasecmp(const char *a, const char *b);
static uint16_t ff_hex_bytes_to_uint16_le(const uint8_t *bytes, uint8_t count);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/*============================================================================*/
/**
 * @brief  Case-insensitive string comparison (portable)
 */
static int ff_strcasecmp(const char *a, const char *b)
{
	while (*a && *b)
	{
		int ca = tolower((unsigned char)*a);
		int cb = tolower((unsigned char)*b);
		if (ca != cb)
			return ca - cb;
		a++;
		b++;
	}
	return (unsigned char)*a - (unsigned char)*b;
}

/*============================================================================*/
/**
 * @brief  Convert Flipper hex bytes (little-endian) to uint16.
 *         Flipper format: "07 00 00 00" means value 0x0007
 * @param  bytes  parsed byte array
 * @param  count  number of bytes
 * @return uint16 value (first two bytes, little-endian)
 */
static uint16_t ff_hex_bytes_to_uint16_le(const uint8_t *bytes, uint8_t count)
{
	uint16_t val = 0;

	if (count >= 1)
		val = bytes[0];
	if (count >= 2)
		val |= (uint16_t)((uint16_t)bytes[1] << 8);

	return val;
}

/*============================================================================*/
/**
 * @brief  Map Flipper protocol name to IRMP protocol ID
 * @param  name  Flipper protocol name string (e.g., "NEC", "Samsung32")
 * @return IRMP protocol ID, or IRMP_UNKNOWN_PROTOCOL if not found
 */
uint8_t flipper_ir_proto_to_irmp(const char *name)
{
	const ir_proto_map_t *entry;

	if (name == NULL)
		return IRMP_UNKNOWN_PROTOCOL;

	for (entry = ir_proto_table; entry->flipper_name != NULL; entry++)
	{
		if (ff_strcasecmp(name, entry->flipper_name) == 0)
			return entry->irmp_id;
	}

	return IRMP_UNKNOWN_PROTOCOL;
}

/*============================================================================*/
/**
 * @brief  Map IRMP protocol ID to Flipper protocol name string
 * @param  irmp_id  IRMP protocol ID
 * @return Flipper protocol name, or "Unknown" if not found
 */
const char *flipper_ir_irmp_to_proto(uint8_t irmp_id)
{
	const ir_proto_map_t *entry;

	for (entry = ir_proto_table; entry->flipper_name != NULL; entry++)
	{
		if (entry->irmp_id == irmp_id)
			return entry->flipper_name;
	}

	return "Unknown";
}

/*============================================================================*/
/**
 * @brief  Open a .ir file and validate the header
 * @param  ctx   flipper file context
 * @param  path  file path
 * @return true if file opened and header is valid
 */
bool flipper_ir_open(flipper_file_t *ctx, const char *path)
{
	if (!ff_open(ctx, path))
		return false;

	if (!ff_validate_header(ctx, FLIPPER_IR_FILETYPE, FLIPPER_IR_MIN_VERSION))
	{
		ff_close(ctx);
		return false;
	}

	return true;
}

/*============================================================================*/
/**
 * @brief  Read the next IR signal from an open .ir file.
 *
 *         Parsed signal format:
 *           name: Power
 *           type: parsed
 *           protocol: NEC
 *           address: 07 00 00 00
 *           command: 02 00 00 00
 *
 *         Raw signal format:
 *           name: Power
 *           type: raw
 *           frequency: 38000
 *           duty_cycle: 0.330000
 *           data: 9024 4512 579 552 ...
 *
 * @param  ctx  flipper file context (must be open)
 * @param  out  output signal structure
 * @return true if a signal was read, false at EOF or error
 */
bool flipper_ir_read_signal(flipper_file_t *ctx, flipper_ir_signal_t *out)
{
	bool got_name = false;
	bool got_type = false;
	uint8_t hex_buf[4];
	uint8_t hex_count;

	if (ctx == NULL || out == NULL)
		return false;

	memset(out, 0, sizeof(flipper_ir_signal_t));
	out->valid = false;

	/* Scan for the start of a signal block */
	while (ff_read_line(ctx))
	{
		/* Skip separator lines */
		if (ff_is_separator(ctx))
			continue;

		if (!ff_parse_kv(ctx))
			continue;

		/* Look for "name:" to start a signal block */
		if (!got_name)
		{
			if (ff_strcasecmp(ff_get_key(ctx), "name") == 0)
			{
				strncpy(out->name, ff_get_value(ctx), FLIPPER_IR_NAME_MAX_LEN - 1);
				out->name[FLIPPER_IR_NAME_MAX_LEN - 1] = '\0';
				got_name = true;
			}
			continue;
		}

		/* After name, expect type */
		if (!got_type)
		{
			if (ff_strcasecmp(ff_get_key(ctx), "type") == 0)
			{
				if (ff_strcasecmp(ff_get_value(ctx), "parsed") == 0)
					out->type = FLIPPER_IR_SIGNAL_PARSED;
				else if (ff_strcasecmp(ff_get_value(ctx), "raw") == 0)
					out->type = FLIPPER_IR_SIGNAL_RAW;
				else
					return false;  /* Unknown type */

				got_type = true;
			}
			continue;
		}

		/* Parse type-specific fields */
		if (out->type == FLIPPER_IR_SIGNAL_PARSED)
		{
			if (ff_strcasecmp(ff_get_key(ctx), "protocol") == 0)
			{
				out->parsed.protocol = flipper_ir_proto_to_irmp(ff_get_value(ctx));
			}
			else if (ff_strcasecmp(ff_get_key(ctx), "address") == 0)
			{
				hex_count = ff_parse_hex_bytes(ff_get_value(ctx), hex_buf, 4);
				out->parsed.address = ff_hex_bytes_to_uint16_le(hex_buf, hex_count);
			}
			else if (ff_strcasecmp(ff_get_key(ctx), "command") == 0)
			{
				hex_count = ff_parse_hex_bytes(ff_get_value(ctx), hex_buf, 4);
				out->parsed.command = ff_hex_bytes_to_uint16_le(hex_buf, hex_count);
				out->parsed.flags = 0;
				out->valid = true;
				return true;  /* Parsed signal complete */
			}
		}
		else /* FLIPPER_IR_SIGNAL_RAW */
		{
			if (ff_strcasecmp(ff_get_key(ctx), "frequency") == 0)
			{
				out->raw.frequency = (uint32_t)strtoul(ff_get_value(ctx), NULL, 10);
			}
			else if (ff_strcasecmp(ff_get_key(ctx), "duty_cycle") == 0)
			{
				/* Parse float manually for embedded compatibility */
				const char *p = ff_get_value(ctx);
				int whole = 0;
				int frac = 0;
				int frac_div = 1;
				char *dot;

				whole = (int)strtol(p, NULL, 10);
				dot = strchr(p, '.');
				if (dot != NULL)
				{
					const char *fp = dot + 1;
					while (*fp >= '0' && *fp <= '9')
					{
						frac = frac * 10 + (*fp - '0');
						frac_div *= 10;
						fp++;
					}
				}
				out->raw.duty_cycle = (float)whole + (float)frac / (float)frac_div;
			}
			else if (ff_strcasecmp(ff_get_key(ctx), "data") == 0)
			{
				out->raw.sample_count = ff_parse_int32_array(
					ff_get_value(ctx),
					out->raw.samples,
					FLIPPER_IR_RAW_MAX_SAMPLES
				);
				out->valid = (out->raw.sample_count > 0);
				return true;  /* Raw signal complete */
			}
		}
	}

	/* If we got a partial signal at EOF, check if it is valid */
	if (got_name && got_type && out->valid)
		return true;

	return false;
}

/*============================================================================*/
/**
 * @brief  Write the .ir file header
 */
bool flipper_ir_write_header(flipper_file_t *ctx)
{
	if (ctx == NULL)
		return false;

	if (!ff_write_kv_str(ctx, "Filetype", FLIPPER_IR_FILETYPE))
		return false;
	if (!ff_write_kv_uint32(ctx, "Version", 1))
		return false;

	return true;
}

/*============================================================================*/
/**
 * @brief  Open an existing .ir file to append further signals. The file must
 *         already contain a valid header (written earlier via
 *         flipper_ir_write_header); this positions at EOF so the next
 *         flipper_ir_write_signal() appends without duplicating the header.
 * @param  ctx   flipper file context
 * @param  path  file path
 * @return true if the file was opened for appending
 */
bool flipper_ir_open_append(flipper_file_t *ctx, const char *path)
{
	return ff_open_append(ctx, path);
}

/*============================================================================*/
/**
 * @brief  Write a single IR signal to a .ir file
 */
bool flipper_ir_write_signal(flipper_file_t *ctx, const flipper_ir_signal_t *sig)
{
	if (ctx == NULL || sig == NULL)
		return false;

	/* Write separator before each signal */
	if (!ff_write_separator(ctx))
		return false;

	/* Write name */
	if (!ff_write_kv_str(ctx, "name", sig->name))
		return false;

	if (sig->type == FLIPPER_IR_SIGNAL_PARSED)
	{
		uint8_t addr_bytes[4];
		uint8_t cmd_bytes[4];

		if (!ff_write_kv_str(ctx, "type", "parsed"))
			return false;

		if (!ff_write_kv_str(ctx, "protocol", flipper_ir_irmp_to_proto(sig->parsed.protocol)))
			return false;

		/* Convert uint16 address to 4-byte little-endian hex */
		addr_bytes[0] = (uint8_t)(sig->parsed.address & 0xFF);
		addr_bytes[1] = (uint8_t)((sig->parsed.address >> 8) & 0xFF);
		addr_bytes[2] = 0;
		addr_bytes[3] = 0;
		if (!ff_write_kv_hex(ctx, "address", addr_bytes, 4))
			return false;

		/* Convert uint16 command to 4-byte little-endian hex */
		cmd_bytes[0] = (uint8_t)(sig->parsed.command & 0xFF);
		cmd_bytes[1] = (uint8_t)((sig->parsed.command >> 8) & 0xFF);
		cmd_bytes[2] = 0;
		cmd_bytes[3] = 0;
		if (!ff_write_kv_hex(ctx, "command", cmd_bytes, 4))
			return false;
	}
	else /* FLIPPER_IR_SIGNAL_RAW */
	{
		uint16_t i;
		int pos;
		char buf[FF_VALUE_MAX_LEN];

		if (!ff_write_kv_str(ctx, "type", "raw"))
			return false;

		if (!ff_write_kv_uint32(ctx, "frequency", sig->raw.frequency))
			return false;

		if (!ff_write_kv_float(ctx, "duty_cycle", sig->raw.duty_cycle))
			return false;

		/* Write data as space-separated int32 values */
		pos = 0;
		for (i = 0; i < sig->raw.sample_count; i++)
		{
			int written = snprintf(&buf[pos], sizeof(buf) - (size_t)pos,
			                       "%s%ld",
			                       (i > 0) ? " " : "",
			                       (long)sig->raw.samples[i]);
			if (written < 0 || (pos + written) >= (int)(sizeof(buf) - 1))
				break;
			pos += written;
		}
		buf[pos] = '\0';

		if (!ff_write_kv_str(ctx, "data", buf))
			return false;
	}

	return true;
}

/*============================================================================*/
/**
 * @brief  Rewrite a .ir file, streaming every signal through a callback into a
 *         temporary file and then atomically replacing the original. Used for
 *         edit operations (rename/delete a button) where the variable-length
 *         Flipper blocks make in-place editing impractical.
 *
 *         Only one signal is held in memory at a time (stack allocation, no
 *         heap). The callback may mutate the signal (e.g. rename) and returns
 *         true to keep it or false to drop it.
 *
 * @param  path  full path to the .ir file to rewrite in place
 * @param  cb    per-signal callback (must not be NULL)
 * @param  user  opaque pointer passed through to the callback
 * @return true if the file was rewritten and replaced successfully
 */
bool flipper_ir_rewrite(const char *path, flipper_ir_rewrite_cb_t cb, void *user)
{
	flipper_file_t      src;
	flipper_file_t      dst;
	flipper_ir_signal_t sig;              /* single signal in flight (stack) */
	char                tmp_path[256];
	uint16_t            index = 0;
	bool                ok = true;
	int                 n;

	if (path == NULL || cb == NULL)
		return false;

	/* Build "<path>.tmp"; bail out if it would be truncated. */
	n = snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
	if (n < 0 || n >= (int)sizeof(tmp_path))
		return false;

	/* Open source (validates header) and a fresh temp destination. */
	if (!flipper_ir_open(&src, path))
		return false;

	if (!ff_open_write(&dst, tmp_path))
	{
		ff_close(&src);
		return false;
	}

	if (!flipper_ir_write_header(&dst))
	{
		ff_close(&src);
		ff_close(&dst);
		f_unlink(tmp_path);
		return false;
	}

	/* Stream each signal through the callback into the temp file. */
	while (flipper_ir_read_signal(&src, &sig))
	{
		bool keep = cb(index, &sig, user);
		index++;

		if (keep)
		{
			if (!flipper_ir_write_signal(&dst, &sig))
			{
				ok = false;
				break;
			}
		}
	}

	ff_close(&src);
	ff_close(&dst);

	if (!ok)
	{
		f_unlink(tmp_path);
		return false;
	}

	/* Atomically replace the original. FatFs f_rename() will not overwrite an
	 * existing target, so unlink the original first, then rename the temp. */
	if (f_unlink(path) != FR_OK)
	{
		f_unlink(tmp_path);
		return false;
	}

	if (f_rename(tmp_path, path) != FR_OK)
		return false;

	return true;
}

/*============================================================================*/
/**
 * @brief  Begin accumulating a raw IR signal into sig. Clears sig, sets the raw
 *         type, and copies the given name. Sample buffer starts empty.
 * @param  sig   signal to initialize
 * @param  name  signal name (may be NULL)
 */
void flipper_ir_raw_begin(flipper_ir_signal_t *sig, const char *name)
{
	if (sig == NULL)
		return;

	memset(sig, 0, sizeof(*sig));
	sig->type = FLIPPER_IR_SIGNAL_RAW;
	sig->valid = false;

	if (name != NULL)
	{
		strncpy(sig->name, name, FLIPPER_IR_NAME_MAX_LEN - 1);
		sig->name[FLIPPER_IR_NAME_MAX_LEN - 1] = '\0';
	}
}

/*============================================================================*/
/**
 * @brief  Append one edge to a raw signal being accumulated. Marks (carrier on)
 *         are stored as +duration, spaces as -duration (Flipper raw convention).
 * @param  sig          signal being accumulated
 * @param  duration_us  edge duration in microseconds
 * @param  is_mark      true for a mark, false for a space
 * @return true if appended, false if the sample buffer is full
 */
bool flipper_ir_raw_add_edge(flipper_ir_signal_t *sig, uint32_t duration_us, bool is_mark)
{
	if (sig == NULL)
		return false;

	if (sig->raw.sample_count >= FLIPPER_IR_RAW_MAX_SAMPLES)
		return false;

	sig->raw.samples[sig->raw.sample_count++] =
		is_mark ? (int32_t)duration_us : -(int32_t)duration_us;

	return true;
}

/*============================================================================*/
/**
 * @brief  Finish raw accumulation: record carrier frequency and duty cycle and
 *         mark the signal valid if at least one edge was captured.
 * @param  sig         accumulated signal
 * @param  frequency   carrier frequency in Hz (e.g. 38000)
 * @param  duty_cycle  carrier duty cycle (e.g. 0.33)
 * @return true if the signal has samples (is valid)
 */
bool flipper_ir_raw_finish(flipper_ir_signal_t *sig, uint32_t frequency, float duty_cycle)
{
	if (sig == NULL)
		return false;

	sig->raw.frequency = frequency;
	sig->raw.duty_cycle = duty_cycle;
	sig->valid = (sig->raw.sample_count > 0);

	return sig->valid;
}

/*============================================================================*/
/**
 * @brief  Feed one received edge into a raw capture in progress (raw learn
 *         fallback for IRMP-undecodable signals). Ordinary edges are appended;
 *         the inter-frame timeout marker (frame_end) closes the frame, either
 *         finalizing the signal or discarding it as noise.
 * @param  sig          raw signal being accumulated (from flipper_ir_raw_begin)
 * @param  duration_us  edge duration in microseconds (ignored when frame_end)
 * @param  is_mark      true for a mark, false for a space (ignored when frame_end)
 * @param  frame_end    true if this is the inter-frame timeout marker
 * @param  min_samples  minimum edges for a real frame (fewer = noise, discarded)
 * @param  frequency    carrier frequency in Hz applied on FRAME_COMPLETE
 * @param  duty_cycle   carrier duty cycle applied on FRAME_COMPLETE
 * @return one of flipper_ir_raw_feed_result_t
 */
flipper_ir_raw_feed_result_t flipper_ir_raw_feed(flipper_ir_signal_t *sig,
                                                 uint32_t duration_us, bool is_mark,
                                                 bool frame_end, uint16_t min_samples,
                                                 uint32_t frequency, float duty_cycle)
{
	if (sig == NULL)
		return FLIPPER_IR_RAW_EDGE_DROPPED;

	if (!frame_end)
	{
		return flipper_ir_raw_add_edge(sig, duration_us, is_mark)
			? FLIPPER_IR_RAW_EDGE_ACCUMULATED
			: FLIPPER_IR_RAW_EDGE_DROPPED;
	}

	/* End of frame: enough edges -> finalize, otherwise reset as noise. */
	if (sig->raw.sample_count >= min_samples)
	{
		flipper_ir_raw_finish(sig, frequency, duty_cycle);
		return FLIPPER_IR_RAW_FRAME_COMPLETE;
	}

	sig->raw.sample_count = 0;   /* keep the name, drop the noise samples */
	sig->valid = false;
	return FLIPPER_IR_RAW_FRAME_NOISE;
}

/*============================================================================*/
/* Context + callback for flipper_ir_rename_signal(). */
typedef struct {
	uint16_t    target_index;
	const char *new_name;
} flipper_ir_rename_ctx_t;

static bool flipper_ir_rename_cb(uint16_t index, flipper_ir_signal_t *sig, void *user)
{
	flipper_ir_rename_ctx_t *ctx = (flipper_ir_rename_ctx_t *)user;

	if (index == ctx->target_index && ctx->new_name != NULL)
	{
		strncpy(sig->name, ctx->new_name, FLIPPER_IR_NAME_MAX_LEN - 1);
		sig->name[FLIPPER_IR_NAME_MAX_LEN - 1] = '\0';
	}
	return true;   /* keep every signal */
}

/*============================================================================*/
/**
 * @brief  Rename the signal at target_index in a .ir file, preserving all other
 *         signals. Out-of-range index leaves the file unchanged (returns true).
 * @param  path          .ir file path
 * @param  target_index  0-based index of the signal to rename
 * @param  new_name      replacement name
 * @return true on success (or harmless no-op)
 */
bool flipper_ir_rename_signal(const char *path, uint16_t target_index, const char *new_name)
{
	flipper_ir_rename_ctx_t ctx;

	ctx.target_index = target_index;
	ctx.new_name = new_name;

	return flipper_ir_rewrite(path, flipper_ir_rename_cb, &ctx);
}

/*============================================================================*/
/* Callback for flipper_ir_delete_signal(): drop the one matching index. The
 * target index is passed by pointer through the rewrite user parameter. */
static bool flipper_ir_delete_cb(uint16_t index, flipper_ir_signal_t *sig, void *user)
{
	uint16_t target = *(const uint16_t *)user;

	(void)sig;
	return index != target;   /* keep everything except the target */
}

/*============================================================================*/
/**
 * @brief  Delete the signal at target_index from a .ir file, preserving all
 *         other signals. Deleting the last one leaves a valid header-only file.
 *         Out-of-range index leaves the file unchanged (returns true).
 * @param  path          .ir file path
 * @param  target_index  0-based index of the signal to delete
 * @return true on success (or harmless no-op)
 */
bool flipper_ir_delete_signal(const char *path, uint16_t target_index)
{
	return flipper_ir_rewrite(path, flipper_ir_delete_cb, &target_index);
}

/*============================================================================*/
/**
 * @brief  Count the number of IR signals in a .ir file without loading all data
 * @param  path  file path
 * @return signal count, or 0 on error
 */
uint16_t flipper_ir_count_signals(const char *path)
{
	flipper_file_t ff;
	uint16_t count = 0;

	if (!flipper_ir_open(&ff, path))
		return 0;

	while (ff_read_line(&ff))
	{
		if (ff_is_separator(&ff))
			continue;

		if (ff_parse_kv(&ff))
		{
			if (ff_strcasecmp(ff_get_key(&ff), "name") == 0)
				count++;
		}
	}

	ff_close(&ff);
	return count;
}
