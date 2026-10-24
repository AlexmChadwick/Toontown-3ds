// Runtime side of the TTM model format (writer: tools/pipeline/ttm.py).
#pragma once

#include <3ds.h>
#include <citro3d.h>

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

enum BlendMode : u8 { kBlendOpaque = 0, kBlendAlphaTest = 1, kBlendAlpha = 2 };

struct Batch {
    u16 texture; // 0xFFFF = untextured
    u8 blend;
    u8 flags; // bit0: two-sided
    u8 tint;
    u8 layer;
    u16 pad;
    u32 firstVertex;
    u32 numVertices;
    u32 firstIndex;
    u32 numIndices;
};
static_assert(sizeof(Batch) == 24, "must match ttm.py batch entry");

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
    void unload();
    bool loaded() const { return vertices_ != nullptr; }

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
    // True when indices index the whole vertex array (set at load if it fits in u16).
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
};

} // namespace tt::render
