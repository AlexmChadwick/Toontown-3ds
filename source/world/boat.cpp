#include "world/boat.h"

#include <cmath>

#include "core/log.h"
#include "world/boat_paths.h"

namespace tt::world {

namespace {

constexpr const char* kModelPath = "sdmc:/3ds/toontown3ds/data/zones/1000_boat.ttm";
constexpr float kDegToRad = 3.14159265f / 180.0f;

// Samples a baked Mopath at t seconds (clamped to its duration): linear between samples.
void samplePath(const float (*pts)[4], int count, float duration, float t, float* out) {
    if (t < 0) t = 0;
    if (t > duration) t = duration;
    float f = t / kBoatPathStep;
    int i = static_cast<int>(f);
    if (i > count - 2) i = count - 2;
    f -= static_cast<float>(i);
    if (f > 1.0f) f = 1.0f;
    for (int k = 0; k < 4; k++) out[k] = pts[i][k] + (pts[i + 1][k] - pts[i][k]) * f;
}

} // namespace

void BoatActor::handleEvent(const net::Event& ev, const net::Client& client) {
    using net::EventType;
    if (ev.type == EventType::BoatGone) {
        if (active_ && doId_ == ev.doId) shutdown();
        return;
    }
    if (ev.type != EventType::BoatSpawn && ev.type != EventType::BoatState) return;
    const net::Boat* b = client.boat();
    if (!b || b->doId != ev.doId) return;
    if (!active_) {
        if (!model_.load(kModelPath)) {
            TT_LOG("boat: no model %s (bake tools/pipeline/bake_boat.py)", kModelPath);
        }
        active_ = true;
        Mtx_Identity(&matrix_);
        doId_ = b->doId;
    }
    // Upstream finish()es the previous track on a state change, which leaves the boat at the
    // end pose that the next state starts from, so only the state and its clock change.
    state_ = b->state;
    stateStartMs_ = b->stateStartMs;
}

// DistributedBoat state machine (docs/design/boat.md):
//   DockedEast  = end of dd-w-e    SailingWest = dd-e-w from t
//   DockedWest  = end of dd-e-w    SailingEast = dd-w-e from t
void BoatActor::pose(u32 nowMs) {
    const s32 ms = static_cast<s32>(nowMs - stateStartMs_);
    const float t = ms > 0 ? ms / 1000.0f : 0.0f;
    float p[4];
    switch (state_) {
    case net::BoatState::SailingWest:
        samplePath(kBoatEW, kBoatEWCount, kBoatEWDuration, t, p);
        break;
    case net::BoatState::DockedWest:
        samplePath(kBoatEW, kBoatEWCount, kBoatEWDuration, kBoatEWDuration, p);
        break;
    case net::BoatState::SailingEast:
        samplePath(kBoatWE, kBoatWECount, kBoatWEDuration, t, p);
        break;
    default: // DockedEast
        samplePath(kBoatWE, kBoatWECount, kBoatWEDuration, kBoatWEDuration, p);
        break;
    }
    x_ = p[0], y_ = p[1], z_ = p[2], h_ = p[3];
}

void BoatActor::update(u32 nowMs) {
    if (!active()) return;
    pose(nowMs);
    Mtx_Identity(&matrix_);
    Mtx_Translate(&matrix_, x_, y_, z_, true);
    Mtx_RotateZ(&matrix_, h_ * kDegToRad, true);
}

void BoatActor::draw(render::Renderer& renderer) {
    if (!active() || !model_.loaded()) return;
    renderer.drawModel(model_, matrix_);
}

void BoatActor::shutdown() {
    if (model_.loaded()) {
        C3D_FrameSync();
        model_.unload();
    }
    active_ = false;
    state_ = net::BoatState::Off;
}

} // namespace tt::world
