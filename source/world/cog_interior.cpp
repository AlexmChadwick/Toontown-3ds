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
    interiorDoId_ = i->doId;
    zoneId_ = i->zoneId;
    memcpy(state_, i->state, sizeof(state_) - 1);
    state_[sizeof(state_) - 1] = 0;
    sentElevatorDone_ = false;
    sentReserveDone_ = false;
    if (!strcmp(state_, "Elevator") || !strcmp(state_, "Resting")) floor_ = floor_ < 0 ? 0 : floor_;
    loadFloor(floor_);
    if (ev.type == net::EventType::CogInteriorUp) client.sendCogInteriorAvatarJoined(interiorDoId_);
}

void CogInterior::loadFloor(int floor) {
    char path[112];
    snprintf(path, sizeof(path), "sdmc:/3ds/toontown3ds/data/cogbldg/floor_%d.ttm", floor);
    room_.unload();
    room_.load(path);
}

void CogInterior::update(net::Client& client, uint32_t) {
    const net::CogInterior* i = client.cogInterior();
    if (!i) { active_ = false; room_.unload(); return; }
    if (i->zoneId != zoneId_ || strcmp(i->state, state_) != 0) {
        zoneId_ = i->zoneId;
        memcpy(state_, i->state, sizeof(state_) - 1);
        state_[sizeof(state_) - 1] = 0;
        sentElevatorDone_ = false;
        sentReserveDone_ = false;
        loadFloor(floor_);
    }
    if (!strcmp(state_, "Elevator") && !sentElevatorDone_) {
        sentElevatorDone_ = client.sendCogInteriorElevatorDone(i->doId);
    } else if (!strcmp(state_, "ReservesJoining") && !sentReserveDone_) {
        sentReserveDone_ = client.sendCogInteriorReserveJoinDone(i->doId);
    }
}

void CogInterior::draw(render::Renderer& renderer) const {
    if (!active_ || !room_.loaded()) return;
    C3D_Mtx m;
    Mtx_Identity(&m);
    renderer.drawModel(const_cast<render::Model&>(room_), m);
}

void CogInterior::shutdown() {
    active_ = false;
    sentElevatorDone_ = false;
    sentReserveDone_ = false;
    room_.unload();
}

const char* CogInterior::popMessage() {
    const char* out = message_;
    message_ = nullptr;
    return out;
}

} // namespace tt::world
