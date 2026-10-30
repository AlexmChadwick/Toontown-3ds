#!/usr/bin/env bash
# Host tests for platform badges: net/platform_badge.* (enum mapping, should-show, batching,
# session table) and the DC numbering check (tools/dcgen/test_platform_dc.py).
set -euo pipefail
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$DIR/../.."
OUT="$DIR/out"
mkdir -p "$OUT"
CXX="${CXX:-$(command -v clang++ || command -v g++ || echo /tmp/rootfs/usr/bin/g++)}"
"$CXX" -std=c++17 -Wall -Wextra -Werror -fno-rtti -fno-exceptions \
    -fsanitize=address,undefined -fno-omit-frame-pointer -I"$ROOT/source" \
    "$DIR/platform_test.cpp" "$ROOT/source/net/platform_badge.cpp" -o "$OUT/platform_test"
"$OUT/platform_test"
PY="${PY:-$ROOT/tools/server/.venv/bin/python}"
"$PY" -I "$ROOT/tools/dcgen/test_platform_dc.py" ${TT_ETC:+"$TT_ETC"} 2>&1 | grep -v '^DCFile::read'
