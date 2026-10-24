#include "game/diving_logic.h"
#include <cstdio>
#include <cmath>

int main() {
    using namespace tt::game::diving;

    const float dt = 0.033f;
    SwimmerState s;
    s.x = 0.0f;
    s.z = 34.0f;
    int recoveries = 0;

    uint32_t seed = 42;
    auto rng = [&]() -> uint32_t {
        seed = (seed * 1103515245 + 12345) & 0x7fffffff;
        return seed;
    };

    for (int frame = 0; frame < 150; ++frame) {
        float inputX = ((int(rng() % 200)) - 100) / 100.0f;
        float inputZ = ((int(rng() % 200)) - 100) / 100.0f;

        updateSwimmer(s, inputX, inputZ, dt);

        // Test pickup at bottom
        if (!s.hasTreasure && s.z <= kSeabedZ + 1.0f && std::abs(s.x) < 5.0f && s.freezeTimer == 0.0f) {
            s.hasTreasure = true;
        }

        // Test obstacle hit at frame 80
        if (frame == 80) {
            applyObstacleHit(s);
        }

        // Test boat recovery
        if (checkBoatRecovery(s)) {
            recoveries++;
        }

        std::printf("dive frame %03d: pos=(%6.2f, %6.2f) vel=(%6.2f, %6.2f) treas=%d rec=%d\n",
                    frame, s.x, s.z, s.xVel, s.zVel, int(s.hasTreasure), recoveries);
    }

    const int test_recs[6] = {0, 1, 2, 3, 5, 8};
    for (int i = 0; i < 6; ++i) {
        int sc = calculateScore(test_recs[i]);
        std::printf("score: rec=%d -> %d\n", test_recs[i], sc);
    }

    return 0;
}
