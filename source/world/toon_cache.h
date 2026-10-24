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

namespace tt::world {

constexpr int kToonAnimCount = 5; // matches ToonAnim::kAnimCount (toon_avatar.h)

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
    render::Anim anim[kToonAnimCount]; // missing clips stay unloaded (static heads)
    int refs = 0;
    u32 lastUsed = 0;
    bool animated = false;
    volatile SlotState state = SlotState::Free;
    u32 bytes = 0; // linear bytes, set by the loader
};

struct ToonClothing {
    u16 texIndex = 0xFFFF;
    C3D_Tex tex{};
    int refs = 0;
    u32 lastUsed = 0;
    volatile SlotState state = SlotState::Free;
};

enum class ToonLod : u8 { High = 0, Low = 1 }; // LOD 500 / 250

// Everything needed to draw one toon. Owned by the cache; held via acquire/release.
struct ToonLook {
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
    void enforceBudget();
    bool findFile(const char* file, char* out, size_t outSize) const;

    ToonPart parts_[kMaxParts];
    ToonClothing clothes_[kMaxClothes];
    ToonLook fallback_;
    // clothes.ttx: map + texture table in heap; textures read by seeking (loader only).
    FILE* clothesFile_ = nullptr;
    u16 clothesCounts_[4] = {};
    u16* clothesMap_ = nullptr;
    render::TexEntry* clothesEntries_ = nullptr;
    u16 numClothesTex_ = 0;

    LightLock lock_;
    LightEvent wake_;
    Thread loader_ = nullptr;
    volatile bool quit_ = false;
    u32 clock_ = 0;
};

} // namespace tt::world
