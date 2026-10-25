#!/usr/bin/env bash
# Host tests for the pad focus model and the panel focus tables (docs/design/pad_navigation.md).
set -euo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$DIR/../.."
OUT="$DIR/out"
mkdir -p "$OUT"

clang++ -std=c++17 -Wall -Wextra -Werror -fno-rtti -fno-exceptions \
    -fsanitize=address,undefined -fno-omit-frame-pointer -I"$ROOT/source" \
    "$DIR/padnav_test.cpp" \
    "$ROOT/source/ui/focus.cpp" \
    "$ROOT/source/ui/pad_tables.cpp" \
    "$ROOT/tools/nettest/log_host.cpp" \
    -o "$OUT/padnav_test"

# The focus model logs every move; show only failures and the summary.
status=0
"$OUT/padnav_test" > "$OUT/padnav_test.log" || status=$?
grep -E "^FAIL|padnav_test:" "$OUT/padnav_test.log"
exit $status
