#pragma once

#include "world/asset_batch.h"

#include <3ds.h>
#include <citro3d.h>

#include "game/two_d_logic.h"
#include "net/client.h"
#include "render/model.h"
#include "render/renderer.h"

namespace tt::game {

class TwoDGame {
public:
    void pumpAssets() { assetBatch_.pump(); }
    bool loadingAssets() const { return assetBatch_.loading(); }
    bool init(const char* root, unsigned hood = 2000);
    void shutdown();
    void update(net::Client& client, const circlePosition& pad, u32 keysDown, float dt, u32 nowMs);
    void draw(render::Renderer& renderer);
    void camera(C3D_FVec* eye, C3D_FVec* target) const;
    void toonPose(float* x, float* y, float* z, float* h) const;
    bool isDone() const { return isDone_; }
    int score() const { return score_; }
    const char* title() const { return "Toon Escape"; }
    const char* line(int i) const;
    void setAutoPlay(bool enabled) { autoPlay_ = enabled; }
    void handleSetSections(const net::Client& client);
    void handleTreasureGrabbed(uint32_t avId, uint8_t secIdx, uint8_t trIdx);
    void handleEveryoneDone();
    void handleEnemyShot(uint8_t section, uint8_t enemy, uint32_t health);
    void handleVictoryScore(uint8_t score);

private:
    world::BasicAssetBatch<16> assetBatch_;
    twodgame::Course course_;
    twodgame::Simulation sim_;
    twodgame::Autopilot autopilot_;
    unsigned hood_ = 2000;
    float timer_ = 0.0f;
    int score_ = 0;
    int treasureCount_ = 0;
    bool completed_ = false;
    bool isDone_ = false;
    bool courseAssembled_ = false;
    bool autoPlay_ = false;
    render::Model blockModels_[4];
    render::Model wallModel_;
    render::Model pipeModel_;
    render::Model elevatorModel_;
    render::Model stomperModel_;
    render::Model arrowModel_;
    render::Model treasureModel_;
    render::Model suitPlaceholder_;
    mutable char lines_[3][64] = {};
};

} // namespace tt::game
