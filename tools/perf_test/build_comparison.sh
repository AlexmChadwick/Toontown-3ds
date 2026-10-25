#!/bin/bash
# All scratch remains in this checkout; no other worktree or emulator is used.
set -euo pipefail
OUT=sdmc-out/perf
BASE="$OUT/profile-baseline"
mkdir -p "$OUT"
if [ ! -d "$BASE" ]; then
    mkdir "$BASE"
    git archive aae224c | tar -x -C "$BASE"
    git diff aae224c 5b3ad75 > "$OUT/profiler-only.patch"
    git apply --directory="$BASE" "$OUT/profiler-only.patch"
fi
# Same capture-off control in both builds; no rendering optimization is copied.
cp source/core/autotest.h "$BASE/source/core/autotest.h"
cp source/core/autotest.cpp "$BASE/source/core/autotest.cpp"
DEVKITPRO="$HOME/devkitpro" DEVKITARM="$HOME/devkitpro/devkitARM" make -j4 -C "$BASE" > "$OUT/profile-baseline-build.log" 2>&1
cp "$BASE/toontown3ds.3dsx" "$OUT/profile-baseline.3dsx"
echo "Built profiler-only baseline: $OUT/profile-baseline.3dsx"
