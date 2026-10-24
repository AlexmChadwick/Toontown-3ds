#!/usr/bin/env bash
set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$DIR/../.."

clang++ -std=c++17 -Wall -Wextra -I"$ROOT/source" \
    "$ROOT/tools/book_test/book_test.cpp" \
    "$ROOT/source/net/client_book.cpp" \
    "$ROOT/source/quest/quest_data.cpp" \
    "$ROOT/source/quest/quest_text.cpp" \
    -o "$DIR/book_test"

"$DIR/book_test"
