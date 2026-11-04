// The hood sky (Hood.skyFile; romfs/baked/sky from tools/pipeline/bake_sky.py). Upstream parents it
// to the camera with a CompassEffect (world rotation, world Z 0), draws it first with no depth and
// no fog, and turns TT_sky's two cloud groups at h and -0.8 h (SkyUtil.cloudSkyTrack).
#pragma once

#include <3ds.h>
#include <citro3d.h>

#include "render/model.h"
#include "render/renderer.h"

namespace tt::world {

class Sky {
public:
    // New place: loads the hood's sky (none indoors and in Cog HQs).
    void setZone(u32 zoneId, bool indoors);
    void update(float dt) { cloudH_ += dt * 0.25f; }
    bool active() const { return dome_.loaded(); }
    // Horizon colour of the dome (0..1): the draw-distance fog fades into it.
    const float* horizon() const { return horizon_; }
    // fogged: Donald's Dock's white / underwater fog also covers its sky (DDHood.setWhiteFog).
    void draw(render::Renderer& renderer, const C3D_FVec& eye, bool fogged);
    void shutdown();

private:
    render::Model dome_, cloud1_, cloud2_;
    char name_[4] = {};
    float horizon_[3] = {0x6A / 255.0f, 0xB0 / 255.0f, 0xD9 / 255.0f};
    float cloudH_ = 0.0f;
};

} // namespace tt::world
