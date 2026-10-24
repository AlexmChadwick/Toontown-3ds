#!/bin/sh
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${TMPDIR:-/tmp}/tt_battle_fx_test
mkdir -p "$OUT"

clang++ -std=c++17 -O2 -Wall -Wextra -Werror \
    -I "$ROOT/source" \
    "$ROOT/source/world/battle_fx.cpp" \
    "$ROOT/tools/battle_fx_test/battle_fx_test.cpp" \
    -o "$OUT/battle_fx_test"

"$OUT/battle_fx_test"
