#!/bin/bash
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$DIR/../.."

mkdir -p "$DIR/out"
python3 -I "$DIR/pattern_ref.py" > "$DIR/out/ref.txt"

clang++ -std=c++17 -I"$ROOT/source" "$DIR/pattern_test.cpp" "$ROOT/source/game/pattern_logic.cpp" -o "$DIR/out/pattern_test"
"$DIR/out/pattern_test" > "$DIR/out/cpp.txt"

if diff -u "$DIR/out/ref.txt" "$DIR/out/cpp.txt"; then
    echo "pattern_logic matches upstream: rounds, scoring, perfect bonuses"
else
    echo "PATTERN TEST FAILED"
    exit 1
fi
