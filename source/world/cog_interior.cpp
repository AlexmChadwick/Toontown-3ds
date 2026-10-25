#include "world/cog_interior.h"

#include <cstdio>
#include <cstring>

namespace tt::world {

void CogInterior::handleEvent(const net::Event& ev, net::Client& client, uint32_t) {
    if (ev.type == net::EventType::CogInteriorGone) {
        shutdown();
        return;
    }
    if (ev.type != net::EventType::CogInteriorUp && ev.type != net::EventType::CogInteriorState) return;
    const net::CogInterior* i = client.cogInterior();
    if (!i) return;
    active_ = true;
    if (interiorDoId_ != i->doId) {
        interiorDoId_ = i->doId;
        floor_ = 0;
        spawned_ = false;
    }
    zoneId_ = i->zoneId;
    memcpy(state_, i->state, sizeof(state_) - 1);
    state_[sizeof(state_) - 1] = 0;
    sentElevatorDone_ = false;
    sentReserveDone_ = false;
    lastStateStartMs_ = i->stateStartMs;
    if (!strcmp(state_, "Elevator") || !strcmp(state_, "Resting")) floor_ = floor_ < 0 ? 0 : floor_;
    loadFloor(floor_);
    if (ev.type == net::EventType::CogInteriorUp) client.sendCogInteriorAvatarJoined(interiorDoId_);
}

void CogInterior::loadFloor(int floor) {
    // State changes on one floor do not change immutable baked geometry.
    if (loadedFloor_ == floor)
        return;
    loadedFloor_ = floor;
    char path[112];
    snprintf(path, sizeof(path), "sdmc:/3ds/toontown3ds/data/cogbldg/floor_%d.ttm", floor);
    assetBatch_.load(room_, path);
    char collisionPath[128];
    snprintf(collisionPath, sizeof(collisionPath),
             "sdmc:/3ds/toontown3ds/data/cogbldg/floor_%d.ttc", floor);
    collision_.unload();
    collision_.load(collisionPath);
}

void CogInterior::update(net::Client& client, uint32_t) {
    const net::CogInterior* i = client.cogInterior();
    if (!i) { active_ = false; room_.unload(); return; }
    if (i->zoneId != zoneId_ || strcmp(i->state, state_) != 0 ||
        i->stateStartMs != lastStateStartMs_) {
        char previous[sizeof(state_)];
        memcpy(previous, state_, sizeof(previous));
        zoneId_ = i->zoneId;
        memcpy(state_, i->state, sizeof(state_) - 1);
        state_[sizeof(state_) - 1] = 0;
        if (!strcmp(state_, "Elevator") && strcmp(previous, "Elevator") &&
            floor_ + 1 < i->numFloors) {
            ++floor_;
        }
        sentElevatorDone_ = false;
        sentReserveDone_ = false;
        loadFloor(floor_);
        lastStateStartMs_ = i->stateStartMs;
    }

    if (!strcmp(state_, "Elevator") && !sentElevatorDone_) {
        sentElevatorDone_ = client.sendCogInteriorElevatorDone(i->doId);
    } else if (!strcmp(state_, "ReservesJoining") && !sentReserveDone_) {
        sentReserveDone_ = client.sendCogInteriorReserveJoinDone(i->doId);
    }
}

bool CogInterior::spawnPose(float* x, float* y, float* z, float* h) {
    if (!active_ || spawned_) return false;
    spawned_ = true;
    *x = 0.0f; *y = -10.0f; *z = 0.0f; *h = 0.0f;
    return true;
}

bool CogInterior::battleTarget(const net::Client& client, uint32_t* doId, float* x, float* y,
                               float* z, float* h) const {
    const net::CogInterior* i = client.cogInterior();
    if (!active_ || !i || !i->numSuits) return false;
    *doId = i->suits[0];
    *x = 0.0f; *y = 4.0f; *z = 0.0f; *h = 180.0f;
    return true;
}

void CogInterior::draw(render::Renderer& renderer) const {
    if (!active_ || !room_.loaded()) return;
    C3D_Mtx m;
    Mtx_Identity(&m);
    renderer.drawModel(const_cast<render::Model&>(room_), m);
}

void CogInterior::shutdown() {
    assetBatch_.clear();
    loadedFloor_ = -1;
    if (room_.loaded())
        C3D_FrameSync();
    active_ = false;
    sentElevatorDone_ = false;
    sentReserveDone_ = false;
    room_.unload();
    collision_.unload();
    spawned_ = false;
    lastStateStartMs_ = 0;
}

const char* CogInterior::popMessage() {
    const char* out = message_;
    message_ = nullptr;
    return out;
}

} // namespace tt::world
