#pragma once

#include <3ds.h>
#include <citro3d.h>

#include "game/photo_logic.h"
#include "net/client.h"
#include "render/model.h"
#include "render/renderer.h"

namespace tt::game {

class PhotoGame {
public:
    PhotoGame() = default;

    bool init(const char* root);
    void shutdown();

    void update(net::Client& client, const circlePosition& pad, u32 keysDown, float dt, u32 nowMs);
    void draw(render::Renderer& renderer);

    void camera(C3D_FVec* eye, C3D_FVec* target) const;
    void toonPose(float* x, float* y, float* z, float* h) const;
    bool isDone() const { return sim_.isDone; }
    int score() const { return sim_.scores[0]; }

    const char* title() const { return "Photo Fun"; }
    const char* line(int i) const;

private:
    photogame::Simulation sim_;
    float camYaw_ = -90.0f;
    float camPitch_ = 0.0f;
    float timeRemaining_ = 60.0f;

    render::Model sceneModel_;
    render::Model subjectModel_;

    mutable char lines_[3][64] = {};
};

} // namespace tt::game
