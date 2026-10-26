#include "world/cog_buildings.h"

#include <cstring>

#include <cmath>
#include <cstdio>

#include "core/log.h"
#include "net/net_platform.h"
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
    // ---- cogdo exterior: load geometry and door anchor once per street ----
    cogdoShell_.unload();
    cogdoShell_.load("sdmc:/3ds/toontown3ds/data/cogdo/shell.ttm");
    FILE* cogdoPoints = fopen("sdmc:/3ds/toontown3ds/data/cogdo/points.bin", "rb");
    if (cogdoPoints) {
        char magic[4] = {};
        uint16_t version = 0, count = 0;
        float point[3] = {};
        const bool valid = fread(magic, 1, 4, cogdoPoints) == 4 && !memcmp(magic, "CGP1", 4) &&
            fread(&version, 2, 1, cogdoPoints) == 1 && version == 1 &&
            fread(&count, 2, 1, cogdoPoints) == 1 && count == 5 && fread(point, sizeof(point), 1, cogdoPoints) == 1;
        if (valid && std::isfinite(point[0]) && std::isfinite(point[1]) && std::isfinite(point[2])) {
            memcpy(cogdoDoor_, point, sizeof(point));
        }
        fclose(cogdoPoints);
    }
    // ---- end cogdo exterior ----
    kart_.unload();
    if (zone == 10000) kart_.load("sdmc:/3ds/toontown3ds/data/cogbldg/cog_kart.ttm"); // Bossbot HQ
}

// DistributedCogKart.announceGenerate: it drives up from 10 below its spot as the doors "open"
// (trolleyEnterTrack, 5 s) and away 45 units along heading - 90 as they "close"
// (trolleyExitTrack, 5 s, to z 0.5); gone while closed. False while it is away.
static bool kartPose(const net::CogElevator& e, uint32_t nowMs, float* x, float* y, float* z) {
    const float t = int32_t(nowMs - e.stateStartMs) / 1000.0f / 5.0f;
    const float u = t < 0 ? 0 : t > 1 ? 1 : t;
    *x = e.x, *y = e.y, *z = e.z;
    if (!strcmp(e.state, "opening")) {
        *z = e.z - 10.0f * (1.0f - u);
    } else if (!strcmp(e.state, "closing")) {
        const float a = (e.h - 90.0f) * kDegToRad;
        *x = e.x + 45.0f * std::cos(a) * u, *y = e.y + 45.0f * std::sin(a) * u;
        *z = e.z + (0.5f - e.z) * u;
    } else if (!strcmp(e.state, "closed") || !strcmp(e.state, "off") || !e.state[0]) {
        return false;
    }
    return true;
}

void CogBuildings::syncAnchors(const Place& place) {
    for (uint32_t block = 0; block < kMaxBlock; ++block) {
        if (const Place::Point* p = place.buildingPoint(block)) setAnchor(block, p->x, p->y, p->z, p->h);
    }
    for (int i = 0; i < kMaxSpots; ++i) {
        char spot[24];
        snprintf(spot, sizeof(spot), "elevator_origin_%d", i);
        const Place::Point* p = place.point(spot);
        spots_[i] = Spot{};
        if (p) spots_[i] = Spot{p->x, p->y, p->z, true};
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
    const uint32_t me = client.avatarId();
    for (int n = 0; n < net::kMaxCogElevators; ++n) {
        const net::CogElevator& e = client.cogElevatorSlot(n);
        if (!e.active) continue;
        bool aboard = false;
        for (uint32_t s : e.slots) aboard |= s == me && me != 0;
        // Facility elevators send the riders their interior zone as they leave.
        if (aboard && e.destZone && e.destZone != lastDest_) {
            pendingDest_ = lastDest_ = e.destZone;
            requestedElevator_ = 0;
        }
        // Cog building elevators: when the doors close with us aboard, go inside
        // (DistributedElevatorExt.getZoneId: the building's interior zone).
        if (aboard && e.buildingDoId && (!strcmp(e.state, "closed") || !strcmp(e.state, "closing"))) {
            if (const net::CogBuilding* b = client.findCogBuilding(e.buildingDoId)) {
                const uint32_t inside = b->interiorZoneId ? b->interiorZoneId : zone_ - zone_ % 100 + 500 + b->block;
                if (inside != lastDest_) pendingDest_ = lastDest_ = inside;
            }
        }
        // Facility elevators stand at the HQ's elevator_origin_<n> anchors; the Bossbot golf
        // karts (DistributedCogKart) where their setPosHpr puts them.
        const bool anchored = e.origin >= 0 && e.origin < kMaxSpots && spots_[e.origin].used;
        if ((anchored || e.hasPose) && !aboard && requestedElevator_ == 0) {
            const float sx = e.hasPose ? e.x : spots_[e.origin].x;
            const float sy = e.hasPose ? e.y : spots_[e.origin].y;
            const float sz = e.hasPose ? e.z : spots_[e.origin].z;
            const float dx = x - sx, dy = y - sy, dz = z - sz;
            if (dx * dx + dy * dy + dz * dz < kBoardRadius * kBoardRadius && client.sendCogElevatorBoard(e.doId)) {
                requestedElevator_ = e.doId;
                requestMs_ = nowMs;
            }
        }
        if (requestedElevator_ && nowMs - requestMs_ > 5000) requestedElevator_ = 0;
        if (e.buildingDoId == 0) continue;
        const net::CogBuilding* b = client.findCogBuilding(e.buildingDoId);
        if (!b) continue;
        Anchor* a = nullptr;
        for (Anchor& candidate : anchors_) if (candidate.used && candidate.block == b->block) a = &candidate;
        if (!a) continue;
        // ---- cogdo exterior: the stock shell is offset from the DNA anchor ----
        float targetX = a->x, targetY = a->y, targetZ = a->z;
        if (b->state == net::CogBuildingState::Cogdo) {
            const float angle = a->h * kDegToRad;
            targetX += cosf(angle) * cogdoDoor_[0] - sinf(angle) * cogdoDoor_[1];
            targetY += sinf(angle) * cogdoDoor_[0] + cosf(angle) * cogdoDoor_[1];
            targetZ += cogdoDoor_[2];
        }
        // ---- end cogdo exterior ----
        const float dx = x - targetX, dy = y - targetY, dz = z - targetZ;
        if (dx * dx + dy * dy + dz * dz < kBoardRadius * kBoardRadius &&
            requestedElevator_ == 0 && client.sendCogElevatorBoard(e.doId)) {
            requestedElevator_ = e.doId;
            requestMs_ = nowMs;
        }
        if (requestedElevator_ && nowMs - requestMs_ > 5000) requestedElevator_ = 0;
    }
}

bool CogBuildings::suitElevatorPoint(const net::Client& client, float* x, float* y, float* z, float* h) const {
    for (int n = 0; n < net::kMaxCogElevators; ++n) {
        const net::CogElevator& e = client.cogElevatorSlot(n);
        if (!e.active || e.buildingDoId == 0) continue;
        const net::CogBuilding* b = client.findCogBuilding(e.buildingDoId);
        if (!b || b->state != net::CogBuildingState::Suit) continue;
        for (const Anchor& a : anchors_) {
            if (!a.used || a.block != b->block) continue;
            *x = a.x;
            *y = a.y;
            *z = a.z;
            *h = a.h;
            return true;
        }
    }
    return false;
}

void CogBuildings::draw(render::Renderer& renderer, const net::Client& client) const {
    for (int n = 0; kart_.loaded() && n < net::kMaxCogElevators; ++n) {
        const net::CogElevator& e = client.cogElevatorSlot(n);
        float kx, ky, kz;
        if (!e.active || !e.hasPose || !kartPose(e, net::plat::nowMs(), &kx, &ky, &kz)) continue;
        C3D_Mtx m;
        Mtx_Identity(&m);
        Mtx_Translate(&m, kx, ky, kz, true);
        Mtx_RotateZ(&m, e.h * kDegToRad, true);
        renderer.drawModel(const_cast<render::Model&>(kart_), m);
    }
    for (int n = 0; n < net::kMaxCogBuildings; ++n) {
        const net::CogBuilding& b = client.cogBuildingSlot(n);
        // ---- cogdo exterior: all departments use the pinned shared shell ----
        if (!b.active || (b.state != net::CogBuildingState::Suit && b.state != net::CogBuildingState::Cogdo)) continue;
        // ---- end cogdo exterior ----
        const Anchor* a = nullptr;
        for (const Anchor& candidate : anchors_) if (candidate.used && candidate.block == b.block) a = &candidate;
        const int dept = b.track < 0 ? 0 : b.track % 4;
        if (!a || (b.state == net::CogBuildingState::Cogdo ? !cogdoShell_.loaded() : !shells_[dept].loaded)) continue;
        C3D_Mtx m;
        Mtx_Identity(&m);
        Mtx_Translate(&m, a->x, a->y, a->z, true);
        Mtx_RotateZ(&m, a->h * kDegToRad, true);
        // ---- cogdo exterior ----
        render::Model& shell = b.state == net::CogBuildingState::Cogdo ?
            const_cast<render::Model&>(cogdoShell_) : const_cast<render::Model&>(shells_[dept].model);
        renderer.drawModel(shell, m);
        // ---- end cogdo exterior ----
    }
}

void CogBuildings::shutdown() {
    cogdoShell_.unload(); // cogdo exterior
    for (Shell& s : shells_) s.model.unload();
}

const char* CogBuildings::popMessage() {
    const char* out = message_;
    message_ = nullptr;
    return out;
}

} // namespace tt::world
