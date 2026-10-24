#!/bin/sh
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${TMPDIR:-/tmp}/tt_battle_cam_test
mkdir -p "$OUT"

clang++ -std=c++17 -O2 -Wall -Wextra -Werror \
    -I "$ROOT/source" \
    "$ROOT/source/world/battle_camera.cpp" \
    "$ROOT/tools/battle_cam_test/battle_cam_test.cpp" \
    -o "$OUT/battle_cam_test"

"$OUT/battle_cam_test"
