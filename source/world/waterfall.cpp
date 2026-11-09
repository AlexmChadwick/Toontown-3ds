#include "world/waterfall.h"

#include "core/log.h"
#include "render/renderer.h"

namespace tt::world {

namespace {
// golf_waterfall slides its texture projector joint +1.0 in v per 200 frames at 24 fps, and
// OZSafeZoneLoader plays it at rate 3.5: 0.42 per second (bake_waterfall.py checks the anim).
constexpr float kScrollPerSecond = 1.0f / 200.0f * 24.0f * 3.5f;
constexpr uint32_t kPeriodMs = 2381; // 1 / kScrollPerSecond, so the shift stays small
} // namespace

void Waterfall::setZone(uint32_t zoneId) {
    model_.unload();
    if (zoneId != 6000) return;
    if (!model_.load("sdmc:/3ds/toontown3ds/data/zones/6000_waterfall.ttm"))
        TT_LOG("waterfall: cannot load 6000_waterfall.ttm");
}

void Waterfall::draw(render::Renderer& renderer, uint32_t nowMs) const {
    if (!model_.loaded()) return;
    C3D_Mtx world;
    Mtx_Identity(&world);
    renderer.setUvShift(0.0f, float(nowMs % kPeriodMs) * 0.001f * kScrollPerSecond);
    renderer.drawModel(const_cast<render::Model&>(model_), world);
    renderer.setUvShift(0.0f, 0.0f);
}

} // namespace tt::world
