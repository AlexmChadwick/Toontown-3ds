#!/bin/bash
# run_gate.sh <app.3dsx> <outdir> [seconds]: the Old vs New 3DS fidelity gate. Runs the eight
# autotest "view:" steps in Azahar as Old, then as New (one emulator at a time), renders the
# near views from the stock assets (stock_detail.py, the 55 degree near lens, into <outdir>/stock),
# dumps each profile's chunk plan (tools/zone_test/view_plan.cpp, <outdir>/plan.txt) and scores
# them with compare_views.py. Exit status is compare_views.py's: 0 only when every view passes.
# TT_RESOURCES: the pinned ref/resources checkout (default this tree's). TT_DATA: the SD data set.
# Copy the .3dsx first if the tree may be rebuilt during the run (AGENTS.md section 6).
set -uo pipefail
APP=$1 OUT=$2 SECS=${3:-550}
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
export TT_DATA=${TT_DATA:-$ROOT/sdmc-out/3ds/toontown3ds/data}
export TT_RESOURCES=${TT_RESOURCES:-$ROOT/ref/resources}
mkdir -p "$OUT"
for mode in old new; do
    rm -rf "$OUT/v$mode"
    bash "$HERE/views_run.sh" "$APP" "$SECS" "$OUT/v$mode" "$mode" || echo "views_run $mode exited $?"
done
echo "stock: TT_RESOURCES $TT_RESOURCES ($(git -C "$TT_RESOURCES" rev-parse HEAD 2>/dev/null))" | tee "$OUT/stock.txt"
xvfb-run -a "$ROOT/.venv/bin/python" -I "$HERE/stock_detail.py" "$OUT/vold/views" "$OUT/vnew/views" \
    --out "$OUT/stock" --fovy 55 2>&1 | tee -a "$OUT/stock.txt"
flock /workspace/.native-build.lock c++ -std=c++17 -O1 -I "$ROOT/source" "$ROOT/tools/zone_test/view_plan.cpp" \
    "$ROOT/source/world/zone.cpp" "$ROOT/source/world/collision.cpp" -o "$OUT/view_plan"
{
    "$OUT/view_plan" "$TT_DATA/zones/2000.tz" ttc_near,ttc_mid,ttc_far,ttc_tree
    "$OUT/view_plan" "$TT_DATA/zones/2100.tz" street_near,street_far
    "$OUT/view_plan" "$TT_DATA/zones/1000.tz" dd_near,dd_far
} > "$OUT/plan.txt"
python3 "$HERE/compare_views.py" "$OUT/vold/views" "$OUT/vnew/views" \
    --old-log "$OUT/vold/log.txt" --new-log "$OUT/vnew/log.txt" \
    --stock "$OUT/stock" --plan "$OUT/plan.txt" | tee "$OUT/gate.txt"
status=${PIPESTATUS[0]}
echo "compare_views exit $status" | tee -a "$OUT/gate.txt"
exit "$status"
