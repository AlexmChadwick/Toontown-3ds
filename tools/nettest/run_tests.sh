#!/bin/bash
# Host-side tests for source/net:
#   1. unit: datagram reader/writer, descriptor tables vs. Panda-packed vectors
#   2. client vs. fake server: full login -> zone flow, remote toon movement
#   3. two clients see each other
#   4. reconnect after the server drops the connection
# Usage: tools/nettest/run_tests.sh            (logs in tools/nettest/out/)
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
PY="${PY:-$ROOT/.venv/bin/python}"
[ -x "$PY" ] || PY="$ROOT/tools/server/.venv/bin/python"
OUT="$HERE/out"
PORT="${PORT:-7298}"
# The main checkout has ref/open-toontown; agent worktrees link the main checkout's ref/ as ref/ref.
TT_ETC="${TT_ETC:-$ROOT/ref/open-toontown/etc}"
[ -f "$TT_ETC/otp.dc" ] || TT_ETC="$ROOT/ref/ref/open-toontown/etc"
export TT_ETC
mkdir -p "$OUT"
"$HERE/build.sh" || exit 1
"$PY" -I -B "$ROOT/tools/dcgen/dcgen.py" "$TT_ETC" --vectors "$HERE/dc_vectors.bin" 2>/dev/null

fail=0
step() { echo "== $*"; }

step unit
"$OUT/nettest" unit "$HERE/dc_vectors.bin" > "$OUT/unit.log" 2>&1 || fail=1
tail -3 "$OUT/unit.log"

start_server() {
    "$PY" -I -B "$HERE/fake_server.py" --port "$PORT" "$@" > "$OUT/server.log" 2>&1 &
    SERVER=$!
    for _ in $(seq 50); do grep -q listening "$OUT/server.log" 2>/dev/null && return; sleep 0.1; done
    echo "fake server did not start"; cat "$OUT/server.log"; exit 1
}
stop_server() { kill "$SERVER" 2>/dev/null; wait "$SERVER" 2>/dev/null; }

step "single client"
start_server
"$OUT/nettest" client --port "$PORT" --token alice --seconds 25 --expect-zone > "$OUT/client1.log" 2>&1 || fail=1
stop_server
grep -E "ENTERED ZONE|SPAWN|SUIT|summary|client test" "$OUT/client1.log"
grep -q "test: SUIT spawn 200000021 \"f\" dept c lvlDist 1 hp 12 sp 200000020 path 11->30 len 5..300 leg 3 state" "$OUT/client1.log" || { echo "FAIL: cog did not generate as expected"; fail=1; }
grep -q "test: SUIT update 200000021 .* state 3" "$OUT/client1.log" || { echo "FAIL: cog setPathState update missing"; fail=1; }
grep -E "gone:" "$OUT/server.log"
grep -q "heartbeats=[2-9]" "$OUT/server.log" || { echo "FAIL: expected >= 2 heartbeats in 25 s"; fail=1; }
grep -q "EJECT" "$OUT/server.log" && { echo "FAIL: client was ejected"; fail=1; }

step "two clients"
start_server
"$OUT/nettest" client --port "$PORT" --token bob --seconds 12 --expect-zone > "$OUT/client2a.log" 2>&1 &
A=$!
sleep 3
"$OUT/nettest" client --port "$PORT" --token carol --seconds 6 --expect-zone > "$OUT/client2b.log" 2>&1 || fail=1
wait $A || fail=1
stop_server
grep -E "SPAWN|DESPAWN|client test" "$OUT/client2a.log" "$OUT/client2b.log"
grep -q 'SPAWN 1000001[0-9][0-9] "Toon"' "$OUT/client2a.log" || { echo "FAIL: bob did not see carol"; fail=1; }
grep -q 'DESPAWN 1000001' "$OUT/client2a.log" || { echo "FAIL: bob did not see carol leave"; fail=1; }

step reconnect
start_server --drop-after 4
"$OUT/nettest" client --port "$PORT" --token dave --seconds 12 --expect-zone --expect-reconnect > "$OUT/client3.log" 2>&1 || fail=1
stop_server
grep -E "error in|retry in|ENTERED ZONE|LEFT ZONE|client test" "$OUT/client3.log"

step "reconnect from a dynamic zone"
start_server --drop-after 4
"$OUT/nettest" client --port "$PORT" --token erin --seconds 12 --dynamic-hop --expect-reconnect > "$OUT/client4.log" 2>&1 || fail=1
stop_server
grep -E "changing zone|dynamic|RELOCATED|ENTERED ZONE|client test" "$OUT/client4.log"

step "platform badges (two clients, MAC and N3DS)"
start_server
"$OUT/nettest" client --port "$PORT" --token frank --seconds 12 --platform mac --expect-badge "Toon=N3DS" > "$OUT/client5a.log" 2>&1 &
A=$!
sleep 3
"$OUT/nettest" client --port "$PORT" --token gina --seconds 6 --expect-badge "Toon=MAC" > "$OUT/client5b.log" 2>&1 || fail=1
wait $A || fail=1
stop_server
grep -E "BADGE|platform:|client test" "$OUT/client5a.log" "$OUT/client5b.log"
grep -q "PLATFORM set .* = 2" "$OUT/server.log" || { echo "FAIL: MAC client did not send setMyPlatform(2)"; fail=1; }
grep -q "PLATFORM set .* = 7" "$OUT/server.log" || { echo "FAIL: 3DS client did not send setMyPlatform(7)"; fail=1; }
grep -q "BADGE .*\"Walker\"\|BADGE .*\"Stander\"" "$OUT/client5a.log" "$OUT/client5b.log" && { echo "FAIL: badge on a toon with no platform"; fail=1; }
grep -q "EJECT" "$OUT/server.log" && { echo "FAIL: client was ejected"; fail=1; }

step "platform badges: same platform shows no badge"
start_server
"$OUT/nettest" client --port "$PORT" --token hal --seconds 10 > "$OUT/client6a.log" 2>&1 &
A=$!
sleep 3
"$OUT/nettest" client --port "$PORT" --token ida --seconds 6 > "$OUT/client6b.log" 2>&1 || fail=1
wait $A || fail=1
stop_server
grep -q "BADGE" "$OUT/client6a.log" "$OUT/client6b.log" && { echo "FAIL: N3DS toon got a badge on N3DS"; fail=1; }
grep -c "PLATFORM request" "$OUT/server.log" | sed 's/^/requests: /'

step "platform badges: server without the manager (stock astrond.yml)"
start_server --no-platform
"$OUT/nettest" client --port "$PORT" --token jo --seconds 14 --expect-zone --expect-reconnect > "$OUT/client7.log" 2>&1 || fail=1
stop_server
grep -E "ejected|badges off|ENTERED ZONE|client test" "$OUT/client7.log"
[ "$(grep -c "EJECT 117" "$OUT/server.log")" = 1 ] || { echo "FAIL: expected exactly one 117 eject"; fail=1; }
grep -q "badges off for this run" "$OUT/client7.log" || { echo "FAIL: client did not turn badges off"; fail=1; }

if [ $fail -eq 0 ]; then echo "ALL NET TESTS PASSED"; else echo "NET TESTS FAILED"; fi
exit $fail
