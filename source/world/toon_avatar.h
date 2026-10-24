// Animated toon: legs, torso and head parts from the ToonCache, composed through the
// per-frame attach tracks baked by tools/pipeline/bake_toon.py.
#pragma once

#include <3ds.h>
#include <citro3d.h>

#include "render/renderer.h"
#include "world/toon_cache.h"
#include "world/toon_playback.h"
#include "world/toon_emote.h"

namespace tt::world {

static_assert(kAnimCount == kToonAnimCount, "clip table size");

// Extra clips for one toon's parts, loaded on demand (battle clips for the local toon) instead
// of with every look: <part key>_<name>.tta from the SD set, one per part (legs, torso, head).
struct ToonClipSet {
    static constexpr int kMax = 8;
    ToonAnim clips[kMax] = {};
    int count = 0;
    const ToonLook* look = nullptr;
    // Compatibility booking only; actual battle IO uses the bounded shared streamer.
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
    void playAction(ToonAnim anim, ClipMode mode, float rate=1, float start=0, float end=-1,
                    bool restart=false, float seek=0);
    void seek(float elapsed) { playback_.elapsed=elapsed>=0?elapsed:0; }
    void hold(ToonAnim anim,float sourceTime) { playAction(anim,ClipMode::Hold,0,sourceTime); }
    bool finished() const;
    bool actionActive() const { return playback_.mode==ClipMode::Once && !finished(); }
    u32 token() const { return playback_.token; }
    void cancel();
    bool playEmote(int id,float elapsed=0);
    bool emoteActive() const { return emote_>=0; }
    bool bodyLocked() const { return bodyEmote(emote_); }
    bool popEmoteFinished() { const bool done=emoteFinished_; emoteFinished_=false; return done; }
    void update(float dt);
    void setHeadOnly(bool on) { headOnly_=on; }
    void setBodyPose(float pitch=0,float z=0) { bodyPitch_=pitch;bodyZ_=z; }
    void setHeadPitch(float pitch) { headPitch_=pitch; }
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
    ToonPlayback playback_;
    int emote_ = -1;
    float emoteAge_ = 0;
    bool emoteFinished_ = false, headOnly_ = false;
    float bodyPitch_=0,bodyZ_=0,headPitch_=-45;

};

} // namespace tt::world
