#!/bin/bash
# Host test for source/game/tug_logic: force formula and key decay must match upstream Python.
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
    "$ROOT/source/game/tug_logic.cpp" "$HERE/tug_test.cpp" -o "$OUT/tug_test"
"$OUT/tug_test" > "$OUT/cpp.txt"
"$PY" -I "$HERE/tug_ref.py" "$OTT" > "$OUT/py.txt"
if diff -u "$OUT/py.txt" "$OUT/cpp.txt" > "$OUT/diff.txt"; then
    echo "tug_logic matches upstream: $(grep -c '^force ' "$OUT/py.txt") forces, $(grep -c '^decay ' "$OUT/py.txt") decays"
else
    head -40 "$OUT/diff.txt"
    echo "TUG TEST FAILED"
    exit 1
fi
