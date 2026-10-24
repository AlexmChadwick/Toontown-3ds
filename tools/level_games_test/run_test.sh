#!/bin/bash
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$DIR/../.."
mkdir -p "$DIR/out"

clang++ -std=c++17 -Wall -Wextra -Werror \
    -I"$ROOT/source" \
    "$DIR/level_games_test.cpp" \
    "$ROOT/source/world/level_games.cpp" \
    -o "$DIR/out/level_games_test"

"$DIR/out/level_games_test"
