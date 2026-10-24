#pragma once

#include <3ds.h>
#include <citro3d.h>

#include "game/pattern_logic.h"
#include "net/client.h"
#include "render/model.h"
#include "render/renderer.h"

namespace tt::game {

class PatternGame {
public:
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

    void onSetPattern(const uint8_t* pattern, int len);

private:
    patterngame::Simulation sim_;
    float timer_ = 0.0f;
    float roundTime_ = 0.0f;
    bool waitingForPattern_ = true;
    bool patternSubmitted_ = false;

    render::Model stageModel_;

    mutable char lines_[3][64] = {};
};

} // namespace tt::game
