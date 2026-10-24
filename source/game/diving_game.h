#pragma once

#include <3ds.h>
#include <citro3d.h>

#include "game/diving_logic.h"
#include "net/client.h"
#include "render/model.h"
#include "render/renderer.h"

namespace tt::game {

class DivingGame {
public:
    void init(net::Client& client, uint32_t nowMs, bool autoPlay);
    void shutdown();
    void update(net::Client& client, const circlePosition& pad, u32 kDown, float dt, uint32_t nowMs);
    void draw(render::Renderer& renderer);

    void toonPose(float* x, float* y, float* z, float* h) const;
    bool toonRunning() const { return true; } // swimming animation
    void camera(C3D_FVec* eye, C3D_FVec* target) const;

    const char* line(int i) const { return (i >= 0 && i < 3) ? lines_[i] : ""; }
    bool isDone() const { return done_; }

private:
    struct Fish {
        float x;
        float z;
        int dir; // +1 or -1
        float speed;
        bool active;
    };

    bool done_ = false;
    bool autoPlay_ = false;
    uint32_t startMs_ = 0;
    float timeRemaining_ = diving::kGameDuration;

    diving::SwimmerState swimmer_;
    diving::ChestState chests_[3];
    int recoveries_ = 0;
    int score_ = 5;

    Fish fish_[diving::kNumSpawners];
    float crabX_ = 0.0f;
    int crabDir_ = 1;

    char lines_[3][48] = {};

    render::Model environModel_;
    render::Model boatModel_;
    render::Model chestModel_;
};

} // namespace tt::game
