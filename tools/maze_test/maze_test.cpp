#include "game/maze_logic.h"
#include <cstdio>

int main() {
    using namespace tt::game::maze;

    float x = 0.0f, y = 0.0f;
    uint32_t seed = 12345;
    auto rng = [&]() -> uint32_t {
        seed = (seed * 1103515245 + 12345) & 0x7fffffff;
        return seed;
    };

    for (int step = 0; step < 100; ++step) {
        float dx = ((int(rng() % 200)) - 100) / 50.0f;
        float dy = ((int(rng() % 200)) - 100) / 50.0f;
        float nx = x + dx;
        float ny = y + dy;
        float outX, outY;
        doOrthoCollisions(x, y, nx, ny, outX, outY);
        std::printf("collide %02d: old=(%6.2f, %6.2f) new=(%6.2f, %6.2f) -> out=(%6.2f, %6.2f)\n",
                    step, x, y, nx, ny, outX, outY);
        x = outX;
        y = outY;
    }

    const float test_points[10][2] = {
        {-24.0f, -18.0f}, {-20.0f, -16.0f}, {-14.0f, 0.0f}, {0.0f, 0.0f}, {14.0f, 10.0f},
        {20.0f, 16.0f}, {24.0f, 18.0f}, {-1.5f, 2.5f}, {3.7f, -4.2f}, {12.1f, -17.9f}
    };
    for (int i = 0; i < 10; ++i) {
        int tx, ty;
        worldToTile(test_points[i][0], test_points[i][1], tx, ty);
        std::printf("tile: (%6.2f, %6.2f) -> (%2d, %2d)\n", test_points[i][0], test_points[i][1], tx, ty);
    }

    const int scores[9][2] = {
        {0, 0}, {5, 0}, {11, 0}, {12, 0}, {23, 0}, {24, 0}, {100, 0}, {197, 0}, {197, 1}
    };
    for (int i = 0; i < 9; ++i) {
        int s = calculateScore(scores[i][0], scores[i][1] != 0);
        std::printf("score: taken=%3d all=%d -> %2d\n", scores[i][0], scores[i][1], s);
    }

    return 0;
}
