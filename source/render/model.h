// Runtime side of the TTM model format (writer: tools/pipeline/ttm.py).
#pragma once

#include <3ds.h>
#include <citro3d.h>

#include <cstdio>

namespace tt::render {

// Tint slots baked by tools/pipeline/bake_toon.py.
enum TintSlot : u8 {
    kTintNone = 0,
    kTintHead,
    kTintArm,
    kTintGlove,
    kTintLeg,
    kTintShirt,
    kTintSleeve,
    kTintBottom,
    kTintCount
};

// Texture slots (ttm batch entry texSlot): which per-draw texture replaces a batch's own
// texture. Mirrors tt::world::TexSlot (toon_dna.h) and bake_toon.py TEX_SLOT_*.
constexpr u32 kTexSlotCount = 5; // 4: a skelecog's department tie (world/suits.*)

enum BlendMode : u8 { kBlendOpaque = 0, kBlendAlphaTest = 1, kBlendAlpha = 2 };

struct Batch {
    u16 texture; // 0xFFFF = untextured
    u8 blend;
    u8 flags; // bit0: two-sided
    u8 tint;
    u8 layer;
    u8 texSlot; // 0 = own texture; 1 shirt/blazer, 2 sleeve, 3 bottom/legs, 4 tie (Renderer slotTex override). Older files: 0.
    u8 pad;
    u32 firstVertex;
    u32 numVertices;
    u32 firstIndex;
    u32 numIndices;
};
static_assert(sizeof(Batch) == 24, "must match ttm.py batch entry");

// Texture table entry of .ttm files, also used by .ttx texture packs (tools/pipeline/ttm.py).
struct TexEntry {
    u16 width;
    u16 height;
    u8 format;
    u8 mipLevels;
    u8 wrapS;
    u8 wrapT;
    u32 dataOffset; // from the start of the file
    u32 dataSize;
    char name[32];
};
static_assert(sizeof(TexEntry) == 48, "must match ttm.py texture entry");

// Creates `tex` in linear memory and reads its pre-tiled data from `f` (filters and wrap
// set). On failure `tex` is left deleted/zeroed.
bool loadTexture(FILE* f, const TexEntry& e, C3D_Tex* tex);

struct Tints {
    float rgba[kTintCount][4];
    void setAll(float r, float g, float b);
};

class Model {
public:
    Model() = default;
    ~Model();
    Model(const Model&) = delete;
    Model& operator=(const Model&) = delete;

    // Loads from a path ("romfs:/baked/x.ttm" or "sdmc:/..."). Vertex/index/texture
    // data is read straight into linear memory; only the small tables use the heap.
    bool load(const char* path);
    // Loads a TTM embedded at byte `base` of an open file (e.g. a .tz chunk). Leaves the
    // file open; `label` is used in log messages. `pool` is the open textures.ttp that a
    // pooled (v2) model reads its texture data from; v2 models fail without it.
    bool loadFrom(FILE* f, u32 base, const char* label = nullptr, FILE* pool = nullptr);
    void unload();
    bool loaded() const { return vertices_ != nullptr; }
    // Exchanges contents (no GPU work): lets a loader thread fill a staging model that
    // the main thread then swaps in.
    void swap(Model& other);

    // Dequantization: world = q * posScale + posOffset.
    const float* posScale() const { return posScale_; }
    const float* posOffset() const { return posOffset_; }
    const float* boundsMin() const { return boundsMin_; }
    const float* boundsMax() const { return boundsMax_; }

    u32 numBatches() const { return numBatches_; }
    const Batch& batch(u32 i) const { return batches_[i]; }
    C3D_Tex* texture(u16 i) { return i < numTextures_ ? &textures_[i] : nullptr; }
    const void* vertices() const { return vertices_; }
    const u16* indices() const { return indices_; }
    u32 numTriangles() const { return numIndices_ / 3; }
    u32 numVertices() const { return numVertices_; }

    u32 linearBytes() const { return linearBytes_; }
    // True when indices index the whole vertex array. Bake-time header flags bit0
    // already stored them that way; otherwise load rebases batch-relative indices.
    bool absoluteIndices() const { return absoluteIndices_; }

private:
    u16 numTextures_ = 0;
    u16 numBatches_ = 0;
    u32 numVertices_ = 0;
    u32 numIndices_ = 0;
    float posScale_[3] = {};
    float posOffset_[3] = {};
    float boundsMin_[3] = {};
    float boundsMax_[3] = {};
    C3D_Tex* textures_ = nullptr;
    Batch* batches_ = nullptr;
    void* vertices_ = nullptr;
    u16* indices_ = nullptr;
    u32 linearBytes_ = 0;
    bool absoluteIndices_ = false;

    friend class ModelStaging;
};

// File bytes for one embedded TTM. A worker thread may read(); only the main thread
// calls pump(), which uploads GPU resources in time slices. Model::loadFrom stays the
// one-shot path (same signature callers already use).
class ModelStaging {
public:
    enum class Result : u8 { Pending, Complete, Error };

    ModelStaging() = default;
    ~ModelStaging();
    ModelStaging(const ModelStaging&) = delete;
    ModelStaging& operator=(const ModelStaging&) = delete;

    void reset();
    // Heap only: no linearAlloc and no C3D. `pool` is the textures.ttp a v2 model reads.
    bool read(FILE* f, u32 base, FILE* pool = nullptr);
    bool pending() const { return stage_ != Stage::Empty && stage_ != Stage::Done; }
    // Uploads into `dst` until `budgetUs` microseconds have been spent or the model is
    // finished. One step may run past the budget; the next call stops first if it is used up.
    Result pump(Model& dst, u32 budgetUs);

private:
    enum class Stage : u8 { Empty, Copy, Rebase, Flush, Textures, Done };
    Stage stage_ = Stage::Empty;
    u16 version_ = 0;
    u16 numTextures_ = 0;
    u16 numBatches_ = 0;
    u32 numVertices_ = 0;
    u32 numIndices_ = 0;
    float posScale_[3] = {};
    float posOffset_[3] = {};
    Batch* batches_ = nullptr;
    TexEntry* tex_ = nullptr;
    u8* verts_ = nullptr;
    u16* indices_ = nullptr;
    u8* texBytes_ = nullptr;
    u32* texOff_ = nullptr; // byte offset of each texture in texBytes_
    u32 vertBytes_ = 0;
    u32 indexBytes_ = 0;
    u32 copiedV_ = 0;
    u32 copiedI_ = 0;
    u32 flushedV_ = 0;
    u32 flushedI_ = 0;
    u16 rebased_ = 0;
    u32 rebasedAt_ = 0; // indices of batches_[rebased_] already rewritten
    u16 texDone_ = 0;
    u32 texFilled_ = 0; // bytes of the current texture already copied
    bool texReady_ = false; // C3D_Tex for texDone_ is allocated
    bool rebaseOk_ = true;
    bool absolute_ = false; // header flags bit 0: indices already address the whole vertex array
};

} // namespace tt::render
