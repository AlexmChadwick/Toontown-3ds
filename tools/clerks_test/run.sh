#!/bin/bash
# Run from the clerks worktree. No AI or emulator is launched.
set -euo pipefail
OUT=sdmc-out/clerks-test
mkdir -p "$OUT"
.venv/bin/python -I -B tools/clerks_test/reference.py
clang++ -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -Isource tools/clerks_test/rules.cpp source/game/clerk_logic.cpp -o "$OUT/rules"
"$OUT/rules"
clang++ -std=c++17 -O1 -g -Wall -Wextra -Werror -fno-rtti -fno-exceptions -fsanitize=address,undefined -fno-omit-frame-pointer -Isource source/net/*.cpp source/game/*_logic.cpp source/game/name_tables.cpp source/world/toon_dna.cpp source/world/suit_path.cpp source/world/level_spec.cpp tools/nettest/log_host.cpp tools/clerks_test/online.cpp -lpthread -o "$OUT/online"
clang++ -std=c++17 -O1 -g -Wall -Wextra -Werror -fno-rtti -fno-exceptions -fsanitize=address,undefined -fno-omit-frame-pointer -Isource source/net/*.cpp source/game/*_logic.cpp source/game/name_tables.cpp source/world/toon_dna.cpp source/world/suit_path.cpp source/world/level_spec.cpp tools/nettest/log_host.cpp tools/clerks_test/client.cpp -lpthread -o "$OUT/client"
"$OUT/client"
export TT_ETC="$PWD/ref/ref/open-toontown/etc"
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
