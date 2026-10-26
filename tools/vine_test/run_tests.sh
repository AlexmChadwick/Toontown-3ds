#!/bin/bash
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
PY="${PY:-$ROOT/.venv/bin/python}"
OUT="$HERE/out"
mkdir -p "$OUT"
"${CXX:-clang++}" -std=gnu++17 -fno-rtti -fno-exceptions -O1 -g -Wall -Wextra -Werror \
    -fsanitize=address,undefined -I "$ROOT/source" \
    "$ROOT/source/game/vine_logic.cpp" "$HERE/vine_test.cpp" -o "$OUT/vine_test"
"$OUT/vine_test" > "$OUT/cpp.txt"
"$PY" -I "$HERE/vine_ref.py" "$@" > "$OUT/py.txt"
if diff -u "$OUT/py.txt" "$OUT/cpp.txt" > "$OUT/diff.txt"; then
    echo "vine_logic matches upstream: $(grep -c '^swing step ' "$OUT/py.txt") swings, $(grep -c '^jump frame ' "$OUT/py.txt") jumps, $(grep -c '^score: ' "$OUT/py.txt") scores"
else
    head -40 "$OUT/diff.txt"
    echo "VINE TEST FAILED"
    exit 1
fi
