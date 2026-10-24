#!/bin/bash
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
PY="${PY:-$ROOT/.venv/bin/python}"
OUT="$HERE/out"
mkdir -p "$OUT"
"${CXX:-clang++}" -std=gnu++17 -fno-rtti -fno-exceptions -O1 -g -Wall -Wextra -Werror \
    -fsanitize=address,undefined -I "$ROOT/source" \
    "$ROOT/source/game/pairing_logic.cpp" "$HERE/pairing_test.cpp" -o "$OUT/pairing_test"
"$OUT/pairing_test" > "$OUT/cpp.txt"
"$PY" -I "$HERE/pairing_ref.py" > "$OUT/py.txt"
if diff -u "$OUT/py.txt" "$OUT/cpp.txt" > "$OUT/diff.txt"; then
    echo "pairing_logic matches upstream: $(grep -c '^seed ' "$OUT/py.txt") seeds, $(grep -c '^eval: ' "$OUT/py.txt") evaluations"
else
    head -40 "$OUT/diff.txt"
    echo "PAIRING TEST FAILED"
    exit 1
fi
