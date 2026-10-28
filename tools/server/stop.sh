#!/bin/bash
# Stop the processes started by tools/server/start.sh (AI, UberDOG, astrond).
HERE="$(cd "$(dirname "$0")" && pwd)"
PIDS="${TT_SERVER_RUN:-$HERE/run}/pids"
[ -f "$PIDS" ] || { echo "Not running (no run/pids)"; exit 0; }
# Reverse order: AI, UD, then astrond (tail -r is BSD-only; tac is GNU-only).
if command -v tac >/dev/null; then REV=(tac); else REV=(tail -r); fi
"${REV[@]}" "$PIDS" | while read -r name pid; do
    if kill -0 "$pid" 2>/dev/null; then
        kill "$pid" 2>/dev/null
        for _ in $(seq 25); do kill -0 "$pid" 2>/dev/null || break; sleep 0.2; done
        kill -9 "$pid" 2>/dev/null && echo "killed -9 $name ($pid)" || echo "stopped $name ($pid)"
    fi
done
rm -f "$PIDS"
