#pragma once
#include "world/asset_batch.h"

#include <3ds.h>
#include <citro3d.h>

#include "game/target_logic.h"
#include "net/client.h"
#include "render/model.h"
#include "render/renderer.h"

namespace tt::game {

class TargetGame {
public:
    targetgame::FlightMode flightMode() const { return flight_.mode; }
    void pumpAssets() {
        assetBatch_.pump();
    }
    bool loadingAssets() const {
        return assetBatch_.loading();
    }
    TargetGame() = default;

    bool init(const char* root, uint32_t seed, uint32_t safezone);
    void shutdown();
    void nextRound();

    void update(net::Client& client, const circlePosition& pad, u32 keysDown, float dt, u32 nowMs);
    void draw(render::Renderer& renderer);

    void camera(C3D_FVec* eye, C3D_FVec* target) const;
    void toonPose(float* x, float* y, float* z, float* h) const;
    bool isDone() const { return isDone_; }
    int score() const { return score_; }
    int round() const { return round_; }

    const char* title() const { return "Toon Slingshot"; }
    const char* line(int i) const;

private:
    world::BasicAssetBatch<16> assetBatch_;
    targetgame::FlightState flight_;
    targetgame::TargetRing targets_[targetgame::kMaxTargets];
    int numTargets_ = 0;
    int score_ = 0;
    int round_ = 1;
    bool isDone_ = false;
    bool scoreReported_ = false;

    static constexpr int kRingTypes = 5; // 4 target types, then the jump circle
    render::Model skyModel_, sandModel_, mountModel_[2], cactusModel_[2], trampModel_;
    render::Model ringModel_[kRingTypes];
    targetgame::Field field_;
    int targetTypes_[targetgame::kMaxTargets] = {};

    mutable char lines_[3][64] = {};
};

} // namespace tt::game
