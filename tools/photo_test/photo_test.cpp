#include <cstdio>
#include "game/photo_logic.h"

int main() {
    // Test 1: Solo game, 3 photos matching assignments
    tt::photogame::Simulation g1;
    g1.init(1, 3);
    g1.submitPhoto(0, 0, 0, 4.0f);
    g1.submitPhoto(0, 1, 0, 5.0f);
    g1.submitPhoto(0, 2, 0, 3.0f);
    g1.calculateScores();
    std::printf("solo: score=%d\n", g1.scores[0]);

    // Test 2: Multiplayer 2 players competing on 5 assignments
    tt::photogame::Simulation g2;
    g2.init(2, 5);
    g2.submitPhoto(0, 0, 0, 5.0f);
    g2.submitPhoto(0, 1, 0, 3.0f);
    g2.submitPhoto(0, 2, 0, 4.0f);

    g2.submitPhoto(1, 1, 0, 4.0f);
    g2.submitPhoto(1, 3, 0, 5.0f);
    g2.submitPhoto(1, 4, 0, 2.0f);
    g2.calculateScores();
    std::printf("multi: p1=%d p2=%d\n", g2.scores[0], g2.scores[1]);

    return 0;
}
