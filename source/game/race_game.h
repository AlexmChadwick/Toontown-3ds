#pragma once

#include <3ds.h>
#include <citro3d.h>

#include "game/race_logic.h"
#include "net/client.h"
#include "render/model.h"
#include "render/renderer.h"

namespace tt::game {

class RaceGame {
public:
    RaceGame() = default;

    bool init(const char* root);
    void shutdown();

    void update(net::Client& client, const circlePosition& pad, u32 keysDown, float dt, u32 nowMs);
    void draw(render::Renderer& renderer);

    void camera(C3D_FVec* eye, C3D_FVec* target) const;
    void toonPose(float* x, float* y, float* z, float* h) const;
    bool isDone() const { return sim_.isDone; }
    int score() const { return sim_.scores[0]; }

    const char* title() const { return "Race Game"; }
    const char* line(int i) const;

    void onSetChancePositions(const uint8_t* positions, int count);
    void onSetServerChoices(const uint8_t* choices, const uint8_t* positions, const int16_t* rewards, int count);

private:
    racegame::Simulation sim_;
    int selectedChoice_ = 1;
    bool choiceSubmitted_ = false;
    float timer_ = 0.0f;

    render::Model trackModel_;

    mutable char lines_[3][64] = {};
};

} // namespace tt::game
