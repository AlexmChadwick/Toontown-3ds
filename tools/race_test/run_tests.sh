#!/bin/bash
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$DIR/../.."

mkdir -p "$DIR/out"
# The lane table is generated from DistributedRaceGame.posHprArray; it must not go stale.
"$ROOT/.venv/bin/python" -I "$ROOT/tools/gen_race_tables.py" --check
python3 -I "$DIR/race_ref.py" > "$DIR/out/ref.txt"

clang++ -std=c++17 -I"$ROOT/source" "$DIR/race_test.cpp" "$ROOT/source/game/race_logic.cpp" -o "$DIR/out/race_test"
"$DIR/out/race_test" > "$DIR/out/cpp.txt"

if diff -u "$DIR/out/ref.txt" "$DIR/out/cpp.txt"; then
    echo "race_logic matches upstream: choices, conflicts, chance cards, win payout"
else
    echo "RACE TEST FAILED"
    exit 1
fi
