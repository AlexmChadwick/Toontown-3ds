#include "world/br_snow.h"

#include <cmath>

#include "core/log.h"
#include "render/renderer.h"

namespace tt::world {

namespace {
constexpr float kTile = 64.0f;  // bake_br_snow.TILE
constexpr float kFall = 15.0f;  // one flake's fall over its 4.5 s life; the tile is two falls tall
constexpr float kSpeed = 3.3f;  // kFall / 4.5 s
} // namespace

void BrSnow::setZone(uint32_t zoneId) {
    model_.unload();
    if (zoneId != 3000) return;
    if (!model_.load("sdmc:/3ds/toontown3ds/data/zones/3000_snow.ttm")) TT_LOG("br snow: cannot load 3000_snow.ttm");
}

void BrSnow::draw(render::Renderer& renderer, const C3D_FVec& eye, uint32_t nowMs) const {
    if (!model_.loaded()) return;
    // Tiles on a world grid, so flakes do not travel with the camera: the 2 x 2 block whose
    // centre is nearest the eye.
    const float x0 = std::floor(eye.x / kTile - 0.5f) * kTile;
    const float y0 = std::floor(eye.y / kTile - 0.5f) * kTile;
    // Falling: the pattern repeats every kFall, so shift it down by the elapsed fall, wrapped,
    // and keep [eye - 10, eye + 5] inside the tile's two falls.
    const float scroll = std::fmod((nowMs % 3600000u) * 0.001f * kSpeed, kFall);
    const float z0 = std::floor((eye.z - 10.0f + scroll) / kFall) * kFall - scroll;
    for (int i = 0; i < 4; ++i) {
        C3D_Mtx m;
        Mtx_Identity(&m);
        Mtx_Translate(&m, x0 + kTile * (i & 1), y0 + kTile * (i >> 1), z0, true);
        renderer.drawModel(const_cast<render::Model&>(model_), m);
    }
}

} // namespace tt::world
