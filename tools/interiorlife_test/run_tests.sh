#!/bin/bash
# Host tests for interior life:
#   1. C++ tests: leaderboard pickle decode, knock-knock jokes, door swing kinematics
#   2. Python test: byte-identical bakes across multiple runs
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
PY="$ROOT/.venv/bin/python"
OUT="$HERE/out"
mkdir -p "$OUT"

echo "Building interior life C++ host test..."
CXX="${CXX:-clang++}"
"$CXX" -std=c++17 -O2 -Wall -Wextra -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I "$ROOT/source" \
    "$HERE/interiorlife_test.cpp" \
    "$ROOT/source/net/client_interior.cpp" \
    "$ROOT/source/game/knockknock_jokes.cpp" \
    "$ROOT/tools/nettest/log_host.cpp" \
    -DTT_HOST_TEST \
    -o "$OUT/interiorlife_test"

echo "Running interior life C++ host test..."
"$OUT/interiorlife_test"

echo "Running byte-identical bake test..."
"$PY" -I "$HERE/test_bakes.py"

echo "ALL INTERIOR LIFE HOST TESTS PASSED SUCCESSFULLY."
