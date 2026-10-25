// Top-screen world renderer (citro3d). Owns the world shader, render targets and
// per-frame draw statistics. The bottom screen is drawn by ui:: with citro2d.
#pragma once

#include <3ds.h>
#include <citro3d.h>

#include "render/anim.h"
#include "render/hot_paths.h"
#include "render/model.h"

namespace tt::render {

struct FrameStats {
    u32 uniformUploads = 0; // API uploads, including matrices
    u32 textureBinds = 0;
    u32 bufferBinds = 0;
    u32 drawCalls = 0;
    u32 triangles = 0;
    u32 modelsDrawn = 0;
    u32 modelsCulled = 0;
    u32 modelsSkipped = 0; // dropped because the GPU command buffer was nearly full
};

class Renderer {
public:
    bool init();
    void shutdown();

    C3D_RenderTarget* topLeft() const { return topLeft_; }
    C3D_RenderTarget* topRight() const { return topRight_; }

    // Clears and binds a top-screen target and the world pipeline. eye: 0 = mono,
    // -1/+1 = left/right eye with interocular distance `iod` (stereo is opt-in).
    void beginWorld(C3D_RenderTarget* target, const C3D_FVec& eye, const C3D_FVec& lookAt, float iod,
                    int eyeSign);
    const C3D_FVec& eye() const { return eye_; } // camera position of the current world pass (billboards)
    // slotTex (optional, kTexSlotCount entries): per-draw textures for batches whose
    // texSlot is non-zero (toon clothing). A null entry keeps the batch's own texture.
    void drawModel(Model& model, const C3D_Mtx& world, const Tints* tints = nullptr,
                   const C3D_Tex* const* slotTex = nullptr);
    // Vertex-animated part: blends anim frames a and b (weight `blend` on b).
    void drawMorph(Model& model, const Anim& anim, u32 frameA, u32 frameB, float blend, const C3D_Mtx& world,
                   const Tints* tints = nullptr, const C3D_Tex* const* slotTex = nullptr);
    // Colour multiplier for batches without a tint slot (default white). Used e.g. to draw
    // the drop shadow card black and translucent.
    void setBaseTint(float r, float g, float b, float a) {
        baseTint_[0] = r;
        baseTint_[1] = g;
        baseTint_[2] = b;
        baseTint_[3] = a;
    }
    // Restores GPU state citro2d's C2D_Prepare does not reset (alpha test, depth map, fog).
    void endWorld();
    // Linear distance fog for the world pass (Panda Fog.setLinearRange(start, end), colour
    // 0..1); the sky clear takes the fog colour too, as upstream fogs the sky. Applied from the
    // next beginWorld until clearFog.
    // Clear colour where no geometry covers the screen (sky blue outdoors, the default
    // grey indoors); fog replaces it with the fog colour.
    void setBackground(u32 rgba8) { background_ = rgba8; }
    void setFog(float r, float g, float b, float start, float end);
    void clearFog() { fogOn_ = false; }

    const FrameStats& stats() const { return stats_; }
    void resetStats() { stats_ = FrameStats{}; }

    static constexpr float kNear = 1.0f;
    static constexpr float kFar = 300.0f;
    static constexpr float kFovYDeg = 55.0f;

private:
    enum Mode : int { kModeStatic = 0, kModeMorph = 1, kModeCount = 2, kModeNone = -1 };
    struct Program {
        DVLB_s* dvlb = nullptr;
        shaderProgram_s program{};
        int uProjection = -1, uModelView = -1, uTint = -1, uBlend = -1;
    };

    static bool loadProgram(const u8* shbin, u32 size, Program* out);
    void bindMode(Mode mode);
    // Culls with a bounding sphere; `world` may contain a uniform scale <= 1 (toon body
    // scale), which only makes the sphere conservative.
    bool beginDraw(const float* boundsMin, const float* boundsMax, const C3D_Mtx& world);
    void setModelView(const C3D_Mtx& world, const float* posScale, const float* posOffset);
    void drawBatches(Model& model, const Tints* tints, const C3D_Tex* const* slotTex, bool perBatchBuffer);
    bool sphereVisible(const C3D_FVec& center, float radius) const;

    float baseTint_[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    u32 background_ = 0x6AB0D9FF; // RGBA8 clear colour without fog (setBackground)
    bool fogOn_ = false;
    u32 fogColor_ = 0;      // 0x00BBGGRR (GPUREG_FOG_COLOR)
    u32 fogClear_ = 0;      // the same colour as an RGBA8 clear value
    float fogStart_ = 0.0f, fogEnd_ = 0.0f;
    C3D_FogLut fogLut_{};
    Program programs_[kModeCount];
    int mode_ = kModeNone;

    C3D_RenderTarget* topLeft_ = nullptr;
    C3D_RenderTarget* topRight_ = nullptr;

    C3D_Mtx projection_{};
    C3D_Mtx view_{};
    C3D_FVec eye_{};
    C3D_FVec frustum_[6]{}; // world-space planes (xyz normal, w distance), inside >= 0
    FrameStats stats_{};
    BatchState batchState_{};
};

} // namespace tt::render
