#!/bin/sh
# Bake the TTC terrain collision, generate Panda reference answers, build the host test
# (host clang++, no libctru) and run it, for the bare TTC terrain and the assembled TTC
# playground DNA. Usage: tools/collision_test/run.sh [model.bam]
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
RESOURCES="$ROOT/ref/resources"
[ -f "$RESOURCES/phase_4/dna/storage.dna" ] || RESOURCES="$ROOT/ref/ref/resources"
MODEL=${1:-$RESOURCES/phase_4/models/neighborhoods/toontown_central.bam}
OUT=${COLLISION_TEST_OUT:-${TMPDIR:-/tmp}/tt_collision_test}
mkdir -p "$OUT"
PY="$ROOT/.venv/bin/python -I -B"
TTC="$OUT/test.ttc"
$PY "$ROOT/tools/pipeline/bake_collision.py" "$MODEL" "$TTC" > "$OUT/bake.json"
$PY "$ROOT/tools/collision_test/panda_reference.py" "$MODEL" "$OUT/reference.txt"
clang++ -std=c++17 -O2 -Wall -Wextra -fno-exceptions -fno-rtti -I "$ROOT/source" \
    "$ROOT/source/world/collision.cpp" "$ROOT/tools/collision_test/log_stub.cpp" \
    "$ROOT/tools/collision_test/collision_test.cpp" -o "$OUT/collision_test"
echo "== terrain: $MODEL"
"$OUT/collision_test" "$TTC" "$OUT/reference.txt"
[ -n "$1" ] && exit 0
D=$RESOURCES/phase_4/dna
$PY "$ROOT/tools/pipeline/bake_collision.py" "$D/toontown_central_sz.dna" "$OUT/zone.ttc" \
    --storage "$D/storage.dna" "$D/storage_TT.dna" "$D/storage_TT_sz.dna" > "$OUT/zone_bake.json"
$PY "$ROOT/tools/collision_test/panda_reference.py" "$D/toontown_central_sz.dna" "$OUT/zone_reference.txt" \
    --storage "$D/storage.dna" "$D/storage_TT.dna" "$D/storage_TT_sz.dna"
echo "== assembled zone: toontown_central_sz.dna"
"$OUT/collision_test" "$OUT/zone.ttc" "$OUT/zone_reference.txt" --zone
