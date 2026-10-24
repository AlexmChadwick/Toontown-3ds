#pragma once

#include <3ds.h>
#include <citro3d.h>

#include "game/tag_logic.h"
#include "net/client.h"
#include "render/model.h"
#include "render/renderer.h"

namespace tt::game {

class TagGame {
public:
    TagGame() = default;

    bool init(const char* root, uint32_t itAvId);
    void shutdown();

    void update(net::Client& client, const circlePosition& pad, u32 keysDown, float dt, u32 nowMs);
    void draw(render::Renderer& renderer);

    void camera(C3D_FVec* eye, C3D_FVec* target) const;
    void toonPose(float* x, float* y, float* z, float* h) const;
    bool isDone() const { return isDone_; }
    int score() const { return sim_.localScore; }

    const char* title() const { return "Tag"; }
    const char* line(int i) const;

    void onTag(uint32_t tagger, uint32_t tagged);

private:
    taggame::Simulation sim_;
    float toonX_ = 0.0f;
    float toonY_ = 0.0f;
    float toonH_ = 0.0f;
    bool isDone_ = false;

    render::Model arenaModel_;
    render::Model treasureModel_;

    mutable char lines_[3][64] = {};
};

} // namespace tt::game
