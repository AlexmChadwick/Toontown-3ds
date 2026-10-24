#pragma once

#include <3ds.h>
#include <citro3d.h>

#include "game/tug_logic.h"
#include "net/client.h"
#include "render/model.h"
#include "render/renderer.h"

namespace tt::game {

class TugGame {
public:
    void init(net::Client& client, uint32_t nowMs, bool autoPlay);
    void shutdown();
    void update(net::Client& client, const circlePosition& pad, u32 kDown, float dt, uint32_t nowMs);
    void draw(render::Renderer& renderer);

    void toonPose(float* x, float* y, float* z, float* h) const;
    bool toonRunning() const { return state_ == State::Tugging; }
    void camera(C3D_FVec* eye, C3D_FVec* target) const;

    const char* line(int i) const { return (i >= 0 && i < 3) ? lines_[i] : ""; }
    bool isDone() const { return done_; }

private:
    enum class State : uint8_t { WaitGo, Tugging, ContestOver };

    State state_ = State::WaitGo;
    bool done_ = false;
    bool autoPlay_ = false;
    bool readySent_ = false;
    uint32_t startMs_ = 0;
    uint32_t lastUpdateMs_ = 0;

    int slot_ = 0;
    int numPlayers_ = 1;
    tuggame::KeyTracker tracker_;

    float toonOffset_ = 0.0f;
    float suitOffset_ = 0.0f;
    bool won_ = false;

    render::Model dock_;
    render::Model rope_;

    char lines_[3][64] = {};
};

} // namespace tt::game
