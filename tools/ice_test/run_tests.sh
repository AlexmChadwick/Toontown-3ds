#!/bin/bash
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$DIR/../.."

mkdir -p "$DIR/out"
python3 -I "$DIR/ice_ref.py" > "$DIR/out/ref.txt"

clang++ -std=c++17 -I"$ROOT/source" "$DIR/ice_test.cpp" "$ROOT/source/game/ice_logic.cpp" -o "$DIR/out/ice_test"
"$DIR/out/ice_test" > "$DIR/out/cpp.txt"

if diff -u "$DIR/out/ref.txt" "$DIR/out/cpp.txt"; then
    echo "ice_logic matches upstream: distance-to-center curve, bonus tiers, placements"
else
    echo "ICE TEST FAILED"
    exit 1
fi
