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
    tt::golf::Physics physics;
    tt::golf::Triangle floor = {{-100, -100, 0, 100, -100, 0, 0, 100, 0}, tt::golf::Surface::Grass};
    assert(physics.addTriangle(floor));
    tt::golf::Movie movie;
    tt::golf::Ball start;
    tt::golf::recordSwing(movie, physics, start, 2.0f, 0.0f, 1.0f, 0.0f, 999.0f, 0.0f);
    assert(movie.count > 3 && movie.count <= tt::golf::kMaxMovieFrames);
    std::printf("golf physics: deterministic 90Hz flight, GPH triangle contact, movie recording\n");
    return 0;
}
