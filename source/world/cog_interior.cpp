#include "world/cog_interior.h"

#include <cstdio>
#include <cstring>

#include "core/log.h"

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
    const bool newState = !stateInitialized_ || strcmp(state_, i->state) != 0 ||
                          lastStateStartMs_ != i->stateStartMs;
    const char* previousState = state_;
    if (newState && !strcmp(i->state, "Elevator") && strcmp(previousState, "Elevator") && floor_ + 1 < i->numFloors) {
        ++floor_;
        TT_LOG("cog_interior: floor %d/%d", floor_ + 1, int(i->numFloors));
    }
    zoneId_ = i->zoneId;
    memcpy(state_, i->state, sizeof(state_) - 1);
    state_[sizeof(state_) - 1] = 0;
    if (newState) {
        TT_LOG("cog_interior: state %s floor %d", state_, floor_ + 1);
        if (!strcmp(state_, "Reward")) TT_LOG("cog_interior: reward");
        sentElevatorDone_ = false;
        sentReserveDone_ = false;
        elevatorRidePending_ = !strcmp(state_, "Elevator");
        // ElevatorUtils.getRideElevatorInterval (normal): 4.5 s, followed by
        // the 2 s normal door-open interval; ElevatorConstants.py.
        elevatorRideDoneMs_ = i->stateStartMs + 6500;
        stateInitialized_ = true;
    }
    lastStateStartMs_ = i->stateStartMs;
    if (floor_ < 0) floor_ = 0;
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

void CogInterior::update(net::Client& client, uint32_t nowMs) {
    const net::CogInterior* i = client.cogInterior();
    if (!i) { active_ = false; room_.unload(); return; }
    if (!boardingLogged_) {
        for (int n = 0; n < net::kMaxCogElevators; ++n) {
            const net::CogElevator& elevator = client.cogElevatorSlot(n);
            if (!elevator.active) continue;
            for (uint32_t avatar : elevator.slots) {
                if (avatar == client.avatarId() && avatar != 0) {
                    TT_LOG("cog_interior: boarded elevator %lu", static_cast<unsigned long>(elevator.doId));
                    boardingLogged_ = true;
                    break;
                }
            }
            if (boardingLogged_) break;
        }
    }
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
            TT_LOG("cog_interior: floor %d/%d", floor_ + 1, int(i->numFloors));
        }
        sentElevatorDone_ = false;
        sentReserveDone_ = false;
        loadFloor(floor_);
        lastStateStartMs_ = i->stateStartMs;
        stateInitialized_ = true;
        elevatorRidePending_ = !strcmp(state_, "Elevator");
        elevatorRideDoneMs_ = i->stateStartMs + 6500;
        TT_LOG("cog_interior: state %s floor %d", state_, floor_ + 1);
    }

    if (elevatorRidePending_ && int32_t(nowMs - elevatorRideDoneMs_) >= 0 && !sentElevatorDone_) {
        sentElevatorDone_ = client.sendCogInteriorElevatorDone(i->doId);
        elevatorRidePending_ = false;
        if (sentElevatorDone_) TT_LOG("cog_interior: elevatorDone floor %d", floor_ + 1);
    } else if (!strcmp(state_, "ReservesJoining") && !sentReserveDone_) {
        sentReserveDone_ = client.sendCogInteriorReserveJoinDone(i->doId);
        if (sentReserveDone_) TT_LOG("cog_interior: reserveJoinDone floor %d", floor_ + 1);
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
    stateInitialized_ = false;
    elevatorRidePending_ = false;
    boardingLogged_ = false;
}

const char* CogInterior::popMessage() {
    const char* out = message_;
    message_ = nullptr;
    return out;
}

} // namespace tt::world
