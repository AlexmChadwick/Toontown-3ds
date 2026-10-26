#pragma once
#include "world/asset_batch.h"

#include <3ds.h>
#include <citro3d.h>

#include "game/ring_logic.h"
#include "net/client.h"
#include "render/model.h"
#include "render/renderer.h"

namespace tt::game {

class RingGame {
public:
    void pumpAssets() {
        assetBatch_.pump();
    }
    bool loadingAssets() const {
        return assetBatch_.loading();
    }
    void init(net::Client& client, uint32_t nowMs, bool autoPlay);
    void shutdown();
    void update(net::Client& client, const circlePosition& pad, u32 kDown, float dt, uint32_t nowMs);
    void draw(render::Renderer& renderer);

    void toonPose(float* x, float* y, float* z, float* h) const;
    bool toonRunning() const { return true; } // swimming forward
    void camera(C3D_FVec* eye, C3D_FVec* target) const;

    const char* line(int i) const { return (i >= 0 && i < 3) ? lines_[i] : ""; }
    bool isDone() const { return done_; }

    // DistributedRingGame: FAR_PLANE_DIST 150, the water background and linear fog WATER_COLOR
    // (0, 0, 0.6) from 0.1 to FAR_PLANE_DIST - 1, applied by MinigameSession::applyWorld.
    static constexpr float kFarPlane = 150.0f;
    static constexpr uint32_t kWaterBackground = 0x000099FF; // RGBA8 of (0, 0, 0.6, 1)
    static constexpr float kWaterColor[3] = {0.0f, 0.0f, 0.6f};
    static constexpr float kFogStart = 0.1f;
    static constexpr float kFogEnd = kFarPlane - 1.0f;

private:
    world::BasicAssetBatch<8> assetBatch_;
    bool done_ = false;
    bool autoPlay_ = false;
    uint32_t startMs_ = 0;

    int slot_ = 0;
    int numPlayers_ = 1;
    int ringsCollected_ = 0;
    bool perfect_ = true;
    int nextGroup_ = 0;

    float toonX_ = 0.0f;
    float toonZ_ = 0.0f;
    float toonH_ = 0.0f;

    ringgame::RingGroupData rings_[ringgame::kNumRingGroups];
    bool groupPassed_[ringgame::kNumRingGroups] = {};
    bool groupHit_[ringgame::kNumRingGroups] = {};

    render::Model environ_;
    render::Model ringModel_;

    char lines_[3][64] = {};
};

} // namespace tt::game
