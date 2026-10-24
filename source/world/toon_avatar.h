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

// Extra clips for one toon's parts, loaded on demand (battle clips for the local toon) instead
// of with every look: <part key>_<name>.tta from the SD set, one per part (legs, torso, head).
struct ToonClipSet {
    static constexpr int kMax = 8;
    render::Anim clip[kMax][3];
    int count = 0;
    const ToonLook* look = nullptr; // the look the clips were loaded for
    // Main thread; loads synchronously (a few hundred KB). Replaces what was loaded before.
    void load(const ToonLook& look, const char* const* names, int n);
    void unload();
};

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
    // Plays clip `index` of `set` (held on its last frame unless `loop`); play() goes back.
    void playClip(const ToonClipSet* set, int index, bool loop);
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
    const ToonClipSet* extra_ = nullptr;
    int extraIndex_ = -1;
    bool extraLoop_ = false;
};

} // namespace tt::world
