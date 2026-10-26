#pragma once
#include "world/asset_batch.h"

#include <3ds.h>
#include <citro3d.h>

#include "game/pattern_logic.h"
#include "net/client.h"
#include "render/anim.h"
#include "render/model.h"
#include "render/renderer.h"

namespace tt::game {

class PatternGame {
public:
    void pumpAssets() {
        assetBatch_.pump();
    }
    bool loadingAssets() const {
        return assetBatch_.loading();
    }
    PatternGame() = default;

    bool init(const char* root);
    void shutdown();

    void update(net::Client& client, const circlePosition& pad, u32 keysDown, float dt, u32 nowMs);
    void draw(render::Renderer& renderer);

    void camera(C3D_FVec* eye, C3D_FVec* target) const;
    void toonPose(float* x, float* y, float* z, float* h) const;
    bool isDone() const { return sim_.isDone; }
    int score() const { return sim_.score; }

    const char* title() const { return "Match Minnie"; }
    const char* line(int i) const;

    void onResults() { readySent_ = false; }
    void setAutoPlay(bool value) { autoPlay_ = value; }
    void onSetPattern(const uint8_t* pattern, int len);

private:
    world::BasicAssetBatch<8> assetBatch_;
    patterngame::Simulation sim_;
    float timer_ = 0.0f;
    float roundTime_ = 0.0f;
    bool autoPlay_ = false;
    bool readySent_ = false;
    bool waitingForPattern_ = true;
    bool patternSubmitted_ = false;

    // Minnie performing each round's pattern (DistributedPatternGame.enterShowServerPattern).
    float showStepDuration() const;
    bool showing() const;

    render::Model stageModel_;
    render::Model minnieModel_;
    render::Anim minnieClips_[4]; // minnieAnimNames: up, left, down, right
    float showTime_ = -1.0f;      // seconds since the round's pattern arrived; -1 = no show

    mutable char lines_[3][64] = {};
};

} // namespace tt::game
