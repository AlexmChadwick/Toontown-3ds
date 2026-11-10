#!/usr/bin/env bash
# Host checks for Groups. No server, no emulator.
set -euo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$DIR/../.."

# Not python3 -I: the DCFile check imports panda3d from the user site.
# otp.dc and toon.dc come from $OTT or ref/open-toontown.
if [ -z "${OTT:-}" ] && [ -f "$ROOT/ref/open-toontown/etc/otp.dc" ]; then
    export OTT="$ROOT/ref/open-toontown"
fi
python3 "$DIR/test_groups.py"
# g++: this image's clang++ selects GCC 14 headers, which are not installed.
g++ -std=c++17 -Wall -Wextra -I"$ROOT/source" "$DIR/groups_test.cpp" -o "$DIR/groups_test"
"$DIR/groups_test"
