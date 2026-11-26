// The Brrrgh's falling snow (BRSafeZoneLoader: snowdisk.ptf started on the camera, playground
// only). A baked tile of flakes (tools/pipeline/bake_br_snow.py) is drawn 2 x 2 around the
// camera, anchored to the world and scrolling down, covering the 50-unit emitter disc's
// visible part from 10 under to 5 over the eye.
#pragma once

#include <cstdint>

#include <citro3d.h>

#include "render/model.h"

namespace tt::render {
class Renderer;
}

namespace tt::world {

class BrSnow {
public:
    void setZone(uint32_t zoneId);
    void draw(render::Renderer& renderer, const C3D_FVec& eye, uint32_t nowMs) const;
    void shutdown() { model_.unload(); }

private:
    render::Model model_;
};

} // namespace tt::world
