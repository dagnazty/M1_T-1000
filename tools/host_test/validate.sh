#!/bin/sh
# Host regression sweep for the IR file layer + shipped .ir database.
#
# Two gates, both host-side (no firmware build, no hardware):
#   1. The .ir round-trip unit suite (append / rewrite / rename / delete / raw).
#   2. Every shipped ir_database/**/*.ir parses (valid header + >= 1 signal) via
#      the standalone validator — the same check applied to M1-authored output.
#
# Run from anywhere; paths resolve against the repo root.
set -e

DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../.." && pwd)"
CC="${CC:-cc}"

echo "== 1/2: .ir round-trip unit suite =="
sh "$DIR/run_tests.sh"

echo
echo "== 2/2: ir_database .ir validation sweep =="
VAL="$DIR/validate_ir"
"$CC" -std=c11 -Wall -Wextra -O0 \
	-I"$DIR" -I"$ROOT/m1_csrc" \
	"$DIR/validate_ir.c" \
	"$DIR/ff_shim.c" \
	"$ROOT/m1_csrc/flipper_file.c" \
	"$ROOT/m1_csrc/flipper_ir.c" \
	-o "$VAL"

# Collect every shipped .ir file and validate in one pass.
find "$ROOT/ir_database" -name '*.ir' -print0 | xargs -0 "$VAL"

echo
echo "== regression sweep passed =="
