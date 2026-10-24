#!/bin/bash
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
PY="${PY:-$ROOT/.venv/bin/python}"
OUT="$HERE/out"
mkdir -p "$OUT"
"${CXX:-clang++}" -std=gnu++17 -fno-rtti -fno-exceptions -O1 -g -Wall -Wextra -Werror \
    -fsanitize=address,undefined -I "$ROOT/source" \
    "$ROOT/source/game/diving_logic.cpp" "$HERE/diving_test.cpp" -o "$OUT/diving_test"
"$OUT/diving_test" > "$OUT/cpp.txt"
"$PY" -I "$HERE/diving_ref.py" > "$OUT/py.txt"
if diff -u "$OUT/py.txt" "$OUT/cpp.txt" > "$OUT/diff.txt"; then
    echo "diving_logic matches upstream: $(grep -c '^dive frame ' "$OUT/py.txt") frames, $(grep -c '^score: ' "$OUT/py.txt") scores"
else
    head -40 "$OUT/diff.txt"
    echo "DIVING TEST FAILED"
    exit 1
fi
