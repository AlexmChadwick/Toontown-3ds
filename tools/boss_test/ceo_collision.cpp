// Verify actual baked floor alignment at stock CEO gameplay positions.
#include <cassert>
#include <cmath>
#include <cstdio>

#include "world/collision.h"

int main(int argc, char** argv) {
    assert(argc == 2);
    tt::world::CollisionWorld collision;
    assert(collision.load(argv[1]));
    // BossbotHQBossBattle.teleportInPosHpr (-1.40, 59.78),
    // WaiterBattle[AB] (20, -45) and (-20, -45),
    // DinerBattle[AB] (20, 115) and (-20, 115),
    // Golf spot origins (-45, 100), (-15, 100), (15, 100), (45, 100),
    // CEO start position (0, 355),
    // Center arena (0, 0).
    const float positions[][2] = {
        {-1.40f, 59.78f},
        {20.0f, -45.0f},
        {-20.0f, -45.0f},
        {20.0f, 115.0f},
        {-20.0f, 115.0f},
        {-45.0f, 100.0f},
        {-15.0f, 100.0f},
        {15.0f, 100.0f},
        {45.0f, 100.0f},
        {0.0f, 355.0f},
        {0.0f, 0.0f},
    };
    for (const auto& position : positions) {
        bool found = false;
        const float height = collision.groundHeight(position[0], position[1], 10, &found);
        std::printf("CEO floor (%.2f, %.2f): found %d height %.4f\n", position[0], position[1], found, height);
        assert(found && std::fabs(height) < .25f);
    }
    std::printf("CEO floor alignment: PASS; collision heap %u bytes\n", unsigned(collision.heapBytes()));
}
