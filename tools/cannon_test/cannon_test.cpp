#include <cstdio>
#include "game/cannon_logic.h"

using namespace tt::game;
using namespace tt::game::cannongame;

int main() {
    struct TestCase {
        uint32_t doId;
        float diff;
    } cases[] = {
        {1001, 0.0f},
        {2002, 0.5f},
        {3003, 1.0f},
        {4004, 0.25f},
        {5005, 0.75f},
    };

    const float zRots[] = {-20.0f, -10.0f, 0.0f, 10.0f, 20.0f};
    const float angles[] = {15.0f, 30.0f, 45.0f, 60.0f, 75.0f, 85.0f};

    for (const auto& tc : cases) {
        RandomNumGen rng(tc.doId);
        Vec3 tower = getTowerPosition(rng, tc.diff);
        printf("tower doId=%u diff=%.2f pos=(%.4f, %.4f, %.4f)\n",
               tc.doId, tc.diff, tower.x, tower.y, tower.z);

        const float cannon_x = 0.0f;
        for (float z_rot : zRots) {
            for (float angle : angles) {
                FlightResult res = calcFlight(cannon_x, z_rot, angle, tower);
                printf("shot rot=%.1f ang=%.1f hit=%d t=%.4f score=%d\n",
                       z_rot, angle, static_cast<int>(res.hitWhat), res.timeOfImpact, res.score);
            }
        }
    }
    return 0;
}
