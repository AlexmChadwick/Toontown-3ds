#!/bin/bash
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$DIR/../.."

mkdir -p "$DIR/out"
python3 -I "$DIR/tag_ref.py" > "$DIR/out/ref.txt"

clang++ -std=c++17 -I"$ROOT/source" "$DIR/tag_test.cpp" "$ROOT/source/game/tag_logic.cpp" -o "$DIR/out/tag_test"
clang++ -std=c++17 -Wall -Wextra -I"$ROOT/source" "$DIR/walk_test.cpp" "$ROOT/source/game/tag_logic.cpp" -o "$DIR/out/walk_test"
"$DIR/out/walk_test"
"$DIR/out/tag_test" > "$DIR/out/cpp.txt"

if diff -u "$DIR/out/ref.txt" "$DIR/out/cpp.txt"; then
    echo "tag_logic matches upstream: IT transitions, tagbacks, treasures"
else
    echo "TAG TEST FAILED"
    exit 1
fi
