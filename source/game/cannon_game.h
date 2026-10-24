#pragma once

#include <3ds.h>
#include <citro3d.h>

#include "game/cannon_logic.h"
#include "net/client.h"
#include "render/model.h"
#include "render/renderer.h"

namespace tt::world { class ToonAvatar; }

namespace tt::game {

class CannonGame {
public:
    void init(net::Client& client, uint32_t nowMs, bool autoPlay);
    void shutdown();
    void update(net::Client& client, const circlePosition& pad, u32 kDown, float dt, uint32_t nowMs);
    void draw(render::Renderer& renderer);

    void toonPose(float* x, float* y, float* z, float* h) const;
    void applyAnimation(world::ToonAvatar& avatar) const;
    bool toonRunning() const { return state_ == State::Flying; }
    void camera(C3D_FVec* eye, C3D_FVec* target) const;

    const char* line(int i) const { return (i >= 0 && i < 3) ? lines_[i] : ""; }
    bool isDone() const { return done_; }

private:
    enum class State : uint8_t { Aiming, Flying, Landed, Won };

    State state_ = State::Aiming;
    bool done_ = false;
    bool autoPlay_ = false;
    uint32_t startMs_ = 0;
    uint32_t stateMs_ = 0;
    uint32_t lastBroadcastMs_ = 0;

    int slot_ = 0;
    int numPlayers_ = 1;
    float zRot_ = 0.0f;
    float angle_ = cannongame::kCannonAngleMin;
    cannongame::Vec3 towerPos_;
    cannongame::FlightResult flight_;

    float toonX_ = 0.0f, toonY_ = 0.0f, toonZ_ = 0.0f, toonH_ = 0.0f, toonPitch_ = 0.0f;
    float camEyeX_ = 0.0f, camEyeY_ = 0.0f, camEyeZ_ = 0.0f;
    float camTgtX_ = 0.0f, camTgtY_ = 0.0f, camTgtZ_ = 0.0f;

    render::Model ground_;
    render::Model hill_;
    render::Model tower_;
    render::Model cannonBase_;
    render::Model cannonBarrel_;

    char lines_[3][64] = {};
};

} // namespace tt::game
