// Acorn Acres' waterfall and geyser (OZSafeZoneLoader.load adds them as Actors, not part of the
// zone): meshes whose textures scroll (tools/pipeline/bake_waterfall.py); the geyser erupts
// every 20 s as setGeyserAnim (its launch of toons standing in it is not reproduced).
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
    void shutdown() {
        model_.unload();
        geyser_.unload();
    }

private:
    render::Model model_, geyser_;
};

} // namespace tt::world
