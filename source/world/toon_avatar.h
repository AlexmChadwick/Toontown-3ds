// Animated toon: legs, torso and head parts from the ToonCache, composed through the
// per-frame attach tracks baked by tools/pipeline/bake_toon.py.
#pragma once

#include <3ds.h>
#include <citro3d.h>

#include "render/renderer.h"
#include "world/toon_cache.h"

namespace tt::world {

enum ToonAnim : int { kAnimNeutral = 0, kAnimWalk, kAnimRun, kAnimJumpIdle, kAnimWave, kAnimCount };
static_assert(kAnimCount == kToonAnimCount, "clip table size");
// Clip names as baked (tools/pipeline/bake_toon.py ANIMS).
extern const char* const kToonAnimNames[kAnimCount];

class ToonAvatar {
public:
    // look: this toon's parts; fallback: drawn while `look` is still loading.
    void setLook(const ToonLook* look, const ToonLook* fallback = nullptr) {
        look_ = look;
        fallback_ = fallback;
    }
    const ToonLook* look() const { return look_; }
    // Switches clip (restarting it) only when the clip changes; rate may be negative.
    void play(ToonAnim anim, float rate);
    void update(float dt) { time_ += dt * rate_; }
    // `world` places the toon's feet; body scale and clothing come from the look.
    // tintOverride (optional) replaces the look's DNA colours (offline colour picker).
    void draw(render::Renderer& renderer, const C3D_Mtx& world, const render::Tints* tintOverride = nullptr) const;
    ToonAnim anim() const { return anim_; }
    float rate() const { return rate_; }

private:
    const ToonLook* look_ = nullptr;
    const ToonLook* fallback_ = nullptr;
    ToonAnim anim_ = kAnimNeutral;
    float rate_ = 1.0f;
    float time_ = 0.0f;
};

} // namespace tt::world
