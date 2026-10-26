#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "game/golf_logic.h"

int main(int argc, char** argv) {
    if (argc == 12 && std::strcmp(argv[1], "--record") == 0) {
        tt::golf::Physics physics;
        if (!physics.load(argv[2])) return 2;
        tt::golf::Ball start;
        start.x = std::strtof(argv[3], nullptr);
        start.y = std::strtof(argv[4], nullptr);
        start.z = std::strtof(argv[5], nullptr);
        const float power = std::strtof(argv[6], nullptr);
        const float dirX = std::strtof(argv[7], nullptr);
        const float dirY = std::strtof(argv[8], nullptr);
        const float cupX = std::strtof(argv[9], nullptr);
        const float cupY = std::strtof(argv[10], nullptr);
        const float cupZ = std::strtof(argv[11], nullptr);
        tt::golf::Movie movie;
        // The ODE reference retains its pre-processRecording samples for
        // integration parity.  Runtime callers use recordSwing(), which then
        // applies the same movie reduction before sending to the AI.
        tt::golf::recordRaw(movie, physics, start, power, dirX, dirY, cupX, cupY, cupZ);
        std::printf("inCup %u\n", movie.ballInFrame ? 1U : 0U);
        for (uint16_t i = 0; i < movie.count; ++i) {
            const tt::golf::Ball& ball = movie.frames[i];
            std::printf("%u %.9g %.9g %.9g\n", movie.frameIds[i], ball.x, ball.y, ball.z);
        }
        return 0;
    }
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
    assert(movie.count >= 2 && movie.count <= tt::golf::kMaxMovieFrames);
    std::printf("golf physics: deterministic 90Hz flight, GPH triangle contact, movie recording\n");
    return 0;
}
