#!/bin/bash
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$DIR/../.."
mkdir -p "$DIR/out"
clang++ -std=c++17 -I"$ROOT/source" "$DIR/golf_test.cpp" "$ROOT/source/game/golf_logic.cpp" -o "$DIR/out/golf_test"
"$DIR/out/golf_test"
