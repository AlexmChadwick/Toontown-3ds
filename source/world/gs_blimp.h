// Goofy Speedway's blimp: GSPlayground.enter takes GS_blimp out of the playground model and
// flies it around a 360 s circle (tools/pipeline/bake_moving_props.py bakes it apart).
#pragma once

#include <cstdint>

#include "render/model.h"

namespace tt::render {
class Renderer;
}

namespace tt::world {

class GsBlimp {
public:
    // On every zone load; only Goofy Speedway (8000) has one.
    void setZone(uint32_t zoneId);
    void draw(render::Renderer& renderer, uint32_t nowMs) const;
    void shutdown() { model_.unload(); }

private:
    render::Model model_;
};

} // namespace tt::world
