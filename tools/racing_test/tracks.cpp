#include "world/collision.h"
#include "world/racing_track.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
int main() {
    using namespace tt::racing;
    FILE *oracle = std::fopen("sdmc-out/racing2/floors.bin", "rb");
    assert(oracle);
    const unsigned ids[] = {0, 1, 20, 21, 40, 41, 60, 61, 62, 63, 64, 65};
    for (unsigned id : ids) {
        Track track;
        tt::world::CollisionWorld collision;
        char path[256];
        std::snprintf(path, sizeof(path), "sdmc-out/3ds/toontown3ds/data/racing/%s.trc",
                      trackName(id));
        assert(track.load(path, id));
        std::snprintf(path, sizeof(path), "sdmc-out/3ds/toontown3ds/data/racing/%s.ttc",
                      trackName(id));
        assert(collision.load(path));
        assert(track.count == 4000 && track.numStarts >= 4);
        int grounds = 0, tagged = 0;
        for (int i = 0; i < track.count; i += 10) {
            const auto &point = track.points[i];
            bool found = false;
            const float ground = collision.groundHeight(point.x, point.y, 40000, &found);
            uint32_t expectedFound = 0;
            float expectedHeight = 0;
            assert(std::fread(&expectedFound, 4, 1, oracle) == 1);
            assert(std::fread(&expectedHeight, 4, 1, oracle) == 1);
            assert(found == bool(expectedFound));
            if (found)
                assert(std::fabs(ground - expectedHeight) < .03f);
            if (found) {
                ++grounds;
                if (int(track.surface(point.x, point.y, ground)) == int(point.surface))
                    ++tagged;
            }
        }

        assert(tagged == grounds);
        std::printf(
            "track %u: %d/400 curve floors; %d matching exact surface tags; collision %u bytes\n",
            id, grounds, tagged, collision.heapBytes());
    }
    std::fflush(stdout);
    assert(std::fgetc(oracle) == EOF);
    std::fclose(oracle);
    Track invalid;
    assert(!invalid.load("sdmc-out/3ds/toontown3ds/data/racing/screwball_stadium.trc", 20));
    assert(!invalid.loaded());
    std::puts(
        "racing: all 12 original tracks, floor/surface metadata and invalid track guard PASS");
}
