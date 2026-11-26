// Headless diagnostic driver: inputs move the real physics through original collision.
#include "world/collision.h"
#include "world/racing_logic.h"
#include "world/racing_track.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdint>
int main() {
    using namespace tt::racing;
    { // PhysicsCollisionHandler.apply_net_shove: friction .1 x angle, only moving into the wall.
        Vehicle v;
        v.velocity = {0, -100, 0}; // head-on into a wall whose shove is +y
        v.wallShove(0, .5f);
        assert(std::fabs(v.velocity.x) < 1e-4f && std::fabs(v.velocity.y) < 1e-4f);
        v.velocity = {100, -1, 0}; // grazing: keeps ~99.9% of its speed
        v.wallShove(0, .5f);
        assert(std::fabs(v.velocity.x - 100 * (1 - .1f * (1 / std::sqrt(10001.0f)))) < 1e-3f && v.velocity.y == 0);
        v.velocity = {30, 40, 0}; // moving away from the wall: untouched
        v.wallShove(0, .5f);
        assert(v.velocity.x == 30 && v.velocity.y == 40);
    }
    const unsigned ids[] = {0, 1, 20, 21, 40, 41, 60, 61, 62, 63, 64, 65};
    for (unsigned id : ids) {
        Track track;
        tt::world::CollisionWorld collision;
        char path[256];
        std::snprintf(path, sizeof(path), "sdmc-out/3ds/toontown3ds/data/racing/%s.trc", trackName(id));
        assert(track.load(path, id));
        std::snprintf(path, sizeof(path), "sdmc-out/3ds/toontown3ds/data/racing/%s.ttc", trackName(id));
        assert(collision.load(path));
        Vehicle kart;
        kart.stopped = false;
        const auto *start = track.starts[2];
        kart.position = {start[0], start[1], start[2]};
        kart.heading = start[3];
        Progress progress;
        progress.reset(track.points, track.count, kart.position, track.start);
        float gravityVelocity = 0, elapsed = 0;
        int lap = 0;
        for (int frame = 0; frame < 12000; ++frame) {
            // Device-like 27-30 fps with occasional 200 ms scheduling hitches;
            // main.cpp caps an individual simulation dt at 100 ms.
            const float rawDt = frame % 211 == 210 ? .2f :
                                (frame % 4 == 0 ? .033f : frame % 4 == 1 ? .037f :
                                 frame % 4 == 2 ? .05f : .09f);
            const float dt = std::min(rawDt, .1f);
            elapsed += dt;
            const auto control = diagnosticControls(track.points, track.count, progress.pole, kart);
            const int16_t padX = int16_t(control.horizontal * 156);
            const int16_t padY = int16_t(control.vertical * 156);
            const float horizontal = padX / 156.0f, vertical = padY / 156.0f;
            const auto old = kart.position;
            kart.surface = track.surface(old.x, old.y, old.z);
            kart.update(dt, horizontal, vertical);
            const auto desired = kart.position;
            const auto resolved = collision.slideCapsule(
                {old.x, old.y, old.z}, 4, 8,
                {desired.x - old.x, desired.y - old.y, desired.z - old.z});
            kart.wallShove(resolved.x - desired.x, resolved.y - desired.y); // as Racing::update
            kart.position = {resolved.x, resolved.y, resolved.z};
            bool found = false;
            const float ground = collision.groundHeight(resolved.x, resolved.y, 40000, &found);
            if (found && kart.position.z <= ground + 4) {
                kart.position.z = ground + .025f;
                gravityVelocity = 0;
            } else {
                gravityVelocity -= 32.174f * 3 * dt;
                kart.position.z += gravityVelocity * dt;
            }
            if (progress.update(track.points, track.count, kart.position, kart.heading, track.start) &&
                progress.laps > lap) {
                lap = progress.laps;
                std::printf("track %u lap %d at %.1fs speed %.1f\n", id, lap, elapsed,
                            double(kart.speed()));
            }
            if (progress.laps >= 3) break;
        }
        if (progress.laps < 3 || (id == 0 && elapsed > 240)) {
            std::fprintf(stderr, "track %u failed: lap %d at %.1fs pos %.1f %.1f speed %.1f\n",
                         id, progress.laps, elapsed, double(kart.position.x),
                         double(kart.position.y), double(kart.speed()));
            return 1;
        }
    }
    return 0;
}
