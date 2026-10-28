#!/usr/bin/env bash
# SpeedChat ToonTask phrases vs upstream (vectors.txt from gen_vectors.py).
set -e
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$DIR/../.."
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -I"$ROOT/source" "$DIR/sc_test.cpp" \
    "$ROOT/source/quest/quest_data.cpp" "$ROOT/source/quest/quest_text.cpp" -o "$DIR/sc_test"
"$DIR/sc_test" "$DIR/vectors.txt"
