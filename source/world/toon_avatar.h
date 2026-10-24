// Animated toon: three baked parts (legs, torso, head) composed through the
// per-frame attach tracks baked by tools/pipeline/bake_toon.py.
#pragma once

#include <3ds.h>
#include <citro3d.h>

#include "render/anim.h"
#include "render/model.h"
#include "render/renderer.h"

namespace tt::world {

enum ToonAnim : int { kAnimNeutral = 0, kAnimWalk, kAnimRun, kAnimJumpIdle, kAnimWave, kAnimCount };
// Clip names as baked (tools/pipeline/bake_toon.py ANIMS) and as sent in setAnimState.
extern const char* const kToonAnimNames[kAnimCount];

// GPU resources for one body combination. Shared by every toon that uses it.
struct ToonParts {
    enum Part : int { kLegs = 0, kTorso, kHead, kPartCount };
    render::Model model[kPartCount];
    render::Anim anim[kPartCount][kAnimCount];

    // Loads <dir>/<part>_<code>_<lod>.ttm and the clips <...>_<anim>.tta.
    bool load(const char* dir, const char* legsCode, const char* torsoCode, const char* headCode, const char* lod);
    void unload();
    u32 linearBytes() const;
};

class ToonAvatar {
public:
    void setParts(ToonParts* parts) { parts_ = parts; }
    // Switches clip (restarting it) only when the clip changes; rate may be negative.
    void play(ToonAnim anim, float rate);
    void update(float dt) { time_ += dt * rate_; }
    void draw(render::Renderer& renderer, const C3D_Mtx& world, const render::Tints* tints) const;
    ToonAnim anim() const { return anim_; }
    float rate() const { return rate_; }

private:
    ToonParts* parts_ = nullptr;
    ToonAnim anim_ = kAnimNeutral;
    float rate_ = 1.0f;
    float time_ = 0.0f;
};

} // namespace tt::world
