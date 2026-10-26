#include "world/cog_interior.h"

#include <cstdio>
#include <cstring>
#include <cmath>

#include "core/log.h"

namespace tt::world {
namespace {
constexpr float kRadians = 3.14159265f / 180.0f;
constexpr float kSphereRadius = 5.0f; // ElevatorData[ELEVATOR_NORMAL]['collRadius']
constexpr uint32_t kBoardRetryMs = 5000;
constexpr int kInteriorCountdownSec = 90;

bool stateIs(const char* state, const char* value) {
    return !strcmp(state, value);
}

float clamp01(float f) { return f < 0 ? 0 : f > 1 ? 1 : f; }
} // namespace

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
        floor_ = -1;
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
    numFloors_ = i->numFloors;
    if (floor_ >= 0) loadFloor(floor_);
    if (ev.type == net::EventType::CogInteriorUp) client.sendCogInteriorAvatarJoined(interiorDoId_);
}

void CogInterior::loadFloor(int floor) {
    // State changes on one floor do not change immutable baked geometry.
    if (loadedFloor_ == floor)
        return;
    loadedFloor_ = floor;
    if (!metadataLoaded_) loadMetadata();
    // DistributedSuitInterior chooses its first, middle and final modules by
    // currentFloor, not a cyclic sequence.
    const int layout = floor == 0 ? 0 : floor + 1 >= numFloors_ ? 2 : 1;
    elevatorIn_ = anchorTable_[layout][0];
    elevatorOut_ = anchorTable_[layout][1];
    char path[112];
    snprintf(path, sizeof(path), "sdmc:/3ds/toontown3ds/data/cogbldg/floor_%d.ttm", layout);
    assetBatch_.load(room_, path);
    assetBatch_.load(elevatorCab_, "sdmc:/3ds/toontown3ds/data/cogbldg/elevator_cab.ttm");
    assetBatch_.load(elevatorLeft_, "sdmc:/3ds/toontown3ds/data/cogbldg/elevator_left-door.ttm");
    assetBatch_.load(elevatorRight_, "sdmc:/3ds/toontown3ds/data/cogbldg/elevator_right-door.ttm");
    for (int light = 0; light < 5; ++light) {
        char lightPath[112];
        snprintf(lightPath, sizeof(lightPath),
                 "sdmc:/3ds/toontown3ds/data/cogbldg/elevator_floor_light_%d.ttm", light + 1);
        assetBatch_.load(elevatorFloorLights_[light], lightPath);
    }
    char collisionPath[128];
    snprintf(collisionPath, sizeof(collisionPath),
             "sdmc:/3ds/toontown3ds/data/cogbldg/floor_%d.ttc", layout);
    collision_.unload();
    collision_.load(collisionPath);
}

bool CogInterior::loadMetadata() {
    metadataLoaded_ = true;
    FILE* file = fopen("sdmc:/3ds/toontown3ds/data/cogbldg/cogbldg.tcb", "rb");
    char magic[4] = {};
    uint16_t version = 0, count = 0;
    bool ok = file && fread(magic, 1, 4, file) == 4 && !memcmp(magic, "TCB1", 4) &&
              fread(&version, sizeof(version), 1, file) == 1 && version == 1 &&
              fread(&count, sizeof(count), 1, file) == 1 && count == 3;
    for (int floor = 0; ok && floor < 3; ++floor) {
        for (int door = 0; ok && door < 2; ++door) {
            float pose[6] = {};
            ok = fread(pose, sizeof(pose), 1, file) == 1;
            for (float v : pose) ok &= std::isfinite(v);
            if (ok) anchorTable_[floor][door] = {pose[0], pose[1], pose[2], pose[3], true};
        }
    }
    if (file) fclose(file);
    if (!ok) TT_LOG("cog_interior: invalid cogbldg.tcb");
    return ok;
}

const net::CogElevator* CogInterior::elevator(const net::Client& client) const {
    const net::CogInterior* interior = client.cogInterior();
    for (int n = 0; n < net::kMaxCogElevators; ++n) {
        const net::CogElevator& e = client.cogElevatorSlot(n);
        if (e.active && e.classId == dc::DistributedElevatorInt::kClassId &&
            (!interior || !interior->buildingDoId || e.buildingDoId == interior->buildingDoId)) return &e;
    }
    return nullptr;
}

bool CogInterior::accepting(const net::CogElevator& e) const {
    // Elevator.py accepts only while its doors are open: waitEmpty or its
    // countdown.  The server owns the slot limit and final transition.
    return stateIs(e.state, "waitEmpty") || stateIs(e.state, "waitCountdown") ||
           stateIs(e.state, "opening");
}

int CogInterior::countdown(const net::Client& client, uint32_t nowMs) const {
    const net::CogElevator* e = elevator(client);
    if (!e || !stateIs(e->state, "waitCountdown")) return -1;
    const int left = kInteriorCountdownSec - int(int32_t(nowMs - e->stateStartMs) / 1000);
    return left > 0 ? left : 0;
}

void CogInterior::update(net::Client& client, float x, float y, float z, uint32_t nowMs) {
    const net::CogInterior* i = client.cogInterior();
    if (!i) { active_ = false; room_.unload(); return; }
    numFloors_ = i->numFloors;
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
    const net::CogElevator* e = elevator(client);
    if (e) {
        elevatorDoId_ = e->doId;
        memcpy(elevatorState_, e->state, sizeof(elevatorState_) - 1);
        elevatorState_[sizeof(elevatorState_) - 1] = 0;
        elevatorStateStartMs_ = e->stateStartMs;
        if (e->rejectSerial != seenReject_) {
            seenReject_ = e->rejectSerial;
            if (e->rejectAvatar == client.avatarId()) {
                requestedBoard_ = false;
                message_ = e->rejectReason == 2 ? "You need more Laff points for this elevator." : "This elevator is full.";
                TT_LOG("cog_interior: rejectBoard elevator %lu reason %u", static_cast<unsigned long>(e->doId), unsigned(e->rejectReason));
            }
        }
        for (int slot = 0; slot < 8; ++slot) {
            if (e->slots[slot] == client.avatarId() && client.avatarId()) {
                if (elevatorSeat_ != slot) TT_LOG("cog_interior: fillSlot %d elevator %lu", slot, static_cast<unsigned long>(e->doId));
                elevatorSeat_ = slot;
                requestedBoard_ = false;
            }
        }
        if (elevatorSeat_ >= 0 && e->slots[elevatorSeat_] != client.avatarId()) {
            TT_LOG("cog_interior: emptySlot %d elevator %lu", elevatorSeat_, static_cast<unsigned long>(e->doId));
            elevatorSeat_ = -1;
        }
        if (elevatorOut_.valid && elevatorSeat_ < 0) {
            // DistributedElevator.setupElevator: CollisionSphere(0, 5, 0, 5),
            // with GravityWalker's event sphere inflated by the toon radius.
            const float c = cosf(elevatorOut_.h * kRadians), s = sinf(elevatorOut_.h * kRadians);
            const float cx = elevatorOut_.x - 5.0f * s;
            const float cy = elevatorOut_.y + 5.0f * c;
            const float r = kSphereRadius + kToonRadius * 1.04f;
            const float dx = x - cx, dy = y - cy, dz = z + kToonRadius - 0.1f - elevatorOut_.z;
            const bool inside = dx * dx + dy * dy + dz * dz < r * r;
            if (inside && !inElevatorSphere_ && accepting(*e) && !requestedBoard_ &&
                client.sendCogElevatorBoard(e->doId)) {
                requestedBoard_ = true;
                requestedBoardMs_ = nowMs;
                TT_LOG("cog_interior: requestBoard elevator %lu", static_cast<unsigned long>(e->doId));
            }
            inElevatorSphere_ = inside;
        }
    }
    else inElevatorSphere_ = false;
    if (requestedBoard_ && nowMs - requestedBoardMs_ >= kBoardRetryMs) requestedBoard_ = false;
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

void CogInterior::handleInput(net::Client& client, uint32_t keysDown) {
    if (!(keysDown & KEY_B) || elevatorSeat_ < 0 || !elevatorDoId_) return;
    const net::CogElevator* e = elevator(client);
    if (e && accepting(*e) && client.sendCogElevatorExit(elevatorDoId_)) {
        TT_LOG("cog_interior: requestExit elevator %lu", static_cast<unsigned long>(elevatorDoId_));
    } else if (client.sendCogInteriorBuildingExit(elevatorDoId_)) {
        TT_LOG("cog_interior: requestBuildingExit");
    }
}

bool CogInterior::elevatorOutPoint(float* x, float* y, float* z, float* h) const {
    if (!active_ || !elevatorOut_.valid) return false;
    const float c = cosf(elevatorOut_.h * kRadians), s = sinf(elevatorOut_.h * kRadians);
    *x = elevatorOut_.x - 3.0f * s;
    *y = elevatorOut_.y + 3.0f * c;
    *z = elevatorOut_.z;
    *h = elevatorOut_.h;
    return true;
}

bool CogInterior::elevatorSeatPose(float* x, float* y, float* z, float* h) const {
    if (elevatorSeat_ < 0 || !elevatorOut_.valid) return false;
    // ElevatorConstants.ElevatorPoints (normal car), local to elevatorModelOut.
    static constexpr float kPoints[8][3] = {
        {-1.5f, 5, .1f}, {1.5f, 5, .1f}, {-2.5f, 3, .1f}, {2.5f, 3, .1f},
        {-3.5f, 5, .1f}, {3.5f, 5, .1f}, {-4, 3, .1f}, {4, 3, .1f},
    };
    const float c = cosf(elevatorOut_.h * kRadians), s = sinf(elevatorOut_.h * kRadians);
    const float* p = kPoints[elevatorSeat_];
    *x = elevatorOut_.x + p[0] * c - p[1] * s;
    *y = elevatorOut_.y + p[0] * s + p[1] * c;
    *z = elevatorOut_.z + p[2];
    *h = elevatorOut_.h + 180.0f;
    return true;
}

bool CogInterior::spawnPose(float* x, float* y, float* z, float* h) {
    if (!active_ || spawned_) return false;
    spawned_ = true;
    if (elevatorIn_.valid) {
        const float c = cosf(elevatorIn_.h * kRadians), s = sinf(elevatorIn_.h * kRadians);
        *x = elevatorIn_.x - 2.0f * s;
        *y = elevatorIn_.y + 2.0f * c;
        *z = elevatorIn_.z;
        *h = elevatorIn_.h;
    } else {
        *x = 0.0f; *y = -10.0f; *z = 0.0f; *h = 0.0f;
    }
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
    const uint32_t nowMs = net::plat::nowMs();
    // The in car arrives during the interior's Elevator interval; the out car
    // follows DistributedElevator's opening/waiting/closing FSM.
    float inOpen = 0.0f;
    if (stateIs(state_, "Elevator"))
        inOpen = clamp01((int32_t(nowMs - lastStateStartMs_) - 4500) / 2000.0f);
    float outOpen = 0.0f;
    const float elevatorAge = int32_t(nowMs - elevatorStateStartMs_) / 1000.0f;
    if (stateIs(elevatorState_, "opening")) outOpen = clamp01(elevatorAge / 2.0f);
    else if (stateIs(elevatorState_, "waitEmpty") || stateIs(elevatorState_, "waitCountdown")) outOpen = 1.0f;
    else if (stateIs(elevatorState_, "closing")) outOpen = 1.0f - clamp01(elevatorAge / 2.0f);
    drawElevator(renderer, elevatorIn_, inOpen);
    drawElevator(renderer, elevatorOut_, outOpen);
}

void CogInterior::drawElevator(render::Renderer& renderer, const Anchor& a, float open) const {
    if (!a.valid || !elevatorCab_.loaded()) return;
    C3D_Mtx cab;
    Mtx_Identity(&cab);
    Mtx_Translate(&cab, a.x, a.y, a.z, true);
    Mtx_RotateZ(&cab, a.h * kRadians, true);
    renderer.drawModel(const_cast<render::Model&>(elevatorCab_), cab);

    // ElevatorUtils closes left at +3.5 and right at -3.5; open is (0, 0).
    for (int side = 0; side < 2; ++side) {
        render::Model& leaf = side ? const_cast<render::Model&>(elevatorRight_)
                                   : const_cast<render::Model&>(elevatorLeft_);
        if (!leaf.loaded()) continue;
        C3D_Mtx door = cab;
        Mtx_Translate(&door, (side ? -1.0f : 1.0f) * 3.5f * (1.0f - open), 0, 0, true);
        renderer.drawModel(leaf, door);
    }
    // ElevatorUtils lights the numbered floor button.  The nodes are baked
    // separately so the inactive lights are absent rather than merely dimmed.
    render::Model& light = const_cast<render::Model&>(elevatorFloorLights_[floor_ % 5]);
    if (light.loaded()) renderer.drawModel(light, cab);
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
    inElevatorSphere_ = false;
    requestedBoard_ = false;
    elevatorSeat_ = -1;
    elevatorDoId_ = 0;
    elevatorCab_.unload();
    elevatorLeft_.unload();
    elevatorRight_.unload();
    for (render::Model& light : elevatorFloorLights_) light.unload();
}

const char* CogInterior::popMessage() {
    const char* out = message_;
    message_ = nullptr;
    return out;
}

} // namespace tt::world
