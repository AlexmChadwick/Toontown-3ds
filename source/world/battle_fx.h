// Battle gag and attack effects: props in hand via JNT tracks, pie arcs and splats,
// squirt sprays, sound/drop/heal visuals, cog attack props, and death kapow.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>

#ifdef __3DS__
#include <3ds.h>
#include <citro2d.h>
#include <citro3d.h>
#include "audio/audio.h"
#include "render/anim.h"
#include "render/model.h"
#include "render/renderer.h"
#include "world/suits.h"
#include "world/toon_anim.h"
#include "world/toon_avatar.h"
#include "world/toon_cache.h"
#include "world/battle_movies.h"
#endif

namespace tt::world {

class Battle;

// ----------------------------------------------------------------------
// Projectile Parabolic Trajectory Math
// ----------------------------------------------------------------------
struct ProjectileArc {
    float x0 = 0.0f, y0 = 0.0f, z0 = 0.0f;
    float x1 = 0.0f, y1 = 0.0f, z1 = 0.0f;
    float maxHeight = 2.5f;
    uint32_t startMs = 0;
    uint32_t hitMs = 0;

    float alpha(uint32_t nowMs) const {
        if (hitMs <= startMs) return 1.0f;
        if (nowMs <= startMs) return 0.0f;
        if (nowMs >= hitMs) return 1.0f;
        return float(nowMs - startMs) / float(hitMs - startMs);
    }

    bool done(uint32_t nowMs) const { return nowMs >= hitMs; }

    void sample(uint32_t nowMs, float* x, float* y, float* z, float* heading = nullptr,
                float* pitch = nullptr) const {
        const float a = alpha(nowMs);
        const float curX = x0 + (x1 - x0) * a;
        const float curY = y0 + (y1 - y0) * a;
        const float curZ = z0 + (z1 - z0) * a + 4.0f * maxHeight * a * (1.0f - a);
        if (x) *x = curX;
        if (y) *y = curY;
        if (z) *z = curZ;

        if (heading) {
            const float dx = x1 - x0;
            const float dy = y1 - y0;
            *heading = atan2f(dy, dx) * 180.0f / 3.14159265f - 90.0f;
        }
        if (pitch) {
            const float dz_da = (z1 - z0) + 4.0f * maxHeight * (1.0f - 2.0f * a);
            const float dxy = sqrtf((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
            *pitch = atan2f(dz_da, dxy > 0.001f ? dxy : 0.001f) * 180.0f / 3.14159265f;
        }
    }
};

// ----------------------------------------------------------------------
// Splat Configuration (per props_manifest.json)
// ----------------------------------------------------------------------
struct SplatConfig {
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
    float scale = 0.5f;
};
SplatConfig getSplatConfig(int track, int level);

// ----------------------------------------------------------------------
// Movie Timing Specifications (per combat.md §6 and upstream)
// ----------------------------------------------------------------------
struct MovieTiming {
    uint32_t releaseMs; // projectile leaves hand
    uint32_t hitMs;     // projectile hits target
    uint32_t durMs;     // total action duration
};
MovieTiming getMovieTiming(int track, int level);

// ----------------------------------------------------------------------
// Gag Atlas Subtexture Coordinates (256x256 atlas, 28x28 icons)
// ----------------------------------------------------------------------
struct GagSubTex {
    uint16_t width, height;
    float left, top, right, bottom;
};
extern const GagSubTex kGagSubTextures[7][7];

#ifdef __3DS__

// ----------------------------------------------------------------------
// JNT1 Loader & Sampler
// ----------------------------------------------------------------------
struct JntTrack {
    uint16_t jointCount = 0;
    uint32_t frameCount = 0;
    float fps = 12.0f;
    uint32_t dataOffset = 0;
    uint32_t frameStride = 0;
    char names[4][32] = {};
    float* data = nullptr;

    bool load(const char* path);
    void unload();
    bool loaded() const { return data != nullptr; }
    int findJoint(const char* name) const;
    bool sample(int jointIdx, float t, C3D_Mtx* out) const;
    int joint(const char* name) const {
        for (int i = 0; i < jointCount && i < 4; ++i) if (!strcmp(names[i], name)) return i;
        return -1;
    }
};

// ----------------------------------------------------------------------
// Gag Atlas 2D Icon Drawing
// ----------------------------------------------------------------------
class GagAtlas {
public:
    static GagAtlas& instance();
    bool init();
    void shutdown();
    bool loaded() const { return model_.loaded(); }
    void drawIcon(int track, int level, float x, float y, float size, float depth = 0.55f, float alpha = 1.0f);

private:
    render::Model model_;
};

// ----------------------------------------------------------------------
// BattleFx Manager
// ----------------------------------------------------------------------
class BattleFx {
public:
    BattleFx() = default;
    ~BattleFx() { shutdown(); }

    bool init();
    void shutdown();
    void reset();

    // Event hooks called by Battle / movie execution
    void onAttack(uint32_t attackerId, uint32_t targetId, int track, int level,
                  uint32_t startMs, uint32_t durMs, uint32_t hitMs, const Battle* battle = nullptr);
    void onHit(uint32_t targetId, uint32_t attackerId, int track, int level, int hp, bool miss,
               const Battle* battle = nullptr);
    void onDie(uint32_t cogId, uint32_t startMs);
    void onHeal(uint32_t targetId, uint32_t healerId, int hp);

    BattleMovies& movies() { return movies_; }
    const BattleMovies& movies() const { return movies_; }

    // Frame update and 3D draw
    void update(float dt, uint32_t nowMs, const Battle* battle, const ToonLook* localLook);
    void draw(render::Renderer& renderer, const Battle* battle, const ToonLook* localLook);
    // Finds the drawn avatar of a battle toon (local or remote) so held props follow its
    // torso's JNT hand joint. Call draw() after the toons are drawn this frame.
    using ToonLookup = const ToonAvatar* (*)(void* user, uint32_t doId);
    void setToonLookup(ToonLookup fn, void* user) { toonLookup_ = fn; toonLookupUser_ = user; }
    void drawExplosion(render::Renderer& renderer, float x, float y, float z) const;


    // Sound cue playback
    void playCue(const char* name, float volume = 1.0f);

private:
    // Prop models and animations
    render::Model tartModel_;
    render::Model fruitpieModel_;
    render::Model creampieModel_;
    render::Model splatModel_;
    render::Anim splatAnim_;
    render::Model flowerModel_;
    render::Model watergunModel_;
    render::Model bottleModel_;
    render::Model splashModel_;
    render::Model megaphoneModel_;
    render::Model buttonModel_;
    render::Model featherModel_;
    render::Anim featherAnim_;
    render::Model lipstickModel_;
    render::Model phoneModel_;
    render::Model receiverModel_;
    render::Model explosionModel_; // phase_3.5 explosion: the Cog-death burst
    ToonLookup toonLookup_ = nullptr;
    void* toonLookupUser_ = nullptr;

    // Active visual effects pools
    struct ActiveProp {
        bool active = false;
        uint32_t actorId = 0;
        bool isToon = true;
        int track = 0;
        int level = 0;
        uint32_t startMs = 0;
        uint32_t releaseMs = 0;
        uint32_t endMs = 0;
        uint32_t targetId = 0;
    };
    static constexpr int kMaxProps = 4;
    ActiveProp props_[kMaxProps] = {};

    struct ActiveProjectile {
        bool active = false;
        int track = 0;
        int level = 0;
        ProjectileArc arc{};
        uint32_t targetId = 0;
        uint32_t attackerId = 0;
        bool hit = true;
        int hp = 0;
        bool soundPlayed = false;
    };
    static constexpr int kMaxProjectiles = 4;
    ActiveProjectile projectiles_[kMaxProjectiles] = {};

    struct ActiveSplat {
        bool active = false;
        uint32_t targetId = 0;
        float x = 0.0f, y = 0.0f, z = 0.0f;
        float h = 0.0f;
        int track = 0;
        int level = 0;
        uint32_t startMs = 0;
        uint32_t durMs = 500;
        bool isSplash = false;
    };
    static constexpr int kMaxSplats = 4;
    ActiveSplat splats_[kMaxSplats] = {};

    struct ActiveKapow {
        bool active = false;
        uint32_t cogId = 0;
        float x = 0.0f, y = 0.0f, z = 0.0f;
        float h = 0.0f;
        uint32_t startMs = 0;
        uint32_t durMs = 600;
        bool playedAudio = false; // ENC_cogfall_apart (0.4 s before the burst)
        bool playedSpin = false;  // Cog_Death spin (0.8 s into the lose animation)
        float height = 0.0f;      // Suit.height: the burst sits at height - 0.2
    };
    static constexpr int kMaxKapows = 4;
    ActiveKapow kapows_[kMaxKapows] = {};

    // JNT cache
    struct JntSlot {
        char path[80] = {};
        JntTrack track;
        uint32_t lastUsed = 0;
    };
    static constexpr int kMaxJntSlots = 6;
    JntSlot jntSlots_[kMaxJntSlots] = {};
    JntTrack* getJnt(const char* path);

    // SFX pool (bounded cache per Rule 8)
    struct SfxSlot {
        char name[32] = {};
        audio::SfxId id = audio::kInvalidSfx;
        audio::VoiceHandle voice = audio::kNoVoice;
        uint32_t lastUsed = 0;
    };
    static constexpr int kMaxSfxSlots = 6;
    SfxSlot sfxSlots_[kMaxSfxSlots] = {};

    uint32_t nowMs_ = 0;
    bool modelsLoaded_ = false;

    BattleMovies movies_;

    render::Model* getPropModel(int track, int level);
};

#endif // __3DS__

} // namespace tt::world
