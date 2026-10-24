#pragma once

#include <3ds.h>

#include "net/client.h"
#include "world/player.h"
#include "world/collision.h"
#include "render/model.h"
#include "render/renderer.h"

namespace tt::world {

class Racing {
public:
    void handleEvent(const net::Event& ev, const net::Client& client);
    void update(net::Client& client, const Input& input, float dt, float x, float y, float z, uint32_t nowMs);
    void draw(render::Renderer& renderer, const net::Client& client) const;
    void shutdown();
    bool active() const { return active_; }
    bool driving() const { return race_ != 0 && !finished_; }
    const CollisionWorld* collision() const { return collision_.loaded() ? &collision_ : nullptr; }
    uint32_t popDestination() { const uint32_t z = destination_; destination_ = 0; return z; }
    bool spawnPose(float* x, float* y, float* z, float* h);
    float speed() const { return speed_; }
    uint8_t lap() const { return lap_; }
    uint8_t place() const { return place_; }
    const char* hudTitle() const { return hudTitle_; }
    const char* hudLine(int i) const { return i >= 0 && i < 3 ? hudLines_[i] : ""; }

private:
    void loadAssets(uint16_t track);
    void unloadAssets();
    render::Model track_;
    render::Model kart_;
    render::Model pad_;
    CollisionWorld collision_;
    uint32_t race_ = 0, vehicle_ = 0, destination_ = 0;
    float x_ = 0, y_ = 0, z_ = 0, h_ = 0, speed_ = 0;
    uint8_t lap_ = 0, place_ = 1;
    bool active_ = false, finished_ = false, spawned_ = false;
    uint16_t loadedTrack_ = 0;
    char hudTitle_[32] = {};
    char hudLines_[3][48] = {};
};

} // namespace tt::world
