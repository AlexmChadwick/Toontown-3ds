#!/bin/bash
# Build the host-side net test harness (no libctru). Output: tools/nettest/out/nettest
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
mkdir -p "$HERE/out"
CXX="${CXX:-clang++}"
"$CXX" -std=gnu++17 -fno-rtti -fno-exceptions -O1 -g -Wall -Wextra -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I "$ROOT/source" \
    "$ROOT"/source/net/*.cpp \
    "$ROOT"/source/game/*_logic.cpp \
    "$ROOT/source/game/name_tables.cpp" \
    "$ROOT/source/world/toon_dna.cpp" \
    "$ROOT/source/world/suit_path.cpp" \
    "$ROOT/source/world/level_spec.cpp" \
    "$HERE/log_host.cpp" \
    "$HERE/nettest.cpp" \
    -lpthread -o "$HERE/out/nettest"
echo "built $HERE/out/nettest"
