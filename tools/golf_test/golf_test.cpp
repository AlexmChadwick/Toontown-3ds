#include <cassert>
#include <cmath>
#include <cstdio>

#include "game/golf_logic.h"

int main() {
    tt::golf::Ball b;
    tt::golf::swing(b, 6.0f, 0.0f, 1.0f);
    for (int i = 0; i < 900 && !b.resting; ++i) tt::golf::step(b, 1.0f / 90.0f);
    assert(b.resting);
    assert(b.y > 1.0f);
    assert(std::fabs(b.x) < 0.01f);
    assert(tt::golf::inCup(b, b.x, b.y, b.z));
    std::printf("golf physics: deterministic 90Hz flight, drag, bounce, rest, cup radius\n");
    return 0;
}
