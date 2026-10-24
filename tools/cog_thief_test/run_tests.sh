#!/bin/bash
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$DIR/../.."

mkdir -p "$DIR/out"
/Users/alexchadwick/toontown-3ds-minigames3/.venv/bin/python -I "$DIR/cog_thief_ref.py" > "$DIR/out/ref.txt"

clang++ -std=c++17 -I"$ROOT/source" "$DIR/cog_thief_test.cpp" "$ROOT/source/game/cog_thief_logic.cpp" -o "$DIR/out/cog_thief_test"
"$DIR/out/cog_thief_test" > "$DIR/out/cpp.txt"

if diff -u "$DIR/out/ref.txt" "$DIR/out/cpp.txt"; then
    echo "cog_thief_logic matches upstream: 18 evaluation cases"
else
    echo "COG THIEF TEST FAILED"
    exit 1
fi
