#pragma once
#include "world/asset_batch.h"

#include <3ds.h>
#include <citro3d.h>

#include "game/race_logic.h"
#include "game/race_tables.h"
#include "net/client.h"
#include "render/model.h"
#include "render/renderer.h"

namespace tt::game {

class RaceGame {
public:
    void pumpAssets() {
        assetBatch_.pump();
    }
    bool loadingAssets() const {
        return assetBatch_.loading();
    }
    RaceGame() = default;

    bool init(const char* root);
    void shutdown();

    void update(net::Client& client, const circlePosition& pad, u32 keysDown, float dt, u32 nowMs);
    void draw(render::Renderer& renderer);

    void camera(C3D_FVec* eye, C3D_FVec* target) const;
    void toonPose(float* x, float* y, float* z, float* h) const;
    void setLane(int lane) { lane_ = lane; }
    bool isDone() const { return sim_.isDone; }
    int score() const { return sim_.scores[0]; }
    bool toonRunning() const { return moveTime_ < moveDuration_; }

    const char* title() const { return "Race Game"; }
    const char* line(int i) const;

    void onSetChancePositions(const uint8_t* positions, int count);
    void onSetServerChoices(const uint8_t* choices, const uint8_t* positions, const int16_t* rewards, int count);

private:
    world::BasicAssetBatch<8> assetBatch_;
    racegame::Simulation sim_;
    int selectedChoice_ = 1;
    bool choiceSubmitted_ = false;
    float timer_ = 0.0f;
    int displayedPosition_ = 0;
    int moveStartPosition_ = 0;
    int moveEndPosition_ = 0;
    float moveTime_ = 0.0f;
    float moveDuration_ = 0.0f;

    render::Model trackModel_;

    mutable char lines_[3][64] = {};
    int lane_ = 0; // our avIdList index: the lane we run in
    int numRacers_ = 1;
    // DistributedRaceGame.moveCamera: eased (posQuatInterval 0.75 s) from the last view.
    void moveCamera();
    float camFrom_[6] = {}; // eye xyz, look-at xyz
    float camTo_[6] = {};
    float camTime_ = 0.0f;
};

} // namespace tt::game
