// The playground trolley (DistributedTrolley; docs/design/trolley.md section 1): walk into the
// station's trolley_sphere while it waits, the AI seats us (fillSlotN), a 10 s countdown runs,
// and setMinigameZone hands the ride to a minigame. B hops off (requestExit).
//
// The car itself is baked into the zone as static geometry (trolley.md 1.3 "Bake gap"), so it
// does not drive in or out yet; seating, the countdown and the hand-off work.
#pragma once

#include <3ds.h>
#include <citro3d.h>

#include "net/client.h"
#include "render/model.h"
#include "render/renderer.h"

namespace tt::world {

class TrolleyRide {
public:
    // New zone: the station transform of this playground (none outside the six trolley hoods).
    void setZone(u32 zoneId);
    void handleEvent(const net::Event& ev, net::Client& client, u32 nowMs);
    // Every frame (not seated): boards when the toon at (x, y, z) walks into the station sphere
    // while the trolley waits. hp <= 0 can't board (Trolley.py "trolley HFA").
    void update(net::Client& client, float x, float y, float z, int hp, u32 nowMs);
    void hopOff(net::Client& client);
    // The car (zones/<hood>_trolley.ttm), driving in and out with the trolley's setState.
    void draw(render::Renderer& renderer, const net::Client& client, u32 nowMs);
    void shutdown() { car_.unload(); }

    bool seated() const { return seat_ >= 0; }
    void seatPose(float* x, float* y, float* z, float* h) const;
    void camera(C3D_FVec* eye, C3D_FVec* target) const;
    // Where the toon stands after climbing off seat i (station space (21 - 3i, -5, 0.02)).
    void exitPose(int seat, float* x, float* y, float* z, float* h) const;
    int countdown(const net::Client& client, u32 nowMs) const; // seconds left, -1 when not counting

    // setMinigameZone for us: the dynamic zone and minigame id, once (0 when none).
    bool popMinigame(u32* zone, u16* gameId);
    // emptySlotN for us (hopped off): the seat we left, once.
    bool popExit(int* seat) {
        if (exitSeat_ < 0) return false;
        *seat = exitSeat_;
        exitSeat_ = -1;
        return true;
    }
    // One-shot status line for a toast.
    const char* popMessage() {
        const char* m = message_;
        message_ = nullptr;
        return m;
    }
    u32 hood() const { return hood_; }
    bool hasStation() const { return has_; }
    // Test helper: a point on the station platform inside the trolley sphere.
    void boardingPoint(float* x, float* y, float* z) const { toWorld(16.19f, sphereY_ - 6.0f, 0.6f, x, y, z); }

private:
    void toWorld(float sx, float sy, float sz, float* wx, float* wy, float* wz) const;
    // Car position in station space for the trolley's current state (DistributedTrolley tracks).
    void carPos(const net::Client& client, u32 nowMs, float* x, float* y, float* z, bool* visible) const;

    bool has_ = false;
    u32 hood_ = 0;
    float px_ = 0, py_ = 0, pz_ = 0, ph_ = 0; // station node (DNA pos / H)
    float sphereY_ = 14.88f;                  // trolley_sphere centre y in station space (DG 13.88)
    int seat_ = -1;
    int exitSeat_ = -1;
    bool inSphere_ = false;  // board once per entry
    bool requested_ = false;
    u32 requestMs_ = 0;
    u32 mgZone_ = 0;
    u16 mgGame_ = 0;
    bool mgPending_ = false;
    const char* message_ = nullptr;
    render::Model car_;
    float carX_ = 15.751f, carY_ = 14.1588f, carZ_ = -0.984615f; // last drawn (seats ride with it)
};

} // namespace tt::world
