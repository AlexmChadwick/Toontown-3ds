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
# The DCFile check fails rather than skips without panda3d, so prefer an
# interpreter that has it. Override with PYTHON=.
if [ -z "${PYTHON:-}" ]; then
    PYTHON=python3
    if ! python3 -c 'import panda3d' 2>/dev/null; then
        if command -v python3.9 >/dev/null && python3.9 -c 'import panda3d' 2>/dev/null; then
            PYTHON=python3.9
        fi
    fi
fi
"$PYTHON" "$DIR/test_groups.py"
# g++: this image's clang++ selects GCC 14 headers, which are not installed.
# The binary stays in /tmp so no compiled artifact lands in the repo.
g++ -std=c++17 -Wall -Wextra -I"$ROOT/source" "$DIR/groups_test.cpp" -o /tmp/groups_test_bin
/tmp/groups_test_bin
