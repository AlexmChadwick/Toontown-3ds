#include <cstdio>
#include <vector>
#include "game/target_logic.h"

int main() {
    uint32_t seeds[] = {5, 1234, 4000};
    for (uint32_t seed : seeds) {
        tt::targetgame::TargetRing targets[64];
        int count = tt::targetgame::setupTargets(seed, 2000, targets, 64);
        std::printf("seed=%u targets=%d\n", seed, count);
        for (int i = 0; i < (count < 5 ? count : 5); i++) {
            std::printf("  t[%d]: x=%.2f y=%.2f r=%.2f v=%d\n",
                        i, targets[i].x, targets[i].y, targets[i].radius, targets[i].value);
        }
        float testCoords[][2] = {
            {0.0f, 500.0f},
            {50.0f, 600.0f},
            {100.0f, 300.0f},
            {-50.0f, 700.0f}
        };
        for (const auto& coord : testCoords) {
            int s = tt::targetgame::evaluateScore(targets, count, coord[0], coord[1]);
            std::printf("  score(%.0f,%.0f)=%d\n", coord[0], coord[1], s);
        }
    }
    return 0;
}
