#!/bin/bash
# Host test for transitions: tunnel tracks, door opening kinematics, portal scaling,
# and coordinate transformations.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="$HERE/out"
mkdir -p "$OUT"

echo "Building transition host test..."
"${CXX:-clang++}" -std=c++17 -O2 -Wall -Wextra -Werror \
    -fsanitize=address,undefined \
    -I "$ROOT/source" \
    "$HERE/transition_test.cpp" -o "$OUT/transition_test"

echo "Running transition host test..."
"$OUT/transition_test"
echo "Transition host tests completed successfully."
