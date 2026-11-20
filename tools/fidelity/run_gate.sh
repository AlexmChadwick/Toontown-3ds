#!/bin/bash
# run_gate.sh <app.3dsx> <outdir> [seconds]: the Old vs New 3DS fidelity gate. Runs the eight
# autotest "view:" steps in Azahar as Old, then as New (one emulator at a time), and scores them
# with compare_views.py. Exit status is compare_views.py's: 0 only when every view passes.
# Copy the .3dsx first if the tree may be rebuilt during the run (AGENTS.md section 6).
set -uo pipefail
APP=$1 OUT=$2 SECS=${3:-550}
HERE=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$OUT"
for mode in old new; do
    rm -rf "$OUT/v$mode"
    bash "$HERE/views_run.sh" "$APP" "$SECS" "$OUT/v$mode" "$mode" || echo "views_run $mode exited $?"
done
python3 "$HERE/compare_views.py" "$OUT/vold/views" "$OUT/vnew/views" \
    --old-log "$OUT/vold/log.txt" --new-log "$OUT/vnew/log.txt" | tee "$OUT/gate.txt"
status=${PIPESTATUS[0]}
echo "compare_views exit $status" | tee -a "$OUT/gate.txt"
exit "$status"
