/* See COPYING.txt for license details. */

/*
 * validate_ir.c  (HOST TOOL)
 *
 * Standalone .ir validator: parses each path given on the command line with the
 * real m1_csrc/flipper_ir layer (compiled against the host FatFs shim) and
 * checks that the file has a valid Flipper header and at least one parseable
 * signal. Exits non-zero if any file fails. Used to gate the hand-authored
 * ir_database sets and, in the regression sweep, any M1-authored .ir output.
 *
 * M1 Project — host test harness
 */

#include "flipper_ir.h"
#include "flipper_file.h"

#include <stdio.h>

int main(int argc, char **argv)
{
	int failures = 0;
	int i;

	if (argc < 2)
	{
		printf("usage: %s <file.ir> [file.ir ...]\n", argv[0]);
		return 2;
	}

	for (i = 1; i < argc; i++)
	{
		flipper_file_t ff;
		bool     header_ok;
		uint16_t signals;

		header_ok = flipper_ir_open(&ff, argv[i]);
		if (header_ok)
			ff_close(&ff);

		signals = flipper_ir_count_signals(argv[i]);

		if (!header_ok || signals == 0)
		{
			printf("  FAIL: %s  (valid_header=%d, signals=%u)\n",
			       argv[i], (int)header_ok, (unsigned)signals);
			failures++;
		}
		else
		{
			printf("  OK:   %s  (%u signals)\n", argv[i], (unsigned)signals);
		}
	}

	printf("== %d file(s) checked, %d failure(s) ==\n", argc - 1, failures);
	return (failures == 0) ? 0 : 1;
}
