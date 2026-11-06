// Host stub of render/model.h for the GUI panel tests: a Model that "loads" any atlas file
// by handing out a shared fake texture, so the panels take their real atlas draw paths
// (laff meter parts, friends panel art) instead of the missing-file fallbacks, and the
// recorded sub-texture UVs can be checked against the real atlas tables.
#pragma once

#include "citro3d.h"
#include "core/log.h"

#include <cstdint>
#include <cstdio>

namespace tt::render {

// Tint slots (render/model.h): only the count matters on the host.
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

// Texture slots (ttm batch entry texSlot); mirrors tt::world::TexSlot (toon_dna.h).
constexpr u32 kTexSlotCount = 9;

struct Tints {
    float rgba[kTintCount][4] = {};
    void setAll(float r, float g, float b) {
        for (u32 i = 0; i < kTintCount; ++i) {
            rgba[i][0] = r;
            rgba[i][1] = g;
            rgba[i][2] = b;
            rgba[i][3] = 1.0f;
        }
    }
};

struct TexEntry {
    char name[32] = {};
    u32 dataOffset = 0, dataSize = 0;
    u16 width = 0, height = 0;
    u16 format = 0;
    u16 mipLevels = 0;
    u16 wrapS = 0, wrapT = 0;
};

inline C3D_Tex gFakeTex;
inline bool loadTexture(FILE*, const TexEntry&, C3D_Tex* tex) {
    *tex = C3D_Tex{};
    return tex != nullptr;
}

class Model {
public:
    Model() = default;
    ~Model() { unload(); }
    Model(const Model&) = delete;
    Model& operator=(const Model&) = delete;

    // Any romfs path "succeeds": the panels draw all their atlas parts, and the host
    // recorder keeps the UV rectangles the code computed.
    bool load(const char* path) {
        (void)path;
        loaded_ = true;
        numTextures_ = 2; // atlas pages the sources index (textures 0 and 1)
        return true;
    }
    void unload() { loaded_ = false; }
    bool loaded() const { return loaded_; }
    C3D_Tex* texture(u16 i) { return i < numTextures_ ? &gFakeTex : nullptr; }
    const float* boundsMin() const { return boundsMin_; }
    const float* boundsMax() const { return boundsMax_; }

private:
    bool loaded_ = false;
    float boundsMin_[3] = {0, 0, 0};
    float boundsMax_[3] = {1, 1, 1};
    u16 numTextures_ = 0;
};

} // namespace tt::render
