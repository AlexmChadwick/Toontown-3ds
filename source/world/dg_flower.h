// Daisy Gardens' big flower (DistributedDGFlower, not part of the zone bake): DG_flower-mod at
// (1.39, 92.91), scale 2.5, spinning, raised to the AI's setHeight; our toon entering its
// trigger sphere sends avatarEnter (each toon there raises it 0.5), and its wall sphere keeps
// toons out (tools/pipeline/bake_dg_flower.py).
#pragma once

#include <cstdint>

#include "render/model.h"
#include "world/player.h"

namespace tt::net {
class Client;
}
namespace tt::render {
class Renderer;
}

namespace tt::world {

class DGFlower {
public:
    void setZone(uint32_t zoneId);
    // Once per frame with our toon's feet: trigger contact and the raise lerp.
    void update(net::Client& client, float x, float y, float z, uint32_t nowMs);
    void draw(render::Renderer& renderer, uint32_t nowMs) const;
    // The wall sphere, while the flower exists (nullptr otherwise).
    const DynamicSolids* solids() const { return present_ ? &solids_ : nullptr; }
    void shutdown() { model_.unload(); }

private:
    static void push(void* user, float* x, float* y, float z, float radius);
    render::Model model_;
    DynamicSolids solids_;
    bool present_ = false, inside_ = false;
    float z_ = 2.0f, fromZ_ = 2.0f, toZ_ = 2.0f;
    uint32_t moveMs_ = 0;
};

} // namespace tt::world
