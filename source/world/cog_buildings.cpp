#include "world/cog_buildings.h"

#include <cmath>
#include <cstdio>

#include "core/log.h"
#include "world/place.h"

namespace tt::world {
namespace {
constexpr float kDegToRad = 3.14159265f / 180.0f;
constexpr float kBoardRadius = 5.0f;
const char* kDept[] = {"c", "l", "m", "s"};
}

void CogBuildings::setZone(uint32_t zone) {
    zone_ = zone;
    requestedElevator_ = 0;
    requestMs_ = 0;
    for (Anchor& a : anchors_) a = Anchor{};
    for (Shell& s : shells_) {
        s.model.unload();
        s.loaded = false;
    }

    for (int i = 0; i < 4; ++i) {
        char path[112];
        snprintf(path, sizeof(path), "sdmc:/3ds/toontown3ds/data/cogbldg/shell_%s.ttm", kDept[i]);
        shells_[i].dept = static_cast<int8_t>(i);
        shells_[i].loaded = shells_[i].model.load(path);
    }
}

void CogBuildings::syncAnchors(const Place& place) {
    char name[16];
    for (uint32_t block = 0; block < kMaxAnchors; ++block) {
        snprintf(name, sizeof(name), "tb%lu", static_cast<unsigned long>(block));
        const Place::Point* p = place.point(name);
        if (!p) {
            snprintf(name, sizeof(name), "sz%lu", static_cast<unsigned long>(block));
            p = place.point(name);
        }
        if (p) setAnchor(block, p->x, p->y, p->z, p->h);
    }
}

void CogBuildings::setAnchor(uint32_t block, float x, float y, float z, float h) {
    for (Anchor& a : anchors_) {
        if (a.used && a.block == block) { a.x = x; a.y = y; a.z = z; a.h = h; return; }
    }
    for (Anchor& a : anchors_) if (!a.used) {
        a = Anchor{block, x, y, z, h, true};
        return;
    }
}

void CogBuildings::handleEvent(const net::Event& ev, const net::Client&) {
    if (ev.type == net::EventType::CogBuildingGone) {
        message_ = nullptr;
    }
}

void CogBuildings::update(net::Client& client, float x, float y, float z, uint32_t nowMs) {
    for (int n = 0; n < net::kMaxCogElevators; ++n) {
        const net::CogElevator& e = client.cogElevatorSlot(n);
        if (!e.active || e.buildingDoId == 0) continue;
        const net::CogBuilding* b = client.findCogBuilding(e.buildingDoId);
        if (!b) continue;
        Anchor* a = nullptr;
        for (Anchor& candidate : anchors_) if (candidate.used && candidate.block == b->block) a = &candidate;
        if (!a) continue;
        const float dx = x - a->x, dy = y - a->y, dz = z - a->z;
        if (dx * dx + dy * dy + dz * dz < kBoardRadius * kBoardRadius &&
            requestedElevator_ == 0 && client.sendCogElevatorBoard(e.doId)) {
            requestedElevator_ = e.doId;
            requestMs_ = nowMs;
        }
        if (requestedElevator_ && nowMs - requestMs_ > 5000) requestedElevator_ = 0;
    }
}

void CogBuildings::draw(render::Renderer& renderer, const net::Client& client) const {
    for (int n = 0; n < net::kMaxCogBuildings; ++n) {
        const net::CogBuilding& b = client.cogBuildingSlot(n);
        if (!b.active || b.state != net::CogBuildingState::Suit) continue;
        const Anchor* a = nullptr;
        for (const Anchor& candidate : anchors_) if (candidate.used && candidate.block == b.block) a = &candidate;
        const int dept = b.track < 0 ? 0 : b.track % 4;
        if (!a || !shells_[dept].loaded) continue;
        C3D_Mtx m;
        Mtx_Identity(&m);
        Mtx_Translate(&m, a->x, a->y, a->z, true);
        Mtx_RotateZ(&m, a->h * kDegToRad, true);
        renderer.drawModel(const_cast<render::Model&>(shells_[dept].model), m);
    }
}

void CogBuildings::shutdown() {
    for (Shell& s : shells_) s.model.unload();
}

const char* CogBuildings::popMessage() {
    const char* out = message_;
    message_ = nullptr;
    return out;
}

} // namespace tt::world
