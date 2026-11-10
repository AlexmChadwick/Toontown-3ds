#!/usr/bin/env bash
# Host checks for Groups. No server, no emulator.
set -euo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$DIR/../.."

python3 -I "$DIR/test_groups.py"
# g++: this image's clang++ selects GCC 14 headers, which are not installed.
g++ -std=c++17 -Wall -Wextra -I"$ROOT/source" "$DIR/groups_test.cpp" -o "$DIR/groups_test"
"$DIR/groups_test"
