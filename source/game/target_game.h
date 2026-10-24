#pragma once

#include <3ds.h>
#include <citro3d.h>

#include "game/target_logic.h"
#include "net/client.h"
#include "render/model.h"
#include "render/renderer.h"

namespace tt::game {

class TargetGame {
public:
    TargetGame() = default;

    bool init(const char* root, uint32_t seed, uint32_t safezone);
    void shutdown();

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
    targetgame::FlightState flight_;
    targetgame::TargetRing targets_[targetgame::kMaxTargets];
    int numTargets_ = 0;
    int score_ = 0;
    int round_ = 1;
    bool isDone_ = false;
    bool scoreReported_ = false;

    render::Model groundModel_;
    render::Model slingModel_;
    render::Model trampModel_;

    mutable char lines_[3][64] = {};
};

} // namespace tt::game
