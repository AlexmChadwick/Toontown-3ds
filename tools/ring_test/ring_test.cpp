#include <cstdio>
#include "game/ring_logic.h"

using namespace tt::game;
using namespace tt::game::ringgame;

int main() {
    const uint32_t hoods[] = {2000, 1000, 5000, 4000, 3000, 9000};
    const uint32_t seeds[] = {12345, 67890, 54321};

    for (uint32_t hood : hoods) {
        for (uint32_t seed : seeds) {
            RandomNumGen rng(seed);
            int pattern[kNumRingGroups];
            getDifficultyPattern(hood, rng, pattern);
            printf("pattern hood=%u seed=%u pat=[", hood, seed);
            for (int i = 0; i < kNumRingGroups; ++i) {
                printf("%d%s", pattern[i], i + 1 < kNumRingGroups ? "," : "");
            }
            printf("]\n");
        }
    }

    struct TestPoint {
        float tx, tz, rx, rz;
    } points[] = {
        {0.0f, 0.0f, 0.0f, 0.0f},
        {1.0f, 2.0f, 1.0f, 2.0f},
        {0.0f, 0.0f, 2.0f, 2.0f},
        {0.0f, 0.0f, 3.0f, 0.0f},
        {0.0f, 0.0f, 3.075f, 0.0f},
        {0.0f, 0.0f, 3.1f, 0.0f},
        {-2.0f, 2.0f, 0.0f, 0.0f},
        {5.0f, 5.0f, 0.0f, 0.0f},
    };

    for (const auto& p : points) {
        const double dx = double(p.tx) - double(p.rx);
        const double dz = double(p.tz) - double(p.rz);
        const double d2 = dx * dx + dz * dz;
        const int hit = checkCollision(p.tx, p.tz, p.rx, p.rz) ? 1 : 0;
        printf("collide toon=(%.2f,%.2f) ring=(%.2f,%.2f) d2=%.4f hit=%d\n",
               p.tx, p.tz, p.rx, p.rz, d2, hit);
    }

    return 0;
}
