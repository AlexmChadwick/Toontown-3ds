#!/usr/bin/env bash
set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$DIR/../.."

clang++ -std=c++17 -Wall -Wextra -I"$ROOT/source" \
    "$ROOT/tools/padnav_test/padnav_test.cpp" \
    "$ROOT/source/ui/focus.cpp" \
    "$ROOT/tools/nettest/log_host.cpp" \
    -o "$DIR/padnav_test"

"$DIR/padnav_test"
