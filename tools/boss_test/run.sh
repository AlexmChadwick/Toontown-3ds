#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
mkdir -p sdmc-out/boss-test
.venv/bin/python -I -B tools/boss_test/pack_reference.py
c++ -std=c++17 -fno-exceptions -fno-rtti -Wall -Wextra -Werror -g -O1 \
    -fsanitize=address,undefined -fno-omit-frame-pointer -Isource \
    tools/boss_test/main.cpp source/world/boss_logic.cpp source/world/cfo_logic.cpp source/net/boss_state.cpp source/net/cfo_state.cpp \
    source/net/datagram.cpp -o sdmc-out/boss-test/boss_test
sdmc-out/boss-test/boss_test sdmc-out/boss-test/panda_vectors.bin
c++ -std=c++17 -fno-exceptions -fno-rtti -Wall -Wextra -Werror -g -O1 \
    -fsanitize=address,undefined -fno-omit-frame-pointer -Isource \
    tools/boss_test/client_test.cpp source/net/*.cpp source/game/*_logic.cpp \
    source/world/toon_dna.cpp source/world/suit_path.cpp source/world/level_spec.cpp \
    tools/nettest/log_host.cpp -lpthread -o sdmc-out/boss-test/client_test
sdmc-out/boss-test/client_test

# Asset-backed check is available after the CFO bake, without requiring goon rebakes.
if [ -f sdmc-out/3ds/toontown3ds/data/boss/cfo_arena.ttc ]; then
    c++ -std=c++17 -fno-exceptions -fno-rtti -Wall -Wextra -Werror -g -O1 \
        -fsanitize=address,undefined -fno-omit-frame-pointer -Isource \
        tools/boss_test/cfo_collision.cpp source/world/collision.cpp \
        tools/collision_test/log_stub.cpp -o sdmc-out/boss-test/cfo_collision
    sdmc-out/boss-test/cfo_collision sdmc-out/3ds/toontown3ds/data/boss/cfo_arena.ttc
fi
