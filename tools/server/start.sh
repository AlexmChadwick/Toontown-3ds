#!/bin/bash
# Start the real Open Toontown server stack locally:
#   astrond  ->  UberDOG  ->  AI district
# macOS: the x86_64 prebuilt astrond in ref/ (Rosetta); Linux: the astrond
# built by setup_linux.sh into tools/server/.deps/astron/.
# Client agent listens on 0.0.0.0:7198 (MD on 127.0.0.1:7199, event logger 7197).
# Private instance for parallel agents / perf runs: TT_SERVER_PORT_BASE=N (client N, MD N+1,
# event logger N+2) and TT_SERVER_RUN=dir (its own database, pids, logs; use the same
# variables with stop.sh). Defaults are the stock ports and tools/server/run.
#
# Usage:  tools/server/start.sh          (run setup.sh / setup_linux.sh once first)
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
[ -f "$RES/phase_4/dna/storage.dna" ] || RES="$ROOT/ref/ref/resources"
[ -d "$OTT/toontown" ] || OTT="$ROOT/ref/ref/open-toontown"
RUN="${TT_SERVER_RUN:-$HERE/run}"
LOGS="${TT_SERVER_RUN:+$RUN/logs}"; LOGS="${LOGS:-$HERE/logs}"
if [ -n "${TT_SERVER_PORT_BASE:-}" ]; then
    CPORT=$TT_SERVER_PORT_BASE; MDPORT=$((CPORT + 1)); ELPORT=$((CPORT + 2))
else
    CPORT=7198; MDPORT=7199; ELPORT=7197
fi
# TT_SERVER_TOOLS=<other checkout>/tools/server reuses its built .venv/.deps (skips setup here).
BUILT="${TT_SERVER_TOOLS:-$HERE}"
PY="$BUILT/.venv/bin/python"
OS="$(uname -s)"
SETUP="$HERE/setup.sh"; [ "$OS" = Linux ] && SETUP="$HERE/setup_linux.sh"

[ -x "$PY" ] || { echo "Run $SETUP first"; exit 1; }
"$PY" -I -c "import panda3d.toontown" 2>/dev/null || { echo "Panda3D fork missing; run $SETUP"; exit 1; }
if [ "$OS" = Linux ]; then
    ASTROND="$BUILT/.deps/astron/bin/astrond"
    [ -x "$ASTROND" ] || { echo "astrond missing; run $SETUP"; exit 1; }
fi
[ -d "$RES/phase_3" ] || { echo "Missing $RES (ref/resources submodule)"; exit 1; }

if [ -f "$RUN/pids" ]; then
    alive=0
    while read -r _ pid; do kill -0 "$pid" 2>/dev/null && alive=1; done < "$RUN/pids"
    [ $alive = 1 ] && { echo "Server already running (see $RUN/pids). Run stop.sh first."; exit 1; }
    rm -f "$RUN/pids"
fi
listening() {  # listening <port>: is something listening on TCP <port>?
    if [ "$OS" = Darwin ]; then
        lsof -nP -iTCP:$1 -sTCP:LISTEN >/dev/null 2>&1
    elif command -v ss >/dev/null; then
        [ -n "$(ss -Htln "sport = :$1" 2>/dev/null)" ]
    else  # /proc/net/tcp{,6}: local address field ends in :PORT (hex), state 0A = LISTEN
        local hex; hex=$(printf '%04X' "$1")
        awk -v p=":$hex" '$4 == "0A" && substr($2, length($2) - 4) == p { f = 1 } END { exit !f }' \
            /proc/net/tcp /proc/net/tcp6 2>/dev/null
    fi
}
for port in $CPORT $MDPORT $ELPORT; do
    if listening $port; then
        echo "Port $port already in use"
        if [ "$OS" = Darwin ]; then lsof -nP -iTCP:$port -sTCP:LISTEN; elif command -v ss >/dev/null; then ss -tlnp "sport = :$port"; fi
        exit 1
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
    -e "s#127\.0\.0\.1:7197#127.0.0.1:$ELPORT#" \
    -e "s#127\.0\.0\.1:7199#127.0.0.1:$MDPORT#" \
    -e "s#0\.0\.0\.0:7198#0.0.0.0:$CPORT#" \
    "$OTT/astron/config/astrond.yml" > "$RUN/astrond.yml"
# ASTRON_HEARTBEAT_TIMEOUT_MS: test rigs only. The client heartbeats every 10 s of *emulated*
# time; an emulator running below 1/6 speed (Azahar with software GL in heavy scenes) misses
# the stock 60 s timeout and is ejected with 345. Unset = the stock 60000.
if [ -n "${ASTRON_HEARTBEAT_TIMEOUT_MS:-}" ]; then
    sed -i.bak "s/heartbeat_timeout: 60000/heartbeat_timeout: $ASTRON_HEARTBEAT_TIMEOUT_MS/" "$RUN/astrond.yml"
    rm -f "$RUN/astrond.yml.bak"
    echo "astron: heartbeat timeout $ASTRON_HEARTBEAT_TIMEOUT_MS ms (test override)"
fi

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
if [ "$OS" = Linux ]; then
    # (rpath to tools/server/.deps/env/lib is baked in by setup_linux.sh)
    (cd "$RUN" && exec "$ASTROND" --boring --loglevel info "$RUN/astrond.yml") \
        > "$LOGS/astrond-$STAMP.log" 2>&1 < /dev/null &
else
# (cwd = astron/darwin so the bare dylib install names resolve; DYLD_* vars
#  would be stripped by SIP-protected wrappers like nohup/env.)
(cd "$OTT/astron/darwin" && exec ./astrond --boring --loglevel info "$RUN/astrond.yml") \
    > "$LOGS/astrond-$STAMP.log" 2>&1 < /dev/null &
fi
APID=$!; echo "astrond $APID" >> "$RUN/pids"
ln -sfn "astrond-$STAMP.log" "$LOGS/astrond.log"
i=0; until listening $MDPORT && listening $CPORT; do
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
"$PY" -I "$HERE/run_module.py" toontown.uberdog.UDStart --base-channel 1000000 --max-channels 999999 \
    --stateserver 4002 --messagedirector-ip 127.0.0.1:$MDPORT --eventlogger-ip 127.0.0.1:$ELPORT \
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
AI_ENTRY=("$HERE/run_module.py" toontown.ai.AIStart)
# AI_DEV=1: relax entry checks and top up toons for testing (tools/server/ai_dev.py).
if [ -n "${AI_DEV:-}" ]; then AI_ENTRY=("$HERE/run_module.py" --dev-ai); echo "ai: dev test mode (ai_dev.py)"; fi
"$PY" -I "${AI_ENTRY[@]}" --base-channel 401000000 --max-channels 999999 \
    --stateserver 4002 --messagedirector-ip 127.0.0.1:$MDPORT --eventlogger-ip 127.0.0.1:$ELPORT \
    --district-name "$DISTRICT" "${AI_PRC[@]}" \
    > "$LOGS/ai-$STAMP.log" 2>&1 < /dev/null &
AIPID=$!; echo "ai $AIPID" >> "$RUN/pids"
ln -sfn "ai-$STAMP.log" "$LOGS/ai.log"
wait_for "$LOGS/ai-$STAMP.log" "ToontownAIRepository: Done\." 180 AI $AIPID
echo "ai up (pid $AIPID), district \"$DISTRICT\""
echo "Client agent: 0.0.0.0:$CPORT   logs: $LOGS/{astrond,uberdog,ai}.log"
