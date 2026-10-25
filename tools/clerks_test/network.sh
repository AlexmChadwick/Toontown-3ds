#!/bin/bash
set -euo pipefail
OUT=sdmc-out/clerks-test
REF="$PWD/ref/ref"; [ -d "$REF" ] || REF="$PWD/ref"
export TT_ETC="$REF/open-toontown/etc"
PORT=7698
SERVER=''
trap 'if [ -n "$SERVER" ]; then kill "$SERVER" 2>/dev/null || true; fi' EXIT
for MODE in stock offers; do
    .venv/bin/python -I -B tools/nettest/fake_server.py --port "$PORT" --clerks "$MODE" > "$OUT/server-$MODE.log" 2>&1 &
    SERVER=$!
    for _ in {1..80}; do
        if rg -q listening "$OUT/server-$MODE.log"; then break; fi
        sleep .1
    done
    "$OUT/online" "$PORT" "$MODE" > "$OUT/client-$MODE.log" 2>&1
    kill "$SERVER"
    wait "$SERVER" 2>/dev/null || true
    SERVER=''
    if rg 'EJECT|Traceback' "$OUT/server-$MODE.log"; then exit 1; fi
    tail -1 "$OUT/client-$MODE.log"
done
