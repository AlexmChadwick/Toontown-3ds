#!/usr/bin/env bash
# Main-agent-only Azahar pass; this script is syntax/dry-run verified on qafix.
# Usage: tools/dev/minigame_pass.sh [--dry-run] [id ...]
# MG_SECONDS=360 MG_APP=/absolute/app.3dsx tools/dev/minigame_pass.sh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
DRY=0
if [ "${1:-}" = --dry-run ]; then DRY=1; shift; fi
if [ "$#" = 0 ]; then set -- 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16; fi
for id in "$@"; do
    case "$id" in 1|2|3|4|5|6|7|8|9|10|11|12|13|14|15|16) ;; *) echo "Invalid game id: $id" >&2; exit 2;; esac
done
APP=${MG_APP:-$ROOT/toontown3ds.3dsx}
SECONDS_PER_GAME=${MG_SECONDS:-360}
case "$SECONDS_PER_GAME" in ''|*[!0-9]*|0) echo 'MG_SECONDS must be a positive integer' >&2; exit 2;; esac
if [ "$DRY" = 1 ]; then
    for id in "$@"; do
        echo "id=$id AI_MINIGAME_ID=$id stop/start; AUTOTEST_ONLY=board trolley,trolley: ride,trolley: game,trolley: purchase tools/dev/azahar_test.sh $APP $SECONDS_PER_GAME /tmp/mg_$id"
    done
    exit 0
fi
LOCK="${TMPDIR:-/tmp}/toontown3ds_minigame_pass.lock"
if ! mkdir "$LOCK" 2>/dev/null; then echo "Another minigame pass owns $LOCK" >&2; exit 1; fi
cleanup() { bash tools/server/stop.sh >/dev/null 2>&1 || true; rmdir "$LOCK" 2>/dev/null || true; }
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM HUP
FAILED=0
for id in "$@"; do
    OUT="/tmp/mg_$id"
    mkdir -p "$OUT"
    echo "Minigame $id -> $OUT"
    bash tools/server/stop.sh > "$OUT/server-stop.log" 2>&1
    if ! AI_MINIGAME_ID="$id" bash tools/server/start.sh > "$OUT/server-start.log" 2>&1; then
        echo "FAIL id=$id server did not start" | tee "$OUT/result.txt"
        FAILED=1
        continue
    fi
    if ! AUTOTEST_ONLY='board trolley,trolley: ride,trolley: game,trolley: purchase' \
        bash tools/dev/azahar_test.sh "$APP" "$SECONDS_PER_GAME" "$OUT" > "$OUT/runner.log" 2>&1; then
        echo "FAIL id=$id emulator runner" | tee "$OUT/result.txt"
        FAILED=1
        continue
    fi
    cp tools/server/logs/ai.log "$OUT/ai.log"
    cp tools/server/logs/uberdog.log "$OUT/uberdog.log"
    OK=1
    for pattern in "mgpass: start id $id$" "mgpass: score id $id beans" \
        "mgpass: end id $id aborted 0" "mgpass: purchase exit id $id acknowledged" "mgpass: returned id $id$"; do
        if ! grep -Eq "$pattern" "$OUT/log.txt"; then echo "Missing: $pattern" >> "$OUT/result.txt"; OK=0; fi
    done
    if [ "$OK" = 1 ]; then
        echo "PASS id=$id lifecycle (review shots and AI errors separately)" | tee "$OUT/result.txt"
    else
        echo "FAIL id=$id incomplete lifecycle" | tee -a "$OUT/result.txt"
        FAILED=1
    fi
done
exit "$FAILED"
