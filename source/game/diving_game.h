#pragma once
#include "world/asset_batch.h"

#include <3ds.h>
#include <citro3d.h>

#include "game/diving_logic.h"
#include "net/client.h"
#include "render/model.h"
#include "render/renderer.h"

namespace tt::game {

class DivingGame {
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
    bool toonRunning() const { return true; } // swimming animation
    void camera(C3D_FVec* eye, C3D_FVec* target) const;

    // fishSpawn: the AI spawned fish `fishCode` at `spawnerId` with lane offset `offset`.
    void spawnFish(uint32_t fishCode, uint32_t spawnerId, uint32_t offset);

    const char* line(int i) const { return (i >= 0 && i < 3) ? lines_[i] : ""; }
    bool isDone() const { return done_; }

private:
    static constexpr int kMaxFish = 24;
    static constexpr int kFishTypeCount = 6;

    world::BasicAssetBatch<12> assetBatch_;
    // A fish the AI spawned with fishSpawn (DistributedDivingGameAI.spawnFish). It crosses the
    // tank from its spawner (DistributedDivingGame.fishSpawn) and is removed when its lerp ends.
    struct Fish {
        bool active = false;
        uint8_t type = 0;    // fishcode: 0 clown, 1 PBJ, 2 bear, 3 balloon, 4 nurse, 5 piano
        uint8_t spawner = 0;
        uint16_t id = 0;     // the spawner's running count: the AI's fishArray key
        int dir = 1;
        float offset = 0.0f; // fishSpawn lane offset
        float elapsed = 0.0f;
        float x = 0.0f;
        float z = 0.0f;
    };

    bool done_ = false;
    bool autoPlay_ = false;
    uint32_t startMs_ = 0;
    float timeRemaining_ = diving::kGameDuration;

    diving::SwimmerState swimmer_;
    diving::ChestState chests_[3];
    int recoveries_ = 0;
    int score_ = 5;

    Fish fish_[kMaxFish];
    uint16_t spawnCount_[diving::kNumSpawners] = {};
    float crabX_ = 0.0f;
    int crabDir_ = 1;

    char lines_[3][48] = {};

    render::Model environModel_;
    render::Model boatModel_;
    render::Model chestModel_;
    render::Model fishModel_[kFishTypeCount];
};

} // namespace tt::game
