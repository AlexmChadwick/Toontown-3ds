#!/bin/bash
# poses_run.sh <app.3dsx> <seconds> <outdir> <old|new> <poses.txt>: offline autotest "pose: N" pass in
# Azahar. poses.txt holds "pose <zone> <eye xyz> <target xyz> <toon xyz h>" lines (core/autotest.cpp);
# grabs every GRAB_EVERY s, then view_grabs.py writes <outdir>/views/shot_view_pose_<n>_{top,bottom}.png.
# TT_DATA as views_run.sh.
set -euo pipefail
APP=$1 SECS=$2 OUT=$3 MODE=$4 POSES=$5
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
mkdir -p "$OUT"
cd "$ROOT"
export TT_DATA=${TT_DATA:-$ROOT/sdmc-out/3ds/toontown3ds/data}
echo "poses_run: app $APP ($(sha256sum "$APP" | cut -c1-16)) mode $MODE TT_DATA $TT_DATA poses $POSES"
if [ "$MODE" = new ]; then export NEW3DS=1; fi
GRAB_DIR="$OUT/grab" GRAB_EVERY=${GRAB_EVERY:-1} AZAHAR="$HERE/az_grab_wrapper.sh" \
TT_OFFLINE=1 AUTOTEST_ONLY="pose:" AUTOTEST_EXTRA="shots off;$(paste -sd';' "$POSES")" \
nice -n 5 tools/dev/azahar_test.sh "$APP" "$SECS" "$OUT"
python3 -I "$HERE/view_grabs.py" "$OUT" "$OUT/views"
