#include "world/estate.h"
#include <cstdio>
namespace tt::world {
void Estate::unload() {
    terrain_.unload(); interior_.unload();
    for (auto& m : houses_) m.unload();
    for (auto& m : furniture_) m.unload();
}
void Estate::loadExterior() {
    if (!terrain_.loaded()) terrain_.load("sdmc:/3ds/toontown3ds/data/estate/terrain.ttm");
    for (int i = 0; i < 6; ++i) if (!houses_[i].loaded()) {
        char path[112]; snprintf(path, sizeof(path), "sdmc:/3ds/toontown3ds/data/estate/house_%d.ttm", i);
        houses_[i].load(path);
    }
}
void Estate::loadInterior() {
    if (!interior_.loaded()) interior_.load("sdmc:/3ds/toontown3ds/data/estate/interior.ttm");
    for (int i = 0; i < 4; ++i) if (!furniture_[i].loaded()) {
        char path[112]; snprintf(path, sizeof(path), "sdmc:/3ds/toontown3ds/data/estate/furniture_%d.ttm", i);
        furniture_[i].load(path);
    }
}
void Estate::handleEvent(const net::Event& ev, net::Client& c) {
    if (ev.type == net::EventType::EstateUp || ev.type == net::EventType::HouseUp) {
        active_ = true; inInterior_ = false; loadExterior();
        if (ev.type == net::EventType::EstateUp && c.estate()) c.sendEstateClientReady(c.estate()->doId);
    } else if (ev.type == net::EventType::HouseInteriorUp) {
        active_ = true; inInterior_ = true; loadInterior();
    } else if (ev.type == net::EventType::EstateGone) {
        shutdown();
    }
}
void Estate::update(net::Client& c, float x, float y, float z, uint32_t now) {
    if (!c.estate() && !c.houseInterior()) { active_ = false; return; }
    if (c.houseInterior()) { inInterior_ = true; loadInterior(); }
    if (now - lastInteract_ < 900 || !c.estate()) return;
    const net::Estate* e = c.estate();
    for (const net::EstateHouse& h : e->houses) if (h.active && h.avatarId == c.avatarId() &&
        x * x + y * y + z * z < 100.0f) {
        if (c.sendHouseEnter(h.doId)) lastInteract_ = now;
        break;
    }
}
void Estate::draw(render::Renderer& renderer, const net::Client& c) const {
    if (!active_) return;
    C3D_Mtx m; Mtx_Identity(&m);
    if (inInterior_) {
        if (interior_.loaded()) renderer.drawModel(const_cast<render::Model&>(interior_), m);
        for (const auto& f : furniture_) if (f.loaded()) renderer.drawModel(const_cast<render::Model&>(f), m);
        return;
    }
    if (terrain_.loaded()) renderer.drawModel(const_cast<render::Model&>(terrain_), m);
    if (const net::Estate* e = c.estate()) {
        for (int i = 0; i < 6; ++i) if (e->houses[i].active && houses_[i].loaded())
            renderer.drawModel(const_cast<render::Model&>(houses_[i]), m);
    }
}
void Estate::shutdown() { active_ = false; inInterior_ = false; unload(); }
}
