#include "world/collision.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <initializer_list>
using namespace tt::world;
int main() {
    const char *directory = "sdmc-out/3ds/toontown3ds/data/cogdo/";
    for (const char *name : {"maze_frame", "suite", "barrel_room", "battle_room", "prototype_room"}) {
        char path[200];
        std::snprintf(path, sizeof(path), "%s%s.ttc", directory, name);
        CollisionWorld world;
        assert(world.load(path));
        assert(world.heapBytes() <= 1024 * 1024);
        std::printf("Cogdo collision %s: %u triangles %u heap bytes\n", name, world.numTris(),
                    world.heapBytes());
    }
    CollisionWorld maze;
    assert(maze.load("sdmc-out/3ds/toontown3ds/data/cogdo/maze_frame.ttc"));
    for (float y : {-74.0f, -50.0f, 0.0f, 50.0f, 74.0f}) {
        bool found = false;
        const float height = maze.groundHeight(0, y, .1f, &found);
        assert(found && std::isfinite(height) && std::fabs(height) < 1);
    }
    std::puts("Cogdo collision load/budget/maze entrance-to-exit ground: PASS");
}
