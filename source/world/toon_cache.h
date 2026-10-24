// Shared GPU resources for toons, keyed by DNA: body parts (model + clips) and clothing
// textures are loaded once and shared by every toon that uses them.
//
// Files (tools/pipeline/bake_toon.py): <part>_<code>_<lod>.ttm (+ _<anim>.tta),
// lashes_<code>.ttm, clothes.ttx. Looked up on the SD set first
// (sdmc:/3ds/toontown3ds/data/toon/), then the RomFS boot set (romfs:/baked/toon/).
//
// Threading: a loader thread (priority below the main thread) does ALL linear-memory
// allocation and freeing for toons after boot, because libctru's linearAlloc is not
// thread-safe. The main thread only books slots under `lock_` (never held during IO)
// and draws looks once `ToonLook::ready()`.
#pragma once

#include <3ds.h>
#include <citro3d.h>

#include <cstdio>

#include "render/anim.h"
#include "render/model.h"
#include "world/toon_dna.h"
#include "world/toon_anim.h"

namespace tt::world {

constexpr int kToonAnimCount = kAnimCount; // matches ToonAnim::kAnimCount (toon_avatar.h)

enum class SlotState : u8 {
    Free,   // no key, no memory
    Queued, // key set; the loader will (unload old data and) load it
    Ready,  // loaded; drawable
    Failed, // file missing/corrupt; key kept so we don't retry every frame
};

// One baked body part (legs, torso, head or lashes) at one LOD.
struct ToonPart {
    char key[24] = {}; // e.g. "torso_ms_500"
    render::Model model;
    render::Anim neutral; // two neutral keys, including the required child attachment
    int refs = 0;
    u32 lastUsed = 0;
    bool animated = false;
    volatile SlotState state = SlotState::Free;
    u32 bytes = 0; // conservative allocated bytes, including mips/alignment
    u32 retiredFrame = 0;
};

struct ToonClothing {
    u16 texIndex = 0xFFFF;
    C3D_Tex tex{};
    int refs = 0;
    u32 lastUsed = 0;
    u32 retiredFrame = 0, bytes = 0;
    volatile SlotState state = SlotState::Free;
};

enum class ToonLod : u8 { High = 0, Low = 1 }; // LOD 500 / 250

// Everything needed to draw one toon. Owned by the cache; held via acquire/release.
class ToonCache;

struct ToonLook {
    ToonCache* cache = nullptr;
    ToonPart* legs = nullptr;
    ToonPart* torso = nullptr;
    ToonPart* head = nullptr;
    ToonPart* lashes = nullptr; // females only
    ToonClothing* clothes[render::kTexSlotCount] = {};
    render::Tints tints{};
    float scale = 1.0f;         // OTPGlobals.toonBodyScales
    float nametagHeight = 4.2f; // toonHeight + 0.5 (Avatar.py), world units
    bool valid() const { return legs && torso && head; }
    // All required parts and clothing loaded (lashes/clothing that failed are skipped).
    bool ready() const;
    // Per-texSlot textures for Renderer::drawModel/drawMorph (null = batch's own).
    void slotTextures(const C3D_Tex* out[render::kTexSlotCount]) const;
};

class ToonCache {
public:
    static constexpr u32 kBudgetBytes = 4u * 1024 * 1024; // AGENTS.md: toons sub-budget (linear)
    static constexpr int kMaxParts = 48;
    static constexpr int kMaxClothes = 48;

    // Opens the clothing pack and starts the loader thread.
    bool init();
    // Loads the default toon and waits for it (call once after init()).
    bool initFallback();
    void shutdown();
    // Always-resident default toon, drawn while a toon's own parts are still loading.
    const ToonLook& fallback() const { return fallback_; }

    // Books the parts/clothing for a DNA into `out` (previous contents released).
    // Loading happens on the loader thread; draw fallback() until out->ready().
    // Returns false (out left empty) when a part is known missing or tables are full.
    bool acquire(const ToonDna& dna, ToonLod lod, ToonLook* out);
    void release(ToonLook* look);

    // Main thread only, right after C3D_FrameBegin(C3D_FRAME_SYNCDRAW) and before any toon is
    // drawn: FrameBegin has waited for the previous frame's GPU queue, so this is the fence for
    // recycling and for publishing complete streamed poses. (It must not C3D_FrameSync itself:
    // that is a second vsync wait per frame and halves the frame rate.)
    void pump();
    struct Pose {
        const render::Anim* parts[3] = {};
        float blend[3] = {};
    };
    // Fixed owner slots; identical body-part/model resources stay shared by looks.
    bool sample(const void* owner, const ToonLook& look, int clip, float time, bool loop, Pose* pose);
    void forget(const void* owner);
    static constexpr int kStreamGroups = 12;
    static constexpr u32 kStreamVertices = 512;
    static constexpr u32 kStreamBytes = kStreamGroups*3*2*2*kStreamVertices*8;
    u32 linearBytes() const;
    int partsLoaded() const;
    int pendingLoads() const;

private:
    ToonPart* bookPart(const char* key, bool animated);
    ToonClothing* bookClothing(u8 table, u8 index);
    static void loaderMain(void* arg);
    bool loaderStep(); // one unit of work; false when idle
    void loadPart(ToonPart& p);
    void loadClothing(ToonClothing& c);
    bool admit(u32 bytes);
    bool streamStep();
    bool findFile(const char* file, char* out, size_t outSize) const;

    enum class StreamState : u8 { Idle, Queued, Loading, Staged };
    struct Stream {
        const void* owner = nullptr;
        char key[3][24] = {};
        u32 vertices[3] = {};
        bool animated[3] = {}, activeFallback[3] = {}, stagedFallback[3] = {};
        render::Anim active[3], staging[3];
        FILE* files[3] = {};
        char fileNames[3][72] = {};
        bool fileFallback[3] = {};
        float time = 0, shownTime = 0;
        bool loop = true, shownLoop = true, valid = false;
        int clip = 0;
        u32 generation = 0, loadedGeneration = 0, usedFrame = 0;
        StreamState state = StreamState::Idle;
    };
    Stream streams_[kStreamGroups];
    u32 frame_ = 1, streamBytes_ = 0;
    ToonPart parts_[kMaxParts];
    ToonClothing clothes_[kMaxClothes];
    ToonLook fallback_;
    // clothes.ttx: map + texture table in heap; textures read by seeking (loader only).
    FILE* clothesFile_ = nullptr;
    u16 clothesCounts_[4] = {};
    u16* clothesMap_ = nullptr;
    render::TexEntry* clothesEntries_ = nullptr;
    u16 numClothesTex_ = 0;

    mutable LightLock lock_;
    LightEvent wake_;
    Thread loader_ = nullptr;
    volatile bool quit_ = false;
    u32 clock_ = 0;
};

} // namespace tt::world
