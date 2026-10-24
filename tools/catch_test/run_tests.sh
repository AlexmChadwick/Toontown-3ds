#!/bin/bash
# Host test for source/game/catch_logic: the drop schedule, suit walks and RNG of the C++
# port must match the upstream Python (catch_ref.py runs the real DropScheduler,
# DropPlacer and Panda's RandomNumGen) byte for byte.
# Usage: tools/catch_test/run_tests.sh [path/to/ref/open-toontown]
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OTT="${1:-$ROOT/ref/open-toontown}"
PY="${PY:-$ROOT/.venv/bin/python}"
OUT="$HERE/out"
mkdir -p "$OUT"
"${CXX:-clang++}" -std=gnu++17 -fno-rtti -fno-exceptions -O1 -g -Wall -Wextra -Werror \
    -fsanitize=address,undefined -I "$ROOT/source" \
    "$ROOT/source/game/catch_logic.cpp" "$HERE/catch_test.cpp" -o "$OUT/catch_test"
"$OUT/catch_test" > "$OUT/cpp.txt"
"$PY" -I "$HERE/catch_ref.py" "$OTT" > "$OUT/py.txt"
if diff -u "$OUT/py.txt" "$OUT/cpp.txt" > "$OUT/diff.txt"; then
    echo "catch_logic matches upstream: $(grep -c '^drop ' "$OUT/py.txt") drops, $(grep -c '^suit ' "$OUT/py.txt") suit walks, $(grep -c '^case ' "$OUT/py.txt") cases"
else
    head -40 "$OUT/diff.txt"
    echo "CATCH TEST FAILED"
    exit 1
fi
