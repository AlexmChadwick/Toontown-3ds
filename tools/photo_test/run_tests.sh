#!/bin/bash
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$DIR/../.."

mkdir -p "$DIR/out"
python3 -I "$DIR/photo_ref.py" > "$DIR/out/ref.txt"

clang++ -std=c++17 -I"$ROOT/source" "$DIR/photo_test.cpp" "$ROOT/source/game/photo_logic.cpp" -o "$DIR/out/photo_test"
clang++ -std=c++17 -Wall -Wextra -I"$ROOT/source" "$DIR/aim_test.cpp" "$ROOT/source/game/photo_logic.cpp" -o "$DIR/out/aim_test"
"$DIR/out/aim_test"
"$DIR/out/photo_test" > "$DIR/out/cpp.txt"

if diff -u "$DIR/out/ref.txt" "$DIR/out/cpp.txt"; then
    echo "photo_logic matches upstream: assignments, film counts, team + high-scorer scoring"
else
    echo "PHOTO TEST FAILED"
    exit 1
fi
