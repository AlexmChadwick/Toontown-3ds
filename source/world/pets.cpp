#include "world/pets.h"
#include "ui/clerk_rules.h"
namespace tt::world {
void Pets::handleEvent(const net::Event& ev) {
    if (ev.type == net::EventType::PetUp && !loaded_)
        loaded_ = doodle_.load("sdmc:/3ds/toontown3ds/data/estate/doodle.ttm");
    if (ev.type == net::EventType::LeftZone) shutdown();
}
void Pets::update(const net::Client&) {}
void Pets::draw(render::Renderer& renderer, const net::Client& c) const {
    if (!loaded_) return;
    for (int i = 0; i < net::kMaxPets; ++i) {
        const net::Pet& p = c.petSlot(i);
        if (!p.active) continue;
        C3D_Mtx m; Mtx_Identity(&m); Mtx_Translate(&m, p.x, p.y, p.z, true);
        const float* color=clerks::petColor(p.body,p.color);
        const float scale=.8f+.05f*p.colorScale;
        renderer.setBaseTint(color[0]*scale,color[1]*scale,color[2]*scale,1);
        renderer.drawModel(const_cast<render::Model&>(doodle_), m);
        renderer.setBaseTint(1,1,1,1);
    }
}
void Pets::shutdown() { doodle_.unload(); loaded_ = false; }
}
