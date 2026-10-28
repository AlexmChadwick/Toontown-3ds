#!/usr/bin/env bash
# SpeedChat ToonTask phrases vs upstream (vectors.txt from gen_vectors.py), then the real Client's
# setSCToontask / setWhisperSCToontaskFrom handling.
set -e
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$DIR/../.."
CXX="${CXX:-clang++}"
OUT="${TMPDIR:-/tmp}/tt_sc_test"
mkdir -p "$OUT"
"$CXX" -std=c++17 -Wall -Wextra -Werror -I"$ROOT/source" "$DIR/sc_test.cpp" \
    "$ROOT/source/quest/quest_data.cpp" "$ROOT/source/quest/quest_text.cpp" -o "$OUT/sc_test"
"$OUT/sc_test" "$DIR/vectors.txt"
"$CXX" -std=gnu++17 -O1 -g -Wall -Wextra -Werror -fno-rtti -fno-exceptions -I"$ROOT/source" \
    "$ROOT"/source/net/*.cpp "$ROOT"/source/game/*_logic.cpp "$ROOT/source/game/name_tables.cpp" \
    "$ROOT/source/quest/quest_data.cpp" "$ROOT/source/quest/quest_text.cpp" \
    "$ROOT/source/world/toon_dna.cpp" "$ROOT/source/world/suit_path.cpp" "$ROOT/source/world/level_spec.cpp" \
    "$ROOT/tools/nettest/log_host.cpp" "$DIR/client.cpp" -lpthread -o "$OUT/client"
"$OUT/client"
