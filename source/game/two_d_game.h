#pragma once

#include <3ds.h>
#include <citro3d.h>

#include "game/two_d_logic.h"
#include "net/client.h"
#include "render/model.h"
#include "render/renderer.h"

namespace tt::game {

class TwoDGame {
public:
    TwoDGame() = default;

    bool init(const char* root);
    void shutdown();

    void update(net::Client& client, const circlePosition& pad, u32 keysDown, float dt, u32 nowMs);
    void draw(render::Renderer& renderer);

    void camera(C3D_FVec* eye, C3D_FVec* target) const;
    void toonPose(float* x, float* y, float* z, float* h) const;
    bool isDone() const { return isDone_; }
    int score() const { return score_; }

    const char* title() const { return "Toon Escape"; }
    const char* line(int i) const;

private:
    twodgame::PlayerPhysics player_;
    twodgame::AABB platforms_[8];
    int numPlatforms_ = 0;
    int rawScore_ = 0;
    int score_ = 0;
    float timer_ = 0.0f;
    bool completed_ = false;
    bool isDone_ = false;

    render::Model gameModel_;
    render::Model blockModel_;

    mutable char lines_[3][64] = {};
};

} // namespace tt::game
