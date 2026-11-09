#include "world/gs_blimp.h"

#include "core/log.h"
#include "render/renderer.h"

namespace tt::world {

void GsBlimp::setZone(uint32_t zoneId) {
    model_.unload();
    if (zoneId != 8000) return;
    if (!model_.load("sdmc:/3ds/toontown3ds/data/zones/8000_blimp.ttm")) TT_LOG("gs blimp: cannot load 8000_blimp.ttm");
}

void GsBlimp::draw(render::Renderer& renderer, uint32_t nowMs) const {
    if (!model_.loaded()) return;
    constexpr float kDeg = 3.14159265f / 180.0f;
    // blimpRoot (0, -70, 40) turning hprInterval(360, 360) -> blimpBase (0, -200, 25) h -40
    // -> GS_blimp setPos(-70, 250, -70).
    const float h = static_cast<float>(nowMs % 360000u) * 0.001f;
    C3D_Mtx m;
    Mtx_Identity(&m);
    Mtx_Translate(&m, 0.0f, -70.0f, 40.0f, true);
    Mtx_RotateZ(&m, h * kDeg, true);
    Mtx_Translate(&m, 0.0f, -200.0f, 25.0f, true);
    Mtx_RotateZ(&m, -40.0f * kDeg, true);
    Mtx_Translate(&m, -70.0f, 250.0f, -70.0f, true);
    renderer.drawModel(const_cast<render::Model&>(model_), m);
}

} // namespace tt::world
