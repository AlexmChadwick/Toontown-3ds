#!/bin/sh
set -eu

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${TMPDIR:-/tmp}/tt_games2_test
mkdir -p "$OUT"

"$ROOT/.venv/bin/python" -I -B "$ROOT/tools/gen_photo_tables.py" > "$OUT/photo_tables.h"
cmp "$OUT/photo_tables.h" "$ROOT/source/game/photo_tables.h"
"$ROOT/.venv/bin/python" -I -B "$ROOT/tools/gen_toonblitz_tables.py" > "$OUT/two_d_tables.h"
cmp "$OUT/two_d_tables.h" "$ROOT/source/game/two_d_tables.h"

clang++ -std=c++17 -fno-exceptions -fno-rtti -O1 -Wall -Wextra -Werror -I "$ROOT/source" \
    "$ROOT/tools/games2_test/photo_test.cpp" "$ROOT/source/game/photo_logic.cpp" -o "$OUT/photo_test"
"$OUT/photo_test"

"$ROOT/.venv/bin/python" -I -B "$ROOT/tools/games2_test/twod_reference.py"
clang++ -std=c++17 -fno-exceptions -fno-rtti -O1 -Wall -Wextra -Werror -I "$ROOT/source" \
    "$ROOT/tools/games2_test/twod_test.cpp" "$ROOT/source/net/"*.cpp "$ROOT/source/game/"*_logic.cpp \
    "$ROOT/source/game/name_tables.cpp" "$ROOT/source/world/toon_dna.cpp" "$ROOT/source/world/suit_path.cpp" \
    "$ROOT/source/world/level_spec.cpp" "$ROOT/tools/nettest/log_host.cpp" -lpthread -o "$OUT/twod_test"
"$OUT/twod_test"
