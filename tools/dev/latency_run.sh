#!/usr/bin/env bash
# Cross-play receive-latency regression: the 3DS (Azahar, Old 3DS mode) idles in TTC and says a
# SpeedChat probe every 2 s; the PC bot echoes it at once and runs circles beside it; then
# tools/dev/latency_check.py reports the round trip, the one-way estimate and the 3DS's own
# socket->handled delay, and fails above 45 ms one way.
#
#   [SERVER=127.0.0.1:7198] tools/dev/latency_run.sh [app.3dsx] [seconds] [out-dir]
#
# Needs a running server (tools/server/start.sh; SERVER_PORT/SERVER_RUN for a private one).
set -euo pipefail
cd "$(dirname "$0")/../.."
APP=${1:-$PWD/toontown3ds.3dsx}
SECS=${2:-120}
OUT=${3:-$(mktemp -d)}
SERVER=${SERVER:-127.0.0.1:7198}
mkdir -p "$OUT"
# The bot joins once the 3DS is in the zone (boot + login take ~25 s of emulated time).
( sleep 30; timeout $((SECS + 30)) tools/server/.venv/bin/python -I tools/server/smoke_test.py \
    --host "${SERVER%:*}" --port "${SERVER##*:}" --token latency-bot --echo 100 101 \
    --move --center 0,-20 --radius 15 --linger $((SECS - 35)) > "$OUT/bot.txt" 2>&1 ) &
BOT=$!
# VM pauses on shared rigs fake latency: log wall time beside the run (gaps > 35 s = discard).
( while true; do date +%s; sleep 30; done > "$OUT/clock.txt" ) &
CLK=$!
SERVER=$SERVER TOKEN=${TOKEN:-latency-3ds} AUTOTEST_ONLY=idle AUTOTEST_EXTRA="chat every 2;shots off" \
    tools/dev/azahar_test.sh "$APP" "$SECS" "$OUT" | tail -1
kill $CLK 2>/dev/null || true
wait $BOT 2>/dev/null || true
GAP=$(awk 'NR > 1 { d = $1 - p; if (d > m) m = d } { p = $1 } END { print m + 0 }' "$OUT/clock.txt")
[ "$GAP" -gt 35 ] && echo "warning: the host paused for ${GAP} s during the run; discard it"
python3 -I tools/dev/latency_check.py "$OUT/log.txt" "$OUT/bot.txt"
