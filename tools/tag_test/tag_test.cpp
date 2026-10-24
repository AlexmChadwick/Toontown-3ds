#include <cstdio>
#include "game/tag_logic.h"

int main() {
    tt::taggame::Simulation sim;
    sim.init(1001);
    std::printf("init: it=%u\n", sim.itAvId);

    bool t1 = sim.attemptTag(1001, 1002);
    std::printf("tag(1001->1002): ok=%s it=%u cd=%.1f\n", t1 ? "True" : "False", sim.itAvId, sim.tagBackTimer);

    bool t2 = sim.attemptTag(1002, 1001);
    std::printf("tag(1002->1001 immediate): ok=%s it=%u\n", t2 ? "True" : "False", sim.itAvId);

    sim.step(1.0f);
    bool t3 = sim.attemptTag(1002, 1001);
    std::printf("tag(1002->1001 after 1s): ok=%s it=%u\n", t3 ? "True" : "False", sim.itAvId);

    sim.step(1.5f);
    bool t4 = sim.attemptTag(1002, 1001);
    std::printf("tag(1002->1001 after 2.5s): ok=%s it=%u\n", t4 ? "True" : "False", sim.itAvId);

    sim.grabTreasure(0);
    sim.grabTreasure(1);
    std::printf("treasures: score=%d\n", sim.localScore);

    return 0;
}
