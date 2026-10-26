// Headless diagnostic driver: inputs move the real physics through original collision.
#include "world/collision.h"
#include "world/racing_logic.h"
#include "world/racing_track.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
int main() {
    using namespace tt::racing;
    Track track;
    tt::world::CollisionWorld collision;
    assert(track.load("sdmc-out/3ds/toontown3ds/data/racing/screwball_stadium.trc", 0));
    assert(collision.load("sdmc-out/3ds/toontown3ds/data/racing/screwball_stadium.ttc"));
    Vehicle kart;
    kart.stopped = false;
    const auto *start = track.starts[0];
    kart.position = {start[0], start[1], start[2]};
    kart.heading = start[3];
    Progress progress;
    progress.reset(track.points, track.count, kart.position, track.start);
    float gravityVelocity = 0;
    for (int frame = 0; frame < 18000; ++frame) {
        const auto control = diagnosticControls(track.points, track.count, progress.pole, kart);
        const auto old = kart.position;
        kart.surface = track.surface(old.x, old.y, old.z);
        kart.update(1.0f / 30, control.horizontal, control.vertical);
        const auto desired = kart.position;
        const auto resolved = collision.slideCapsule(
            {old.x, old.y, old.z}, 4, 8, {desired.x - old.x, desired.y - old.y, desired.z - old.z});
        kart.position = {resolved.x, resolved.y, resolved.z};
        bool found = false;
        const float ground = collision.groundHeight(resolved.x, resolved.y, 40000, &found);
        if (found && kart.position.z <= ground + 4) {
            kart.position.z = ground + .025f;
            gravityVelocity = 0;
        } else {
            gravityVelocity -= 32.174f * 3 / 30;
            kart.position.z += gravityVelocity / 30;
        }
        if (progress.update(track.points, track.count, kart.position, kart.heading, track.start)) {
            std::printf("diagnostic lap %d at %.1fs t %.3f\n", progress.laps, frame / 30.0,
                        double(progress.fraction));
            if (progress.laps >= 3) {
                assert(frame < 240 * 30); // RaceGlobals qualifying time, not a client payout.
                return 0;
            }
        }
    }
    std::fprintf(stderr, "diagnostic failed to finish: lap %d t %.3f pos %.1f %.1f\n",
                 progress.laps, double(progress.fraction), double(kart.position.x),
                 double(kart.position.y));
    return 1;
}
