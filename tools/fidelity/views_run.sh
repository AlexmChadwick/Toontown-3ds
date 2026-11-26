#!/bin/bash
# views_run.sh <app.3dsx> <seconds> <outdir> <old|new>: offline autotest "view:" pass in Azahar with
# Xvfb grabs every GRAB_EVERY s (az_grab_wrapper.sh), then view_grabs.py writes
# <outdir>/views/shot_view_<name>_{top,bottom}.png for compare_views.py.
# TT_DATA (default: this checkout's sdmc-out data dir) is the SD data set the app reads; it is
# printed so a gate log records which bake it ran against.
set -euo pipefail
APP=$1 SECS=$2 OUT=$3 MODE=$4
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
mkdir -p "$OUT"
cd "$ROOT"
export TT_DATA=${TT_DATA:-$ROOT/sdmc-out/3ds/toontown3ds/data}
echo "views_run: app $APP ($(sha256sum "$APP" | cut -c1-16)) mode $MODE TT_DATA $TT_DATA"
if [ "$MODE" = new ]; then export NEW3DS=1; fi
GRAB_DIR="$OUT/grab" GRAB_EVERY=${GRAB_EVERY:-1} AZAHAR="$HERE/az_grab_wrapper.sh" \
TT_OFFLINE=1 AUTOTEST_ONLY="view:" AUTOTEST_EXTRA="shots off" \
nice -n 5 tools/dev/azahar_test.sh "$APP" "$SECS" "$OUT"
python3 -I "$HERE/view_grabs.py" "$OUT" "$OUT/views"
