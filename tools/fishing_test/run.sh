#!/bin/bash
# Host build + run of the fishing rules test (no devkitPro needed).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$HERE/../.."
OUT="${TMPDIR:-/tmp}/fishing_test"
c++ -std=c++17 -Wall -Wextra -I"$ROOT/source" -o "$OUT" "$HERE/fishing_test.cpp" \
    "$ROOT/source/world/fish_logic.cpp" "$ROOT/source/world/fish_tables.cpp"
"$OUT"
