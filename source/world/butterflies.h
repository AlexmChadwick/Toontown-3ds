// The playground butterflies (DistributedButterfly; docs/design/butterflies.md): models and
// flutter / land tracks from tools/pipeline/bake_butterfly.py, flights replayed from the
// server's setState as DistributedButterfly.enterFlying / enterLanded.
#pragma once

#include <cstdint>

#include "render/anim.h"
#include "render/model.h"

namespace tt::net {
class Client;
}
namespace tt::render {
class Renderer;
}

namespace tt::world {

class Butterflies {
public:
    // Loads the six wing sets in Toontown Central and Daisy Gardens playgrounds only.
    void setZone(uint32_t zoneId);
    void draw(render::Renderer& renderer, const net::Client& client, float eyeX, float eyeY, uint32_t nowMs);
    void shutdown();

private:
    static constexpr int kWings = 6;
    render::Model model_[kWings];
    render::Anim flutter_[kWings], land_[kWings];
    bool loaded_ = false;
};

} // namespace tt::world
