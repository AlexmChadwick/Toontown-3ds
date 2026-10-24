#include "world/factory.h"

#include <cmath>
#include <algorithm>

namespace tt::world {

void Factory::loadAssets() {
    if (!level_.loaded()) level_.load("sdmc:/3ds/toontown3ds/data/factory/factory.ttm");
    if (!cell_.loaded()) cell_.load("sdmc:/3ds/toontown3ds/data/factory/cell_3.ttm");
    if (!collision_.loaded()) collision_.load("sdmc:/3ds/toontown3ds/data/factory/factory.ttc");
    const char* names[] = {"lift", "door", "button", "crate", "goon", "stomper"};
    for (int n = 0; n < 6; ++n) {
        if (!props_[n].loaded) {
            char path[112];
            snprintf(path, sizeof(path), "sdmc:/3ds/toontown3ds/data/factory/%s.ttm", names[n]);
            props_[n].type = static_cast<net::FactoryEntityType>(n + 1);
            props_[n].loaded = props_[n].model.load(path);
        }
    }
}

void Factory::unloadAssets() {
    level_.unload();
    cell_.unload();
    collision_.unload();
    for (Prop& p : props_) { p.model.unload(); p.loaded = false; }
}

void Factory::handleEvent(const net::Event& ev, const net::Client& client) {
    if (ev.type == net::EventType::FactoryUp || ev.type == net::EventType::FactoryState) {
        const net::Factory* f = client.factory();
        if (!f) return;
        active_ = true;
        zoneId_ = f->levelZoneId;
        spawned_ = false;
        loadAssets();
    } else if (ev.type == net::EventType::FactoryGone) {
        active_ = false;
        zoneId_ = 0;
        spawned_ = false;
        unloadAssets();
    }
}

void Factory::update(net::Client& client, float x, float y, float z, uint32_t nowMs) {
    const net::Factory* f = client.factory();
    if (!f) { active_ = false; return; }
    active_ = true;
    zoneId_ = f->levelZoneId;
    loadAssets();
    if (spawnZone_ != zoneId_) {
        spawnZone_ = zoneId_;
        spawned_ = false;
    }
    for (int n = 0; n < net::kMaxFactoryEntities; ++n) {
        const net::FactoryEntity& e = client.factoryEntitySlot(n);
        if (!e.active) continue;
        float t = std::min(1.0f, (nowMs >= e.stateStartMs) ?
            (nowMs - e.stateStartMs) / 1500.0f : 0.0f);
        poses_[n].x = e.x + (e.tx - e.x) * t;
        poses_[n].y = e.y + (e.ty - e.y) * t;
        poses_[n].z = e.z + (e.tz - e.z) * t;
        if (e.type == net::FactoryEntityType::Goon && e.state != e.nextState)
            poses_[n].x += std::sin(nowMs * 0.004f + n) * 0.35f;
    }
    if (nowMs - lastInteraction_ < 750) return;
    for (int n = 0; n < net::kMaxFactoryEntities; ++n) {
        const net::FactoryEntity& e = client.factoryEntitySlot(n);
        if (!e.active) continue;
        const float dx = x - poses_[n].x, dy = y - poses_[n].y, dz = z - poses_[n].z;
        if (dx * dx + dy * dy + dz * dz > 25.0f) continue;
        bool sent = false;
        if (e.type == net::FactoryEntityType::Goon) sent = client.sendFactoryGoonBattle(e.doId, std::sqrt(dx * dx + dy * dy + dz * dz));
        else if (e.type == net::FactoryEntityType::Crate) sent = client.sendFactoryCratePush(e.doId, 0);
        else if (e.type == net::FactoryEntityType::Button || e.type == net::FactoryEntityType::Door ||
                 e.type == net::FactoryEntityType::Lift) sent = client.sendFactoryEntityInteract(e.doId);
        if (sent) lastInteraction_ = nowMs;
        break;
    }

}

bool Factory::spawnPose(float* x, float* y, float* z, float* h) {
    if (!active_ || spawned_) return false;
    spawned_ = true;
    *x = 0.0f; *y = -12.0f; *z = 0.0f; *h = 0.0f;
    return true;
}

bool Factory::battleTarget(const net::Client& client, uint32_t* doId, float* x, float* y, float* z, float* h) const {
    if (!active_) return false;
    for (int n = 0; n < net::kMaxFactoryEntities; ++n) {
        const net::FactoryEntity& e = client.factoryEntitySlot(n);
        if (e.active && e.type == net::FactoryEntityType::Goon) {
            *doId = e.doId; *x = poses_[n].x; *y = poses_[n].y; *z = poses_[n].z; *h = 180.0f;
            return true;
        }
    }
    return false;
}

void Factory::draw(render::Renderer& renderer, const net::Client& client) const {
    if (!active_ || !level_.loaded()) return;
    C3D_Mtx identity;
    Mtx_Identity(&identity);
    renderer.drawModel(const_cast<render::Model&>(level_), identity);
    if (cell_.loaded()) renderer.drawModel(const_cast<render::Model&>(cell_), identity);
    for (int n = 0; n < net::kMaxFactoryEntities; ++n) {
        const net::FactoryEntity& e = client.factoryEntitySlot(n);
        if (!e.active) continue;
        const int index = static_cast<int>(e.type) - 1;
        if (index < 0 || index >= 6 || !props_[index].loaded) continue;
        C3D_Mtx m;
        Mtx_Identity(&m);
        Mtx_Translate(&m, poses_[n].x, poses_[n].y, poses_[n].z, true);
        renderer.drawModel(const_cast<render::Model&>(props_[index].model), m);
    }
}

void Factory::shutdown() {
    active_ = false;
    collision_.unload();
    spawned_ = false;
    unloadAssets();
}

} // namespace tt::world
