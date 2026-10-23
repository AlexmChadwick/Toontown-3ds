// Top-screen world renderer (citro3d). Owns the world shader, render targets and
// per-frame draw statistics. The bottom screen is drawn by ui:: with citro2d.
#pragma once

#include <3ds.h>
#include <citro3d.h>

#include "render/model.h"

namespace tt::render {

struct FrameStats {
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
    void drawModel(Model& model, const C3D_Mtx& world, const Tints* tints = nullptr);
    // Restores GPU state citro2d's C2D_Prepare does not reset (alpha test, depth map).
    void endWorld();

    const FrameStats& stats() const { return stats_; }
    void resetStats() { stats_ = FrameStats{}; }

    static constexpr float kNear = 1.0f;
    static constexpr float kFar = 300.0f;
    static constexpr float kFovYDeg = 55.0f;

private:
    bool sphereVisible(const C3D_FVec& center, float radius) const;

    DVLB_s* shaderDvlb_ = nullptr;
    shaderProgram_s program_{};
    int uProjection_ = -1;
    int uModelView_ = -1;
    int uTint_ = -1;

    C3D_RenderTarget* topLeft_ = nullptr;
    C3D_RenderTarget* topRight_ = nullptr;

    C3D_Mtx projection_{};
    C3D_Mtx view_{};
    C3D_FVec frustum_[6]{}; // world-space planes (xyz normal, w distance), inside >= 0
    FrameStats stats_{};
};

} // namespace tt::render
