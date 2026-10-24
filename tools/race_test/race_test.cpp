#include <cstdio>
#include "game/race_logic.h"

int main() {
    // Scenario 1: Solo run to finish
    tt::racegame::Simulation g1;
    g1.init(1);
    g1.setChance(0, 7, 23); // chance at pos 7 with 4 jellybeans
    int choices1[4] = {4, 3, 4, 3};
    for (int c : choices1) {
        int ch[4] = {c, 0, 0, 0};
        int rewards[4];
        bool won = g1.processChoices(ch, rewards);
        std::printf("solo turn(c=%d): pos=%d won=%s\n", c, g1.positions[0], won ? "True" : "False");
    }
    std::printf("solo final score: %d\n", g1.scores[0]);

    // Scenario 2: Two players, conflicting choices cancel out
    tt::racegame::Simulation g2;
    g2.init(2);
    int ch1[4] = {3, 3, 0, 0};
    int r1[4];
    g2.processChoices(ch1, r1);
    std::printf("p2 turn 1 (conflict): p1=%d p2=%d\n", g2.positions[0], g2.positions[1]);

    int ch2[4] = {4, 2, 0, 0};
    int r2[4];
    g2.processChoices(ch2, r2);
    std::printf("p2 turn 2 (unique): p1=%d p2=%d\n", g2.positions[0], g2.positions[1]);

    return 0;
}
