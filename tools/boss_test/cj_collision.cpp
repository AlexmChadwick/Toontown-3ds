// The decoded court floor must agree with the stock boss and cannon anchors.
#include <cassert>
#include <cmath>
#include <cstdio>
#include "world/collision.h"
int main(int argc, char** argv) {
    assert(argc == 2);
    tt::world::CollisionWorld collision;
    assert(collision.load(argv[1]));
    const float positions[][3] = {
        {-2.84f, -99.47f, 0}, {-2.798f, -85, 0}, {-2.798f, -35, 0},
        {-80, 9.625f, 0}, {-3, 0, 0}, {54, 97, 0}, {20, 10, 0}
    };
    for (const auto& point : positions) {
        bool found = false;
        const float height = collision.groundHeight(point[0], point[1], point[2] + 5, &found);
        std::printf("CJ floor (%.3f, %.3f): found %d height %.4f\n", point[0], point[1], found, height);
        assert(found && std::fabs(height - point[2]) < .25f);
    }
    std::printf("CJ floor alignment: PASS; collision heap %u bytes\n", unsigned(collision.heapBytes()));
}
