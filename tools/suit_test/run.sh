#!/bin/sh
# Bake the street suit graphs, record libtoontown's answers, build the host test of
# source/world/suit_path.cpp (host clang++, no libctru) and compare.
#
#   tools/suit_test/run.sh [queries per street]
#
# The reference needs the Open Toontown Panda3D fork from tools/server/setup.sh
# (TT_SERVER_PY overrides the interpreter); the bake uses the stock .venv. TT_ROOT points at
# the checkout that holds .venv, tools/server/.venv and ref/ when this runs in a worktree.
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
MAIN=${TT_ROOT:-$ROOT}
OUT=${SUIT_TEST_OUT:-${TMPDIR:-/tmp}/tt_suit_test}
mkdir -p "$OUT"
PY="$MAIN/.venv/bin/python -I -B"
SERVER_PY=${TT_SERVER_PY:-$MAIN/tools/server/.venv/bin/python}
RESOURCES="$MAIN/ref/resources"
[ -d "$RESOURCES" ] || RESOURCES="$MAIN/ref/ref/resources"
if [ ! -x "$SERVER_PY" ]; then
    echo "UNAVAILABLE: suit reference requires tools/server/.venv with panda3d.toontown"
    exit 77
fi
export TT_RESOURCES="$RESOURCES"
$PY "$ROOT/tools/pipeline/bake_suit_paths.py" --resources "$RESOURCES" --out "$OUT/zones" > /dev/null
"$SERVER_PY" -I -B "$ROOT/tools/suit_test/reference.py" "$OUT/reference.txt" --queries "${1:-400}" 2>/dev/null
clang++ -std=c++17 -O2 -Wall -Wextra -fno-exceptions -fno-rtti -I "$ROOT/source" \
    "$ROOT/source/world/suit_path.cpp" "$ROOT/tools/collision_test/log_stub.cpp" \
    "$ROOT/tools/suit_test/suit_test.cpp" -o "$OUT/suit_test"
"$OUT/suit_test" "$OUT/zones" "$OUT/reference.txt"
