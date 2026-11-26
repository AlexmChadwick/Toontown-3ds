#include "world/dg_flower.h"

#include <cmath>

#include "core/log.h"
#include "net/client.h"
#include "render/renderer.h"

namespace tt::world {

namespace {
constexpr float kX = 1.39f, kY = 92.91f, kScale = 2.5f;
constexpr float kWallRadius = 4.5f * kScale;    // CollisionSphere(0, 0, 0, 4.5) under the scaled flower
constexpr float kTriggerRadius = 6.0f * kScale; // the intangible 'bigFlowerTrigger' sphere
constexpr float kDegPerSecond = 1.25f * 60.0f;  // SPIN_RATE a frame, at 60 frames a second
} // namespace

void DGFlower::setZone(uint32_t zoneId) {
    model_.unload();
    present_ = inside_ = false;
    if (zoneId != 5000) return;
    if (!model_.load("sdmc:/3ds/toontown3ds/data/zones/5000_flower.ttm")) TT_LOG("dg flower: cannot load 5000_flower.ttm");
    solids_ = DynamicSolids{};
    solids_.push = &DGFlower::push;
    solids_.user = this;
}

void DGFlower::update(net::Client& client, float x, float y, float z, uint32_t nowMs) {
    const net::DGFlower* f = client.dgFlower();
    present_ = f && model_.loaded();
    if (!present_) {
        inside_ = false;
        return;
    }
    // setHeight: posInterval(0.5) from where it is to the new height.
    if (f->height != toZ_) {
        fromZ_ = z_;
        toZ_ = f->height;
        moveMs_ = nowMs;
    }
    const float u = (nowMs - moveMs_) / 500.0f;
    z_ = u >= 1.0f ? toZ_ : fromZ_ + (toZ_ - fromZ_) * u;
    const float dx = x - kX, dy = y - kY, dz = z + kToonRadius - z_;
    const bool inside = dx * dx + dy * dy + dz * dz < (kTriggerRadius + kToonRadius) * (kTriggerRadius + kToonRadius);
    if (inside != inside_ && client.sendDGFlowerTouch(inside)) inside_ = inside;
}

void DGFlower::draw(render::Renderer& renderer, uint32_t nowMs) const {
    if (!present_) return;
    C3D_Mtx m;
    Mtx_Identity(&m);
    Mtx_Translate(&m, kX, kY, z_, true);
    Mtx_RotateZ(&m, std::fmod(nowMs * 0.001f * kDegPerSecond, 360.0f) * 3.14159265f / 180.0f, true);
    Mtx_Scale(&m, kScale, kScale, kScale);
    renderer.drawModel(const_cast<render::Model&>(model_), m);
}

// The flower's wall sphere: push the toon's (x, y) out where its body overlaps it.
void DGFlower::push(void* user, float* x, float* y, float z, float radius) {
    const DGFlower& self = *static_cast<const DGFlower*>(user);
    const float dz = z + radius - self.z_;
    const float reach = kWallRadius + radius;
    if (dz * dz >= reach * reach) return;
    const float ring = std::sqrt(reach * reach - dz * dz);
    const float dx = *x - kX, dy = *y - kY;
    const float d = std::sqrt(dx * dx + dy * dy);
    if (d >= ring || d < 1e-4f) return;
    *x = kX + dx / d * ring;
    *y = kY + dy / d * ring;
}

} // namespace tt::world
