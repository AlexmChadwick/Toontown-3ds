#include <cstdio>
#include <cstdint>
#include "game/pattern_logic.h"

int main() {
    uint8_t patterns[4][8] = {
        {0, 1},
        {0, 1, 3, 2},
        {0, 1, 3, 2, 2, 0},
        {0, 1, 3, 2, 2, 0, 1, 3}
    };
    int lens[4] = {2, 4, 6, 8};

    // Test 1: Solo perfect game
    tt::patterngame::Simulation sim1;
    sim1.init();
    for (int r = 0; r < 4; ++r) {
        sim1.setTargetPattern(patterns[r], lens[r]);
        for (int i = 0; i < lens[r]; ++i) {
            sim1.inputButton(patterns[r][i]);
        }
        sim1.finishRound(2.5f, false, false);
    }
    std::printf("solo perfect: score=%d perfect=%d\n", sim1.score, sim1.isPerfect ? 1 : 0);

    // Test 2: Solo imperfect game (missed round 3)
    tt::patterngame::Simulation sim2;
    sim2.init();
    for (int r = 0; r < 4; ++r) {
        sim2.setTargetPattern(patterns[r], lens[r]);
        for (int i = 0; i < lens[r]; ++i) {
            uint8_t btn = patterns[r][i];
            if (r == 2 && i == lens[r] - 1) {
                btn = (btn + 1) % 4;
            }
            sim2.inputButton(btn);
        }
        sim2.finishRound(3.0f, false, false);
    }
    std::printf("solo imperfect: score=%d perfect=%d\n", sim2.score, sim2.isPerfect ? 1 : 0);

    // Test 3: Multiplayer 2 players (p1 faster rounds 1 & 2, p2 faster round 3)
    tt::patterngame::Simulation p1, p2;
    p1.init();
    p2.init();
    for (int r = 0; r < 4; ++r) {
        p1.setTargetPattern(patterns[r], lens[r]);
        p2.setTargetPattern(patterns[r], lens[r]);
        for (int i = 0; i < lens[r]; ++i) {
            p1.inputButton(patterns[r][i]);
            p2.inputButton(patterns[r][i]);
        }
        bool p1Fast = (r < 2);
        bool p2Fast = (r >= 2);
        p1.finishRound(p1Fast ? 2.0f : 3.5f, true, p1Fast);
        p2.finishRound(p2Fast ? 2.5f : 3.0f, true, p2Fast);
    }
    std::printf("multiplayer: p1=%d p2=%d\n", p1.score, p2.score);

    return 0;
}
