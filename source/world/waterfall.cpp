#include "world/waterfall.h"

#include "core/log.h"
#include "render/renderer.h"

namespace tt::world {

namespace {
// golf_waterfall slides its texture projector joint +1.0 in v per 200 frames at 24 fps, and
// OZSafeZoneLoader plays it at rate 3.5: 0.42 per second; golf_geyser -5.0 per 200 frames at
// rate 8.6: -5.16 per second (bake_waterfall.py checks both anims).
constexpr float kWaterfallScroll = 1.0f / 200.0f * 24.0f * 3.5f;
constexpr uint32_t kWaterfallPeriodMs = 2381; // one whole texture repeat
constexpr float kGeyserScroll = -5.0f / 200.0f * 24.0f * 8.6f;
constexpr uint32_t kGeyserPeriodMs = 1938;    // five texture repeats
// geyserPlacer.getPos() in golf_outdoor_zone (bake_waterfall.py reports it).
constexpr float kGeyserPos[3] = {114.379f, -127.445f, -0.002f};

// OZSafeZoneLoader.setGeyserAnim, every 20 s: (seconds, scale from, scale to, up from, up to),
// "max" being 0.75 + 0.4 * random() per eruption; up 0 is geyserPos - 8, 1 is geyserPos.
struct Step { float seconds, scaleFrom, scaleTo; int upFrom, upTo; };
constexpr float kMax = -1.0f;
constexpr Step kSteps[] = {{2, 0.01f, 0.75f, 0, 0}, {1, 0.75f, kMax, 0, 1}, {2, kMax, 0.75f, 1, 0},
                           {1, 0.75f, kMax, 0, 1},  {2, kMax, 0.75f, 1, 0}, {1, 0.75f, kMax, 0, 1},
                           {4, kMax, 0.01f, 1, 0}};
} // namespace

void Waterfall::setZone(uint32_t zoneId) {
    model_.unload();
    geyser_.unload();
    if (zoneId != 6000) return;
    if (!model_.load("sdmc:/3ds/toontown3ds/data/zones/6000_waterfall.ttm"))
        TT_LOG("waterfall: cannot load 6000_waterfall.ttm");
    if (!geyser_.load("sdmc:/3ds/toontown3ds/data/zones/6000_geyser.ttm"))
        TT_LOG("waterfall: cannot load 6000_geyser.ttm");
}

void Waterfall::draw(render::Renderer& renderer, uint32_t nowMs) const {
    if (model_.loaded()) {
        C3D_Mtx world;
        Mtx_Identity(&world);
        renderer.setUvShift(0.0f, float(nowMs % kWaterfallPeriodMs) * 0.001f * kWaterfallScroll);
        renderer.drawModel(const_cast<render::Model&>(model_), world);
    }
    if (geyser_.loaded()) {
        const uint32_t cycle = nowMs / 20000;
        float t = float(nowMs % 20000) * 0.001f;
        // A repeatable stand-in for random(): one size per eruption.
        const float maxSize = 0.75f + 0.4f * float((cycle * 2654435761u) >> 22 & 1023) / 1023.0f;
        float scale = 0.0f, up = 0.0f;
        for (const Step& s : kSteps) {
            if (t <= s.seconds) {
                const float u = t / s.seconds;
                const float from = s.scaleFrom == kMax ? maxSize : s.scaleFrom;
                const float to = s.scaleTo == kMax ? maxSize : s.scaleTo;
                scale = from + (to - from) * u;
                up = float(s.upFrom) + float(s.upTo - s.upFrom) * u;
                break;
            }
            t -= s.seconds;
        }
        if (scale > 0.02f) { // after the eruption it rests at scale 0.01 out of sight
            C3D_Mtx world;
            Mtx_Identity(&world);
            Mtx_Translate(&world, kGeyserPos[0], kGeyserPos[1], kGeyserPos[2] - 8.0f * (1.0f - up), true);
            Mtx_Scale(&world, scale, scale, scale);
            renderer.setUvShift(0.0f, float(nowMs % kGeyserPeriodMs) * 0.001f * kGeyserScroll);
            renderer.drawModel(const_cast<render::Model&>(geyser_), world);
        }
    }
    renderer.setUvShift(0.0f, 0.0f);
}

} // namespace tt::world
