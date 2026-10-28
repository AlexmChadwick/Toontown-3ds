#pragma once

#include <cstdint>
#include <cstring>
#include "world/cog_attack_tables.h"

#ifdef __3DS__
#include <3ds.h>
#include <citro3d.h>
#include "audio/audio.h"
#include "render/anim.h"
#include "render/model.h"
#include "render/renderer.h"
#include "world/asset_batch.h"
#endif

namespace tt::world {

class Battle;

#ifdef __3DS__

// ----------------------------------------------------------------------
// Cog Attack Movie & Prop Player
// ----------------------------------------------------------------------
class CogAttacks {
public:
    CogAttacks() = default;
    ~CogAttacks() { shutdown(); }

    bool init();
    void shutdown();
    void reset();

    // Preload props for all active Cogs before the movie starts
    void preloadForBattle(const char* const cogCodes[], int numCogs);

    // Event hooks called from BattleFx
    void onAttack(uint32_t attackerId, uint32_t targetId, int attackIdx,
                  uint32_t startMs, uint32_t durMs, uint32_t hitMs, const Battle* battle);
    void onHit(uint32_t targetId, uint32_t attackerId, int attackIdx, int hp, bool miss,
               const Battle* battle);

    void update(float dt, uint32_t nowMs, const Battle* battle);
    void draw(render::Renderer& renderer, const Battle* battle);
    // After the GPU fence: publishes props streamed by the shared background loader.
    void pumpAssets() { assetBatch_.pump(); }

private:
    struct PropSlot {
        char name[32] = {};
        render::Model model;
        render::Anim anim;
        bool hasAnim = false; // an animated (actor) prop: its clip was requested too
        bool loaded = false;  // slot in use: requested (model.loaded() once published)
    };
    static constexpr int kMaxPropSlots = 14;
    PropSlot propSlots_[kMaxPropSlots] = {};
    BasicAssetBatch<2 * kMaxPropSlots> assetBatch_;

    PropSlot* findOrLoadProp(const char* propName, const char* propAnim);

    struct ActiveAttack {
        bool active = false;
        uint32_t attackerId = 0;
        uint32_t targetId = 0;
        int attackId = -1;
        uint32_t startMs = 0;
        uint32_t durMs = 0;
        uint32_t hitMs = 0;
        uint32_t releaseMs = 0;
        bool isThrow = false;
        bool holdLeft = false;
        PropSlot* prop = nullptr;

        // Trajectory
        float x0 = 0.0f, y0 = 0.0f, z0 = 0.0f;
        float x1 = 0.0f, y1 = 0.0f, z1 = 0.0f;

        // Sound cue
        char soundName[32] = {};
        uint32_t soundAtMs = 0;
        bool soundPlayed = false;
    };
    static constexpr int kMaxAttacks = 4;
    ActiveAttack attacks_[kMaxAttacks] = {};

    uint32_t nowMs_ = 0;

    // SFX bounded cache
    struct SfxSlot {
        char name[32] = {};
        audio::SfxId id = audio::kInvalidSfx;
        audio::VoiceHandle voice = audio::kNoVoice;
        uint32_t lastUsed = 0;
    };
    static constexpr int kMaxSfx = 8;
    static constexpr uint32_t kSfxCacheBytes = 512 * 1024; // linear bytes for Cog attack cues
    SfxSlot sfx_[kMaxSfx] = {};
    void playSound(const char* soundFile);
};

#else

// Host stub
class CogAttacks {
public:
    void reset() {}
    void preloadForBattle(const char* const[], int) {}
    void onAttack(uint32_t, uint32_t, int, uint32_t, uint32_t, uint32_t, const Battle*) {}
    void onHit(uint32_t, uint32_t, int, int, bool, const Battle*) {}
    void update(float, uint32_t, const Battle*) {}
    void pumpAssets() {}
};

#endif // __3DS__

} // namespace tt::world
