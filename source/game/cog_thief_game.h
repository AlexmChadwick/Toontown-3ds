#pragma once

#include "game/minigame_cog.h"
#include "world/asset_batch.h"

#include <3ds.h>
#include <citro3d.h>

#include "game/cog_thief_logic.h"
#include "net/client.h"
#include "render/model.h"
#include "render/renderer.h"

namespace tt::game {

class CogThiefGame {
public:
    void pumpAssets() {
        assetBatch_.pump();
    }
    bool loadingAssets() const {
        return assetBatch_.loading();
    }
    CogThiefGame() = default;

    bool init(const char* root, int numCogs = 5, float cogSpeed = cogthief::kCogSpeedTTC);
    void shutdown();

    void update(net::Client& client, const circlePosition& pad, u32 keysDown, float dt, u32 nowMs);
    void draw(render::Renderer& renderer);

    void camera(C3D_FVec* eye, C3D_FVec* target) const;
    void toonPose(float* x, float* y, float* z, float* h) const;
    bool isDone() const { return isDone_; }
    // DistributedCogThiefGame.loadCogs: each Cog's suit type from the game's RandomNumGen(doId).
    void chooseSuitTypes(uint32_t gameDoId);
    int cogs(MinigameCog* out, int max) const;
    int score() const { return score_; }

    const char* title() const { return "Cog Thief"; }
    const char* line(int i) const;

private:
    world::BasicAssetBatch<8> assetBatch_;
    cogthief::Simulation sim_;
    char suitTypes_[cogthief::kMaxCogs][4] = {};
    float cogHeading_[cogthief::kMaxCogs] = {};
    cogthief::Vec3 lastCogPos_[cogthief::kMaxCogs] = {};
    float tx_ = 0.0f;
    float ty_ = 12.0f;
    float th_ = 180.0f;
    float timer_ = 0.0f;
    int score_ = 0;
    bool isDone_ = false;

    render::Model groundModel_;
    render::Model barrelModel_;
    render::Model tankModel_;

    mutable char lines_[3][64] = {};
};

} // namespace tt::game
