#include "world/cogdo_crane.h"
#include "world/cogdo_maze.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
using namespace tt::world;
static CogdoMaze maze;
int main() {
    assert(cogdoDifficulty(kCogdoNoDifficultyOverride, 2100, -1) == 0);
    assert(cogdoDifficulty(kCogdoNoDifficultyOverride, 1100, -1) == .2f);
    assert(cogdoDifficulty(kCogdoNoDifficultyOverride, 2100, 9000) == 1);
    assert(cogdoDifficulty(32768, 2100, -1) == .5f);
    assert(cogdoDifficulty(65536, 2100, -1) == 1);
    FILE *choices = std::fopen("sdmc-out/cogdo-test/maze_seeds.bin", "rb");
    FILE *maps = std::fopen("sdmc-out/cogdo-test/maze_maps.bin", "rb");
    FILE *walks = std::fopen("sdmc-out/cogdo-test/maze_walks.bin", "rb");
    FILE *coolers = std::fopen("sdmc-out/cogdo-test/maze_coolers.bin", "rb");
    assert(choices && maps && walks && coolers);
    uint32_t seed = 0;
    const uint8_t counts[] = {4, 5, 5};
    const char *directory = "sdmc-out/3ds/toontown3ds/data/cogdo/";
    int tested = 0;
    while (std::fread(&seed, 4, 1, choices) == 1) {
        uint8_t expected[30];
        assert(std::fread(expected, 1, sizeof(expected), choices) == sizeof(expected));
        assert(maze.load(seed, counts, .5f, directory));
        assert(!std::memcmp(maze.barriers, expected, 12));
        assert(!std::memcmp(maze.quadrant, expected + 12, 9));
        assert(!std::memcmp(maze.angle, expected + 21, 9));
        uint32_t mapSeed = 0;
        uint8_t cells[52][52];
        uint8_t spawns[14][2];
        assert(std::fread(&mapSeed, 4, 1, maps) == 1 && mapSeed == seed);
        assert(std::fread(cells, 1, sizeof(cells), maps) == sizeof(cells));
        assert(std::fread(spawns, 1, sizeof(spawns), maps) == sizeof(spawns));
        assert(!std::memcmp(cells, maze.cells, sizeof(cells)));
        assert(maze.numSuits == 14);
        for (int index = 0; index < 14; ++index) {
            assert(maze.suits[index].x == spawns[index][0] && maze.suits[index].y == spawns[index][1]);
        }
        uint32_t coolerSeed = 0;
        uint16_t coolerCount = 0;
        assert(std::fread(&coolerSeed, 4, 1, coolers) == 1 && coolerSeed == seed);
        assert(std::fread(&coolerCount, 2, 1, coolers) == 1 && coolerCount == maze.numCoolers);
        for (int index = 0; index < coolerCount; ++index) {
            float pose[7];
            assert(std::fread(pose, sizeof(pose), 1, coolers) == 1);
            const auto &cooler = maze.coolers[index];
            const float actual[] = {cooler.position.x, cooler.position.y, cooler.position.z, cooler.heading,
                                    cooler.trigger.x,  cooler.trigger.y,  cooler.trigger.z};
            for (int component = 0; component < 7; ++component) {
                const float delta = component == 3 ? std::remainder(actual[component] - pose[component], 360.0f)
                                                   : actual[component] - pose[component];
                if (std::fabs(delta) >= .001f) {
                    std::fprintf(stderr, "cooler seed %u index %d component %d %.6f != %.6f\n", seed, index,
                                 component, actual[component], pose[component]);
                    assert(false);
                }
            }
        }
        uint8_t hp[64];
        std::memset(hp, 2, sizeof(hp));
        for (int tick = 0; tick < 600; ++tick) {
            uint32_t when = 0;
            uint8_t expectedWalks[14][5];
            assert(std::fread(&when, 4, 1, walks) == 1);
            assert(std::fread(expectedWalks, sizeof(expectedWalks), 1, walks) == 1);
            maze.update(when / 256.0f, hp);
            for (int index = 0; index < maze.numSuits; ++index) {
                const auto &suit = maze.suits[index];
                const auto &expectedWalk = expectedWalks[index];
                if (suit.x != expectedWalk[0] || suit.y != expectedWalk[1] || suit.nextX != expectedWalk[2] ||
                    suit.nextY != expectedWalk[3] || suit.direction != expectedWalk[4]) {
                    std::fprintf(stderr,
                                 "walk seed %u frame %d actor %d actual %d,%d -> %d,%d d%u expected %d,%d -> "
                                 "%d,%d d%u\n",
                                 seed, tick, index, suit.x, suit.y, suit.nextX, suit.nextY,
                                 unsigned(suit.direction), expectedWalk[0], expectedWalk[1], expectedWalk[2],
                                 expectedWalk[3], expectedWalk[4]);
                    assert(false);
                }
                assert(std::isfinite(suit.position.x) && std::isfinite(suit.position.y));
                assert(suit.nextX >= 0 && suit.nextX < 52 && suit.nextY >= 0 && suit.nextY < 52);
            }
        }
        Vec3 next;
        assert(maze.route(CogdoMaze::tile(26, 2), CogdoMaze::tile(26, 50), &next));
        assert(std::isfinite(next.x));
        Vec3 position = CogdoMaze::tile(26, 2);
        position.x += 1.5f;
        position.y += 1.5f;
        for (Vec3 goal : {maze.coolers[0].trigger, CogdoMaze::tile(26, 50)}) {
            bool arrived = false;
            for (int frame = 0; frame < 3000; ++frame) {
                const float gx = goal.x - position.x, gy = goal.y - position.y;
                if (gx * gx + gy * gy < 3.25f * 3.25f) {
                    arrived = true;
                    break;
                }
                Vec3 step;
                assert(maze.route(position, goal, &step));
                const float dx = step.x - position.x, dy = step.y - position.y;
                const float length = std::sqrt(dx * dx + dy * dy);
                const float amount = std::min(.35f, length);
                Vec3 moved = position;
                if (length > .001f) {
                    moved.x += dx / length * amount;
                    moved.y += dy / length * amount;
                }
                maze.slide(position, &moved);
                position = moved;
            }
            if (!arrived)
                std::fprintf(stderr, "navigation stuck seed %u at %.2f %.2f toward %.2f %.2f\n", seed,
                             position.x, position.y, goal.x, goal.y);
            assert(arrived);
        }
        ++tested;
    }
    CogdoCable cable;
    cable.reset();
    for (int tick = 0; tick < 1800; ++tick) {
        cable.update(tick % 120 < 60 ? 1 : -1, tick % 240 < 120 ? 1 : -1, 1.0f / 30);
        assert(cable.y >= 8 && cable.y <= 25 && cable.heading >= -45 && cable.heading <= 45);
        for (int crane = 0; crane < 4; ++crane) {
            const Vec3 magnet = cable.magnet(crane);
            assert(std::isfinite(magnet.x) && std::isfinite(magnet.y) && std::isfinite(magnet.z));
        }
        for (const auto &link : cable.links)
            assert(std::isfinite(link.x) && std::isfinite(link.y) && std::isfinite(link.z));
    }
    std::fclose(choices);
    std::fclose(maps);
    std::fclose(walks);
    std::fclose(coolers);
    std::printf("Cogdo seeded quadrants, full maps, exact suit spawns, 3600-frame exact walks/drops, cooler "
                "indices, collision navigation: %d seeds PASS\n",
                tested);
}
