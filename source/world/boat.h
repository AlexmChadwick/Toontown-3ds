// Donald's Dock boat (DistributedBoat, zone 1000): the moving ship that Donald (class
// DistributedDonaldDock) stands on. tt::net::Client parses setState(state, ts); this class
// replays the upstream track (DistributedBoat.setupTracks) from the synced timestamp.
//
// Files (tools/pipeline/bake_boat.py; docs/design/boat.md):
//   sdmc:/3ds/toontown3ds/data/zones/1000_boat.ttm   boat geometry, boat-local frame
//   source/world/boat_paths.h                        Mopath dd-e-w / dd-w-e samples (generated)
// 1000_boat.ttc (deck collision) is baked but not loaded yet: the toon does not ride the boat.
#pragma once

#include <3ds.h>
#include <citro3d.h>

#include "net/client.h"
#include "render/model.h"
#include "render/renderer.h"

namespace tt::world {

class BoatActor {
public:
    // Donald's attachment point on the boat (DistributedDonaldDock.generate: setPos(0, -1, 3.95)).
    static constexpr float kDonaldOffset[3] = {0.0f, -1.0f, 3.95f};

    void handleEvent(const net::Event& ev, const net::Client& client);
    void update(u32 nowMs);
    void draw(render::Renderer& renderer);
    void shutdown();

    // True while a DistributedBoat exists and has a pose (not "off").
    bool active() const { return active_ && state_ != net::BoatState::Off; }
    // Boat-local -> zone transform (translation + heading), valid when active() after update().
    const C3D_Mtx& matrix() const { return matrix_; }
    float x() const { return x_; }
    float y() const { return y_; }
    float z() const { return z_; }
    float heading() const { return h_; }

private:
    void pose(u32 nowMs);

    render::Model model_;
    bool active_ = false;
    u32 doId_ = 0;
    net::BoatState state_ = net::BoatState::Off;
    u32 stateStartMs_ = 0;
    float x_ = 0, y_ = 0, z_ = 0, h_ = 0;
    C3D_Mtx matrix_;
};

} // namespace tt::world
