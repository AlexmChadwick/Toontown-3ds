#pragma once

#include <3ds.h>
#include <citro3d.h>

#include "game/ring_logic.h"
#include "net/client.h"
#include "render/model.h"
#include "render/renderer.h"

namespace tt::game {

class RingGame {
public:
    void init(net::Client& client, uint32_t nowMs, bool autoPlay);
    void shutdown();
    void update(net::Client& client, const circlePosition& pad, u32 kDown, float dt, uint32_t nowMs);
    void draw(render::Renderer& renderer);

    void toonPose(float* x, float* y, float* z, float* h) const;
    bool toonRunning() const { return true; } // swimming forward
    void camera(C3D_FVec* eye, C3D_FVec* target) const;

    const char* line(int i) const { return (i >= 0 && i < 3) ? lines_[i] : ""; }
    bool isDone() const { return done_; }

private:
    bool done_ = false;
    bool autoPlay_ = false;
    uint32_t startMs_ = 0;

    int slot_ = 0;
    int numPlayers_ = 1;
    int ringsCollected_ = 0;
    bool perfect_ = true;
    int nextGroup_ = 0;

    float toonX_ = 0.0f;
    float toonZ_ = 0.0f;
    float toonH_ = 0.0f;

    ringgame::RingGroupData rings_[ringgame::kNumRingGroups];
    bool groupPassed_[ringgame::kNumRingGroups] = {};
    bool groupHit_[ringgame::kNumRingGroups] = {};

    render::Model environ_;
    render::Model ringModel_;

    char lines_[3][64] = {};
};

} // namespace tt::game
