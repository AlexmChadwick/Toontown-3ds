#!/bin/bash
set -euo pipefail
OUT=sdmc-out/racing2
mkdir -p "$OUT"
FLAGS=(-std=c++17 -O1 -g -Wall -Wextra -Werror -fno-exceptions -fno-rtti -fsanitize=address,undefined -fno-omit-frame-pointer -Isource)
clang++ "${FLAGS[@]}" tools/racing_test/physics.cpp source/world/racing_logic.cpp -o "$OUT/physics"
clang++ "${FLAGS[@]}" tools/racing_test/progress.cpp source/world/racing_logic.cpp -o "$OUT/progress"
"$OUT/physics" > "$OUT/physics-cpp.txt"
"$OUT/progress"
.venv/bin/python -I -B tools/racing_test/reference.py > "$OUT/physics-reference.txt"
.venv/bin/python -I -B tools/racing_test/check.py
.venv/bin/python -I -B tools/racing_test/vectors.py
SOURCES=(source/net/*.cpp source/game/*_logic.cpp source/game/name_tables.cpp source/quest/quest_data.cpp source/quest/quest_text.cpp source/world/toon_dna.cpp source/world/suit_path.cpp source/world/level_spec.cpp tools/nettest/log_host.cpp)
clang++ "${FLAGS[@]}" tools/racing_test/client.cpp "${SOURCES[@]}" -lpthread -o "$OUT/client"
"$OUT/client" > "$OUT/client.log"
tail -1 "$OUT/client.log"
.venv/bin/python -I -B tools/racing_test/floors.py > "$OUT/floors.log"
clang++ "${FLAGS[@]}" tools/racing_test/tracks.cpp source/world/racing_track.cpp source/world/racing_logic.cpp source/world/collision.cpp tools/nettest/log_host.cpp -o "$OUT/tracks"
"$OUT/tracks" > "$OUT/tracks.log"
tail -1 "$OUT/tracks.log"
clang++ "${FLAGS[@]}" tools/racing_test/drive.cpp source/world/racing_track.cpp source/world/racing_logic.cpp source/world/collision.cpp tools/nettest/log_host.cpp -o "$OUT/drive"
"$OUT/drive" > "$OUT/drive.log"
tail -3 "$OUT/drive.log"
clang++ "${FLAGS[@]}" tools/racing_test/pies.cpp source/world/racing_logic.cpp -o "$OUT/pies"
"$OUT/pies" > "$OUT/pies-cpp.txt"
.venv/bin/python -I -B tools/racing_test/pie_reference.py > "$OUT/pies-reference.txt"
.venv/bin/python -I -B tools/racing_test/check_pies.py
