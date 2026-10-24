#include "world/trolley.h"

#include <cmath>

#include "core/log.h"
#include "world/collision.h"

namespace tt::world {

namespace {

constexpr float kDegToRad = 3.14159265f / 180.0f;
constexpr float kCountdownSec = 10.0f; // TrolleyConstants.TROLLEY_COUNTDOWN_TIME
constexpr float kSphereRadius = 15.756f;
// The parked car in station space (DistributedTrolley enter track ends at (15, 14, -1)).
constexpr float kCarX = 15.0f, kCarY = 14.0f, kCarZ = -1.0f;

struct Station {
    u32 hood;
    float x, y, z, h;
};
// DNA placement of prop_trolley_station per playground (trolley.md 1.3).
const Station kStations[] = {
    {2000, -120.945f, -77.5626f, 0.525f, 128.0f}, {1000, -25.0f, -90.0f, 5.67f, 180.0f},
    {5000, 8.37f, 222.32f, 10.0f, 0.0f},          {4000, -54.0463f, 55.8937f, 6.5f, 45.0f},
    {3000, 52.5f, 55.8f, 6.15f, -70.0f},          {9000, 64.0f, -105.2f, 0.0f, -180.0f},
};

} // namespace

void TrolleyRide::setZone(u32 zoneId) {
    has_ = false;
    seat_ = -1;
    inSphere_ = requested_ = false;
    if (zoneId % 1000 != 0) return; // playgrounds only
    for (const Station& s : kStations) {
        if (s.hood != zoneId) continue;
        has_ = true;
        hood_ = s.hood;
        px_ = s.x, py_ = s.y, pz_ = s.z, ph_ = s.h;
        sphereY_ = s.hood == 5000 ? 13.88f : 14.88f;
    }
}

void TrolleyRide::toWorld(float sx, float sy, float sz, float* wx, float* wy, float* wz) const {
    const float c = cosf(ph_ * kDegToRad), s = sinf(ph_ * kDegToRad);
    *wx = px_ + sx * c - sy * s;
    *wy = py_ + sx * s + sy * c;
    *wz = pz_ + sz;
}

void TrolleyRide::seatPose(float* x, float* y, float* z, float* h) const {
    // DistributedTrolley.fillSlot: the toon ends sitting at (-3.9, -4.5 + 3i, 3.0) in car space, H 90.
    const int i = seat_ < 0 ? 0 : seat_;
    toWorld(kCarX - 3.9f, kCarY - 4.5f + 3.0f * i, kCarZ + 3.0f, x, y, z);
    *h = ph_ + 90.0f;
}

void TrolleyRide::exitPose(int seat, float* x, float* y, float* z, float* h) const {
    toWorld(21.0f - 3.0f * seat, -5.0f, 0.02f, x, y, z);
    *h = ph_ - 90.0f;
}

void TrolleyRide::camera(C3D_FVec* eye, C3D_FVec* target) const {
    // Trolley.py enterTrolley: camera at (-35, 0, 8) relative to the car, H -90 (looking at it).
    float ex, ey, ez, tx, ty, tz;
    toWorld(kCarX - 35.0f, kCarY, kCarZ + 8.0f, &ex, &ey, &ez);
    toWorld(kCarX, kCarY, kCarZ + 4.0f, &tx, &ty, &tz);
    *eye = FVec3_New(ex, ey, ez);
    *target = FVec3_New(tx, ty, tz);
}

int TrolleyRide::countdown(const net::Client& client, u32 nowMs) const {
    const net::Trolley* t = client.trolley();
    if (!t || t->state != net::TrolleyState::WaitCountdown) return -1;
    const float left = kCountdownSec - static_cast<s32>(nowMs - t->stateStartMs) / 1000.0f;
    return left > 0 ? static_cast<int>(left) : 0;
}

void TrolleyRide::update(net::Client& client, float x, float y, float z, int hp, u32 nowMs) {
    if (!has_ || seat_ >= 0) return;
    const net::Trolley* t = client.trolley();
    float cx, cy, cz;
    toWorld(16.19f, sphereY_, 3.57f, &cx, &cy, &cz);
    // GravityWalker's event sphere (AGENTS.md): centre r - 0.1 above the feet, radius r * 1.04.
    const float r = kToonRadius;
    const float dx = x - cx, dy = y - cy, dz = z + r - 0.1f - cz;
    const bool inside = dx * dx + dy * dy + dz * dz < (kSphereRadius + r * 1.04f) * (kSphereRadius + r * 1.04f);
    const bool waiting = t && (t->state == net::TrolleyState::WaitEmpty || t->state == net::TrolleyState::WaitCountdown);
    if (requested_ && nowMs - requestMs_ > 5000) requested_ = false; // no answer: allow another try
    if (inside && !inSphere_ && waiting && !requested_ && z >= pz_ + kCarZ) {
        if (hp <= 0) {
            message_ = "You need more Laff points to ride the trolley.";
        } else if (client.sendTrolleyBoard()) {
            requested_ = true;
            requestMs_ = nowMs;
            TT_LOG("trolley: requestBoard");
        }
    }
    inSphere_ = inside;
}

void TrolleyRide::hopOff(net::Client& client) {
    if (seat_ >= 0 && client.sendTrolleyExit()) TT_LOG("trolley: requestExit");
}

void TrolleyRide::handleEvent(const net::Event& ev, net::Client& client, u32 nowMs) {
    (void)nowMs;
    using net::EventType;
    const u32 me = client.avatarId();
    switch (ev.type) {
    case EventType::TrolleyFill:
        if (ev.doId == me) {
            seat_ = ev.arg;
            requested_ = false;
            message_ = "All aboard! B hops off the trolley.";
            TT_LOG("trolley: seated in %d", seat_);
        }
        break;
    case EventType::TrolleyEmpty:
        if (ev.doId == me && seat_ >= 0) {
            exitSeat_ = seat_;
            seat_ = -1;
            TT_LOG("trolley: off");
        }
        break;
    case EventType::TrolleyReject:
        requested_ = false;
        message_ = "The trolley is full.";
        break;
    case EventType::TrolleyMinigameZone:
        if (seat_ >= 0) {
            mgZone_ = ev.doId;
            mgGame_ = static_cast<u16>(ev.arg);
            mgPending_ = true;
            TT_LOG("trolley: off to minigame %u in zone %lu", unsigned(mgGame_), static_cast<unsigned long>(mgZone_));
        }
        break;
    case EventType::TrolleyGone:
        seat_ = -1;
        break;
    default:
        break;
    }
}

bool TrolleyRide::popMinigame(u32* zone, u16* gameId) {
    if (!mgPending_) return false;
    mgPending_ = false;
    seat_ = -1;
    *zone = mgZone_;
    *gameId = mgGame_;
    return true;
}

} // namespace tt::world
