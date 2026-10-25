#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
mkdir -p sdmc-out/cogdo-test
.venv/bin/python -I -B tools/cogdo_test/reference.py
c++ -std=c++17 -fno-exceptions -fno-rtti -Wall -Wextra -Werror -g -O1 \
    -fsanitize=address,undefined -fno-omit-frame-pointer -Isource \
    tools/cogdo_test/parser.cpp source/net/cogdo_state.cpp source/net/datagram.cpp -o sdmc-out/cogdo-test/parser
sdmc-out/cogdo-test/parser
c++ -std=c++17 -fno-exceptions -fno-rtti -Wall -Wextra -Werror -g -O1 \
    -fsanitize=address,undefined -fno-omit-frame-pointer -Isource \
    tools/cogdo_test/maze.cpp source/world/cogdo_maze.cpp source/world/cogdo_crane.cpp source/game/catch_logic.cpp \
    -o sdmc-out/cogdo-test/maze
sdmc-out/cogdo-test/maze
c++ -std=c++17 -fno-exceptions -fno-rtti -Wall -Wextra -Werror -g -O1 \
    -fsanitize=address,undefined -fno-omit-frame-pointer -Isource \
    tools/cogdo_test/client.cpp source/net/*.cpp source/game/*_logic.cpp \
    source/world/toon_dna.cpp source/world/suit_path.cpp source/world/level_spec.cpp \
    tools/nettest/log_host.cpp -lpthread -o sdmc-out/cogdo-test/client
sdmc-out/cogdo-test/client
.venv/bin/python -I -B tools/cogdo_test/server.py

c++ -std=c++17 -fno-exceptions -fno-rtti -Wall -Wextra -Werror -g -O1 \
    -fsanitize=address,undefined -fno-omit-frame-pointer -Isource \
    tools/cogdo_test/collision.cpp source/world/collision.cpp tools/collision_test/log_stub.cpp \
    -o sdmc-out/cogdo-test/collision
sdmc-out/cogdo-test/collision
.venv/bin/python -I -B tools/cogdo_test/assets.py
