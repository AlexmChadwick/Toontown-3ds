#pragma once
#include "render/renderer.h"

namespace tt::world {
// Four small, boot-resident models. Cloud rings retain their upstream transforms.
class Sky {
public:
    bool load();
    void unload() { dome_.unload(); cloud1_.unload(); cloud2_.unload(); dock_.unload(); }
    void draw(render::Renderer& renderer, const C3D_FVec& eye, bool dock, float seconds);
private:
    render::Model dome_, cloud1_, cloud2_, dock_;
};
}
