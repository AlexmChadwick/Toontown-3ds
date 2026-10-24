#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
mkdir -p sdmc-out/boss-test
c++ -std=c++17 -fno-exceptions -fno-rtti -Wall -Wextra -Werror -g -O1 \
    -fsanitize=address,undefined -fno-omit-frame-pointer -Isource \
    tools/boss_test/main.cpp source/world/boss_logic.cpp source/net/boss_state.cpp \
    source/net/datagram.cpp -o sdmc-out/boss-test/boss_test
sdmc-out/boss-test/boss_test
