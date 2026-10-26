#pragma once
#include "world/asset_batch.h"

#include <3ds.h>

#include "net/client.h"
#include "render/model.h"
#include "render/renderer.h"
#include "world/collision.h"

namespace tt::world {

class CogInterior {
public:
    void pumpAssets() {
        assetBatch_.pump();
    }
    bool loadingAssets() const {
        return assetBatch_.loading();
    }
    void handleEvent(const net::Event& ev, net::Client& client, uint32_t nowMs);
    void update(net::Client& client, float x, float y, float z, uint32_t nowMs);
    // The interior elevator owns B only while it is still accepting riders.
    void handleInput(net::Client& client, uint32_t keysDown);
    void draw(render::Renderer& renderer) const;
    void shutdown();
    bool active() const { return active_; }
    bool seated() const { return elevatorSeat_ >= 0; }
    uint32_t zoneId() const { return zoneId_; }
    const char* popMessage();
    const CollisionWorld* collision() const { return collision_.loaded() ? &collision_ : nullptr; }
    bool spawnPose(float* x, float* y, float* z, float* h);
    bool battleTarget(const net::Client& client, uint32_t* doId, float* x, float* y, float* z, float* h) const;
    bool elevatorOutPoint(float* x, float* y, float* z, float* h) const;
    bool elevatorSeatPose(float* x, float* y, float* z, float* h) const;
    int elevatorCountdown(const net::Client& client, uint32_t nowMs) const { return countdown(client, nowMs); }

private:
    struct Anchor { float x = 0, y = 0, z = 0, h = 0; bool valid = false; };
    BasicAssetBatch<13> assetBatch_;
    int loadedFloor_ = -1;
    void loadFloor(int floor);
    bool active_ = false;
    uint32_t zoneId_ = 0;
    uint32_t interiorDoId_ = 0;
    int floor_ = -1;
    int numFloors_ = 0;
    char state_[24] = {};
    bool sentElevatorDone_ = false;
    bool sentReserveDone_ = false;
    render::Model room_;
    // ElevatorUtils' elevatorModelIn/elevatorModelOut, plus the separate leaves
    // needed to reproduce its left/right door intervals.
    render::Model elevatorCab_, elevatorLeft_, elevatorRight_;
    render::Model elevatorFloorLights_[5];
    Anchor elevatorIn_, elevatorOut_;
    Anchor anchorTable_[3][2];
    bool metadataLoaded_ = false;
    CollisionWorld collision_;
    const char* message_ = nullptr;
    bool spawned_ = false;
    uint32_t lastStateStartMs_ = 0;
    bool stateInitialized_ = false;
    bool elevatorRidePending_ = false;
    uint32_t elevatorRideDoneMs_ = 0;
    bool boardingLogged_ = false;
    bool inElevatorSphere_ = false;
    bool requestedBoard_ = false;
    uint32_t requestedBoardMs_ = 0;
    int elevatorSeat_ = -1;
    uint32_t elevatorDoId_ = 0;
    uint32_t seenReject_ = 0;
    char elevatorState_[20] = {};
    uint32_t elevatorStateStartMs_ = 0;

    bool loadMetadata();
    bool accepting(const net::CogElevator& elevator) const;
    const net::CogElevator* elevator(const net::Client& client) const;
    void drawElevator(render::Renderer& renderer, const Anchor& anchor, float open) const;
    int countdown(const net::Client& client, uint32_t nowMs) const;
};

} // namespace tt::world
