#!/bin/bash
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$DIR/../.."

mkdir -p "$DIR/out"
"$ROOT/.venv/bin/python" -I "$DIR/target_ref.py" > "$DIR/out/ref.txt"

clang++ -std=c++17 -I"$ROOT/source" "$DIR/target_test.cpp" "$ROOT/source/game/target_logic.cpp" -o "$DIR/out/target_test"
"$DIR/out/target_test" > "$DIR/out/cpp.txt"

if diff -u "$DIR/out/ref.txt" "$DIR/out/cpp.txt"; then
    echo "target_logic matches upstream: 3 seeds, 12 evaluations"
else
    echo "TARGET TEST FAILED"
    exit 1
fi
