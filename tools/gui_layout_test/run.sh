#!/usr/bin/env bash
# Host checks for bottom-screen layout and the Map tab glyph.
set -euo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$DIR/../.."
OUT="$DIR/out"
mkdir -p "$OUT"

${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -fno-rtti -fno-exceptions \
    -I"$ROOT/source" \
    "$DIR/gui_layout_test.cpp" \
    -o "$OUT/gui_layout_test"

"$OUT/gui_layout_test"
python3 -I "$DIR/check_map_glyph.py"
