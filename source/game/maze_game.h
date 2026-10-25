#pragma once
#include "world/asset_batch.h"

#include <3ds.h>
#include <citro3d.h>

#include "game/maze_logic.h"
#include "net/client.h"
#include "render/model.h"
#include "render/renderer.h"

namespace tt::game {

class MazeGame {
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
    bool toonRunning() const { return isRunning_; }
    void camera(C3D_FVec* eye, C3D_FVec* target) const;

    const char* line(int i) const { return (i >= 0 && i < 3) ? lines_[i] : ""; }
    bool isDone() const { return done_; }

private:
    world::BasicAssetBatch<8> assetBatch_;
    bool done_ = false;
    bool autoPlay_ = false;
    bool isRunning_ = false;
    uint32_t startMs_ = 0;
    float timeRemaining_ = maze::kGameDuration;

    float toonX_ = 0.0f;
    float toonY_ = 0.0f;
    float toonZ_ = 0.0f;
    float toonH_ = 180.0f;

    bool treasureTaken_[maze::kNumTreasures] = {};
    int treasuresCollected_ = 0;
    int score_ = 0;

    maze::SuitState suits_[maze::kNumSuits];
    float hitCooldown_ = 0.0f;

    char lines_[3][48] = {};

    render::Model mazeModel_;
    render::Model treasureModel_;
};

} // namespace tt::game
