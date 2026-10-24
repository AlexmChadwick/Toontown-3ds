#!/bin/bash
# Host test for source/game/ring_logic: pattern generation, RNG, and collision detection.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OTT="${1:-$ROOT/ref/open-toontown}"
if [ ! -d "$OTT/toontown" ]; then
    if [ -d "$ROOT/ref/ref/open-toontown/toontown" ]; then
        OTT="$ROOT/ref/ref/open-toontown"
    else
        OTT="/Users/alexchadwick/toontown-3ds/ref/open-toontown"
    fi
fi
PY="${PY:-$ROOT/.venv/bin/python}"
OUT="$HERE/out"
mkdir -p "$OUT"
"${CXX:-clang++}" -std=gnu++17 -fno-rtti -fno-exceptions -O1 -g -Wall -Wextra -Werror \
    -fsanitize=address,undefined -I "$ROOT/source" \
    "$ROOT/source/game/ring_logic.cpp" "$HERE/ring_test.cpp" -o "$OUT/ring_test"
"$OUT/ring_test" > "$OUT/cpp.txt"
"$PY" -I "$HERE/ring_ref.py" "$OTT" > "$OUT/py.txt"
if diff -u "$OUT/py.txt" "$OUT/cpp.txt" > "$OUT/diff.txt"; then
    echo "ring_logic matches upstream: $(grep -c '^pattern ' "$OUT/py.txt") patterns, $(grep -c '^collide ' "$OUT/py.txt") collides"
else
    head -40 "$OUT/diff.txt"
    echo "RING TEST FAILED"
    exit 1
fi
