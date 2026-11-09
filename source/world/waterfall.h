// Acorn Acres' waterfall (OZSafeZoneLoader.enter loads it as an Actor, not part of the zone):
// a static mesh whose texture scrolls down (tools/pipeline/bake_waterfall.py).
#pragma once

#include <cstdint>

#include "render/model.h"

namespace tt::render {
class Renderer;
}

namespace tt::world {

class Waterfall {
public:
    // On every zone load; only Acorn Acres (6000) has one.
    void setZone(uint32_t zoneId);
    void draw(render::Renderer& renderer, uint32_t nowMs) const;
    void shutdown() { model_.unload(); }

private:
    render::Model model_;
};

} // namespace tt::world
