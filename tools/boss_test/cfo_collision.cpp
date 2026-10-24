// Verify actual baked floor alignment at stock CFO gameplay positions.
#include <cassert>
#include <cmath>
#include <cstdio>

#include "world/collision.h"

int main(int argc, char** argv) {
    assert(argc == 2);
    tt::world::CollisionWorld collision;
    assert(collision.load(argv[1]));
    // CashbotHQBossBattle.teleportInPosHpr, CashbotBossBattleOnePosHpr,
    // CashbotBossBattleThreePosHpr and CashbotToonsBattleThreeStartPosHpr.
    const float positions[][2] = {
        {88, -214}, {120, -230}, {145, -230}, {95, -230}, {120, -315},
        {105, -285}, {136, -342}, {105, -342}, {135, -292},
        {93, -303}, {144, -327}, {145, -302}, {93, -327}
    };
    for (const auto& position : positions) {
        bool found = false;
        const float height = collision.groundHeight(position[0], position[1], 10, &found);
        std::printf("CFO floor (%.1f, %.1f): found %d height %.4f\n", position[0], position[1], found, height);
        assert(found && std::fabs(height) < .25f);
    }
    std::printf("CFO floor alignment: PASS; collision heap %u bytes\n", unsigned(collision.heapBytes()));
}
