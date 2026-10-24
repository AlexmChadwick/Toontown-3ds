#!/bin/bash
# Host test for source/game/cannon_logic: tower placement, flight physics, collision, and score
# must match upstream Python byte for byte.
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
    "$ROOT/source/game/cannon_logic.cpp" "$HERE/cannon_test.cpp" -o "$OUT/cannon_test"
"$OUT/cannon_test" > "$OUT/cpp.txt"
"$PY" -I "$HERE/cannon_ref.py" "$OTT" > "$OUT/py.txt"
if diff -u "$OUT/py.txt" "$OUT/cpp.txt" > "$OUT/diff.txt"; then
    echo "cannon_logic matches upstream: $(grep -c '^shot ' "$OUT/py.txt") shots, $(grep -c '^tower ' "$OUT/py.txt") towers"
else
    head -40 "$OUT/diff.txt"
    echo "CANNON TEST FAILED"
    exit 1
fi
