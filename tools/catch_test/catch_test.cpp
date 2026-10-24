// Prints the same text as tools/catch_test/catch_ref.py for the same cases, from the C++ port
// in source/game/catch_logic.*. run_tests.sh diffs the two.
#include <cstdio>

#include "game/catch_logic.h"

using namespace tt::game::catchgame;

struct Case {
    uint32_t doId, hood;
    int players;
    double difficulty;
};

int main() {
    const Case cases[] = {
        {100000001, 2000, 1, 0.0}, {100000417, 2000, 1, 0.0}, {555555555, 1000, 1, 0.2},
        {123456789, 5000, 2, 0.4}, {987654321, 4000, 3, 0.6}, {424242424, 3000, 4, 0.8},
        {31337, 9000, 1, 1.0},     {2147483000u, 2000, 2, 0.0},
    };
    static Schedule s;
    for (const Case& c : cases) {
        s.init(c.doId, c.hood, c.players, c.difficulty);
        printf("case doId=%u hood=%u players=%d diff=%.6f\n", c.doId, c.hood, c.players, c.difficulty);
        printf("const toonSpeed=%.6f halfW=%.6f halfH=%.6f onscreen=%.6f offscreen=%.6f period=%.6f drops=%d "
               "fruits=%d anvils=%d\n",
               s.toonSpeed, s.stageHalfWidth, s.stageHalfHeight, s.baselineOnscreen, s.offscreenTime,
               s.dropPeriod, s.totalDrops, s.numFruits, s.numAnvils);
        printf("serverFruits=%d\n", s.serverFruits);
        for (int i = 0; i < s.numDrops; ++i) {
            const Drop& d = s.drops[i];
            printf("drop %d t=%.6f %s %d %d\n", i, d.t, d.type == kAnvil ? "anvil" : "fruit", d.gx, d.gy);
        }
        for (int i = 0; i < s.numDrops; ++i) printf("spawnrand %d %.9f\n", i, s.drops[i].spin);
        for (int i = 0; i < s.numSuitWalks; ++i) {
            const SuitWalk& w = s.suitWalks[i];
            printf("suit %d delay=%.6f from=%.6f,%.6f to=%.6f,%.6f dur=%.6f kind=%d\n", i, w.delay, w.sx, w.sy, w.ex,
                   w.ey, w.duration, int(w.kind));
        }
    }
    // Score rule spot checks (DistributedCatchGameAI.gameOver; see catch_ref.py for the Python).
    printf("score 0/18=%d 5/18=%d 17/18=%d 18/18=%d 28/28=%d 19/19=%d\n", expectedScore(0, 18), expectedScore(5, 18),
           expectedScore(17, 18), expectedScore(18, 18), expectedScore(28, 28), expectedScore(19, 19));
    return 0;
}
