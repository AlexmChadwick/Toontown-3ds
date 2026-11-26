#include "world/butterflies.h"

#include <cmath>
#include <cstdio>

#include "core/log.h"
#include "net/client.h"
#include "render/renderer.h"
#include "world/butterfly_points.h"

namespace tt::world {

namespace {
constexpr float kDegToRad = 3.14159265f / 180.0f;
// DistributedButterfly's LODNode: drawn out to 100 units (animated inside 40).
constexpr float kDrawDistance = 100.0f;
constexpr float kAnimDistance = 40.0f;

// DistributedButterfly.yellowColors / whiteColors / paleYellowColors, picked by doId.
const float kYellow[3][3] = {{1, 1, 1}, {0.2f, 0, 1}, {0.8f, 0, 1}};
const float kWhite[6][3] = {{0.8f, 0, 0.8f}, {0, 0.8f, 0.8f}, {0.9f, 0.4f, 0.6f}, {0.9f, 0.4f, 0.4f}, {0.8f, 0.5f, 0.9f}, {0.4f, 0.1f, 0.7f}};
const float kPale[6][3] = {{0.8f, 0, 0.8f}, {0.6f, 0.6f, 0.9f}, {0.7f, 0.6f, 0.9f}, {0.8f, 0.6f, 0.9f}, {0.9f, 0.6f, 0.9f}, {1, 0.6f, 0.9f}};

void wingColor(uint32_t doId, float out[3]) {
    const int index = int(doId % 6);
    const float* c = index <= 1 ? kYellow[doId % 3] : index <= 3 ? kWhite[doId % 6] : index == 4 ? kPale[doId % 6] : nullptr;
    out[0] = c ? c[0] : 1.0f;
    out[1] = c ? c[1] : 1.0f;
    out[2] = c ? c[2] : 1.0f;
}

// RandomNumGen(doId).random() stand-in: per-butterfly play rate 0.6 .. 1.4.
float playRate(uint32_t doId) { return 0.6f + 0.8f * float((doId * 2654435761u) >> 20 & 4095) / 4095.0f; }

// NodePath.headsUp: +y toward the target.
float headingTo(const float* from, const float* to) {
    return std::atan2(-(to[0] - from[0]), to[1] - from[1]) / kDegToRad;
}
} // namespace

void Butterflies::setZone(uint32_t zoneId) {
    const bool want = zoneId == 2000 || zoneId == 5000;
    if (want == loaded_) return;
    shutdown();
    if (!want) return;
    char path[96];
    for (int w = 0; w < kWings; ++w) {
        snprintf(path, sizeof(path), "sdmc:/3ds/toontown3ds/data/butterfly/wing_%d.ttm", w);
        bool ok = model_[w].load(path);
        snprintf(path, sizeof(path), "sdmc:/3ds/toontown3ds/data/butterfly/wing_%d_flutter.tta", w);
        ok = flutter_[w].load(path) && ok;
        snprintf(path, sizeof(path), "sdmc:/3ds/toontown3ds/data/butterfly/wing_%d_land.tta", w);
        ok = land_[w].load(path) && ok;
        if (!ok) TT_LOG("butterflies: wing set %d missing", w);
    }
    loaded_ = true;
}

void Butterflies::shutdown() {
    for (int w = 0; w < kWings; ++w) {
        model_[w].unload();
        flutter_[w].unload();
        land_[w].unload();
    }
    loaded_ = false;
}

void Butterflies::draw(render::Renderer& renderer, const net::Client& client, float eyeX, float eyeY, uint32_t nowMs) {
    if (!loaded_) return;
    namespace bf = butterfly;
    for (int i = 0; i < net::kMaxButterflies; ++i) {
        const net::Butterfly& b = client.butterflySlot(i);
        if (!b.active || b.state == 0 || b.playground < 0 || b.playground >= bf::kPlaygrounds) continue;
        const int pg = b.playground;
        if (b.area < 0 || b.area >= bf::kAreas[pg]) continue;
        const int count = bf::kNumPoints[pg][b.area];
        if (b.cur >= count || b.dest >= count) continue;
        const float* cur = bf::kPoints[pg][b.area][b.cur];
        const float* dest = bf::kPoints[pg][b.area][b.dest];
        const float rate = playRate(b.doId);
        const float age = int32_t(nowMs - b.startMs) / 1000.0f;
        float pos[3] = {cur[0], cur[1], cur[2]};
        float heading = headingTo(cur, dest);
        bool flying = false;
        if (b.state == 1 && age <= b.seconds) {
            // enterFlying: take off to cur + height, cross at that height, land on dest.
            const float takeoff = bf::kTakeoff[pg], landing = bf::kLanding[pg];
            const float flyTime = b.seconds - takeoff - landing;
            const float high = (cur[2] > dest[2] ? cur[2] : dest[2]) + bf::kHeight[pg];
            flying = true;
            if (age < takeoff) {
                const float u = age < 0 ? 0 : age / takeoff;
                pos[2] = cur[2] + (high - cur[2]) * u;
            } else if (age < takeoff + flyTime) {
                const float u = flyTime > 0 ? (age - takeoff) / flyTime : 1.0f;
                pos[0] = cur[0] + (dest[0] - cur[0]) * u;
                pos[1] = cur[1] + (dest[1] - cur[1]) * u;
                pos[2] = high;
            } else {
                const float u = (age - takeoff - flyTime) / landing;
                pos[0] = dest[0], pos[1] = dest[1];
                pos[2] = high + (dest[2] - high) * (u > 1 ? 1 : u);
            }
        } else if (b.state == 1) {
            pos[0] = dest[0], pos[1] = dest[1], pos[2] = dest[2]; // the flight is over: landed on dest
        }
        const float dx = pos[0] - eyeX, dy = pos[1] - eyeY;
        const float dist2 = dx * dx + dy * dy;
        if (dist2 > kDrawDistance * kDrawDistance) continue;
        const int w = int(b.doId % kWings);
        const render::Anim& anim = flying ? flutter_[w] : land_[w];
        if (!model_[w].loaded() || !anim.loaded() || anim.numVertices() != model_[w].numVertices()) continue;
        // Inside 40 the clip loops; past it a landed butterfly holds one random land frame.
        u32 fa, fb;
        float blend = 0.0f;
        if (flying || dist2 < kAnimDistance * kAnimDistance) {
            const float t = std::fmod(nowMs * 0.001f * rate * anim.fps(), float(anim.numFrames()));
            fa = u32(t) % anim.numFrames();
            fb = (fa + 1) % anim.numFrames();
            blend = t - std::floor(t);
        } else {
            fa = fb = (b.doId * 7919u) % anim.numFrames();
        }
        C3D_Mtx m;
        Mtx_Identity(&m);
        Mtx_Translate(&m, pos[0], pos[1], pos[2], true);
        Mtx_RotateZ(&m, (heading + 180.0f) * kDegToRad, true); // the actor is turned 180 in its node
        render::Tints tints;
        tints.setAll(1, 1, 1);
        wingColor(b.doId, tints.rgba[1]);
        renderer.drawMorph(model_[w], anim, fa, fb, blend, m, &tints);
    }
}

} // namespace tt::world
