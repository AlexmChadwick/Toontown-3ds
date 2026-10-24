#!/bin/bash
# Start the real Open Toontown server stack locally:
#   astrond (x86_64 prebuilt, Rosetta)  ->  UberDOG  ->  AI district
# Client agent listens on 0.0.0.0:7198 (MD on 127.0.0.1:7199).
#
# Usage:  tools/server/start.sh          (run tools/server/setup.sh once first)
#         tools/server/stop.sh
#         tools/server/start.sh --reset  (wipe accounts/toons database first)
#
# Nothing is written inside ref/: runtime state lives in tools/server/run/
# (database, accounts.json, pycache) and logs in tools/server/logs/.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OTT="$ROOT/ref/open-toontown"
RES="$ROOT/ref/resources"
RUN="$HERE/run"
LOGS="$HERE/logs"
PY="$HERE/.venv/bin/python"

[ -x "$PY" ] || { echo "Run $HERE/setup.sh first"; exit 1; }
"$PY" -I -c "import panda3d.toontown" 2>/dev/null || { echo "Panda3D fork missing; run $HERE/setup.sh"; exit 1; }
[ -d "$RES/phase_3" ] || { echo "Missing $RES (ref/resources submodule)"; exit 1; }

if [ -f "$RUN/pids" ]; then
    alive=0
    while read -r _ pid; do kill -0 "$pid" 2>/dev/null && alive=1; done < "$RUN/pids"
    [ $alive = 1 ] && { echo "Server already running (see $RUN/pids). Run stop.sh first."; exit 1; }
    rm -f "$RUN/pids"
fi
for port in 7198 7199; do
    if lsof -nP -iTCP:$port -sTCP:LISTEN >/dev/null 2>&1; then
        echo "Port $port already in use:"; lsof -nP -iTCP:$port -sTCP:LISTEN; exit 1
    fi
done

if [ "${1:-}" = "--reset" ]; then
    rm -rf "$RUN/astrondb" "$RUN/astron/databases/accounts.json"
fi

mkdir -p "$RUN/astrondb" "$RUN/astron/databases" "$RUN/pycache" "$LOGS"
ln -sfn "$OTT/etc" "$RUN/etc"
ln -sfn "$RES" "$RUN/resources"

# Astron config: identical to ref/open-toontown/astron/config/astrond.yml except
# for absolute paths (dc files, db dir, event log) so nothing lands in ref/.
sed -e "s#\.\./\.\./etc/#$OTT/etc/#g" \
    -e "s#directory: \.\./databases/astrondb#directory: $RUN/astrondb#" \
    -e "s#output: \.\./logs/#output: $LOGS/#" \
    "$OTT/astron/config/astrond.yml" > "$RUN/astrond.yml"

STAMP="$(date +%Y%m%d_%H%M%S)"
: > "$RUN/pids"

wait_for() {  # wait_for <file> <regex> <seconds> <name> <pid>
    local i=0
    while ! grep -qE "$2" "$1" 2>/dev/null; do
        if ! kill -0 "$5" 2>/dev/null; then echo "$4 died; see $1"; tail -20 "$1"; "$HERE/stop.sh"; exit 1; fi
        i=$((i+1)); [ $i -gt $(( $3 * 5 )) ] && { echo "$4 not ready after $3 s; see $1"; "$HERE/stop.sh"; exit 1; }
        sleep 0.2
    done
}

# 1. Astron
# (cwd = astron/darwin so the bare dylib install names resolve; DYLD_* vars
#  would be stripped by SIP-protected wrappers like nohup/env.)
(cd "$OTT/astron/darwin" && exec ./astrond --boring --loglevel info "$RUN/astrond.yml") \
    > "$LOGS/astrond-$STAMP.log" 2>&1 < /dev/null &
APID=$!; echo "astrond $APID" >> "$RUN/pids"
ln -sfn "astrond-$STAMP.log" "$LOGS/astrond.log"
i=0; until lsof -nP -iTCP:7199 -sTCP:LISTEN >/dev/null 2>&1 && lsof -nP -iTCP:7198 -sTCP:LISTEN >/dev/null 2>&1; do
    kill -0 $APID 2>/dev/null || { echo "astrond died:"; cat "$LOGS/astrond-$STAMP.log"; "$HERE/stop.sh"; exit 1; }
    i=$((i+1)); [ $i -gt 100 ] && { echo "astrond not listening"; "$HERE/stop.sh"; exit 1; }
    sleep 0.2
done
echo "astrond up (pid $APID)"

cd "$RUN"
export PYTHONPATH="$OTT"
export PYTHONPYCACHEPREFIX="$RUN/pycache"
export PYTHONUNBUFFERED=1

# 2. UberDOG (same args as ref/open-toontown/darwin/start-uberdog-server.sh)
"$PY" -m toontown.uberdog.UDStart --base-channel 1000000 --max-channels 999999 \
    --stateserver 4002 --messagedirector-ip 127.0.0.1:7199 --eventlogger-ip 127.0.0.1:7197 \
    > "$LOGS/uberdog-$STAMP.log" 2>&1 < /dev/null &
UPID=$!; echo "uberdog $UPID" >> "$RUN/pids"
ln -sfn "uberdog-$STAMP.log" "$LOGS/uberdog.log"
wait_for "$LOGS/uberdog-$STAMP.log" "UberDOG server is ready" 60 UberDOG $UPID
echo "uberdog up (pid $UPID)"

# 3. AI district (same args as ref/open-toontown/darwin/start-ai-server.sh)
DISTRICT="${DISTRICT_NAME:-Toon Valley}"
# Optional test overrides (PRC): AI_MINIGAME_ID=8 makes every trolley ride the Catching game
# (MinigameCreatorAI reads minigame-id; ToontownGlobals *GameId values).
AI_PRC=(etc/Configrc.prc)
if [ -n "${AI_MINIGAME_ID:-}" ]; then
    printf 'minigame-id %s\n' "$AI_MINIGAME_ID" > "$RUN/ai-test.prc"
    AI_PRC+=("$RUN/ai-test.prc")
    echo "ai: forcing minigame-id $AI_MINIGAME_ID"
fi
"$PY" -m toontown.ai.AIStart --base-channel 401000000 --max-channels 999999 \
    --stateserver 4002 --messagedirector-ip 127.0.0.1:7199 --eventlogger-ip 127.0.0.1:7197 \
    --district-name "$DISTRICT" "${AI_PRC[@]}" \
    > "$LOGS/ai-$STAMP.log" 2>&1 < /dev/null &
AIPID=$!; echo "ai $AIPID" >> "$RUN/pids"
ln -sfn "ai-$STAMP.log" "$LOGS/ai.log"
wait_for "$LOGS/ai-$STAMP.log" "ToontownAIRepository: Done\." 180 AI $AIPID
echo "ai up (pid $AIPID), district \"$DISTRICT\""
echo "Client agent: 0.0.0.0:7198   logs: $LOGS/{astrond,uberdog,ai}.log"
