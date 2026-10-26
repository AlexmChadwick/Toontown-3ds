#!/bin/bash
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$DIR/../.."

mkdir -p "$DIR/out"
"$ROOT/.venv/bin/python" -I "$DIR/two_d_ref.py" > "$DIR/out/ref.txt"

clang++ -std=c++17 -I"$ROOT/source" "$DIR/two_d_test.cpp" "$ROOT/source/game/two_d_logic.cpp" -o "$DIR/out/two_d_test"
"$DIR/out/two_d_test" > "$DIR/out/cpp.txt"

if diff -u "$DIR/out/ref.txt" "$DIR/out/cpp.txt"; then
    echo "two_d_logic matches upstream: 7 evaluation cases"
else
    echo "TWO-D TEST FAILED"
    exit 1
fi
