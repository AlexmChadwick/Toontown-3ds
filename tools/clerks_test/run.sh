#!/bin/bash
# Run from the clerks worktree. No AI or emulator is launched.
set -euo pipefail
OUT=sdmc-out/clerks-test
mkdir -p "$OUT"
.venv/bin/python -I -B tools/clerks_test/reference.py
FLAGS=(
    -std=c++17 -O1 -g -Wall -Wextra -Werror -fno-rtti -fno-exceptions
    -fsanitize=address,undefined -fno-omit-frame-pointer -Isource
)
SOURCES=(
    source/net/*.cpp source/game/*_logic.cpp source/game/name_tables.cpp
    source/world/toon_dna.cpp source/world/suit_path.cpp source/world/level_spec.cpp
    tools/nettest/log_host.cpp
)
clang++ "${FLAGS[@]}" tools/clerks_test/rules.cpp source/game/clerk_logic.cpp -o "$OUT/rules"
"$OUT/rules"
clang++ "${FLAGS[@]}" "${SOURCES[@]}" tools/clerks_test/online.cpp -lpthread -o "$OUT/online"
clang++ "${FLAGS[@]}" "${SOURCES[@]}" tools/clerks_test/client.cpp -lpthread -o "$OUT/client"
"$OUT/client"
bash tools/clerks_test/network.sh
