#pragma once
#include "net/client.h"
#include "render/model.h"
#include "render/renderer.h"
#include "world/collision.h"
namespace tt::world {
class MintOffice {
public:
    void handleEvent(const net::Event&, const net::Client&);
    void update(net::Client&, float, float, float, uint32_t);
    void draw(render::Renderer&, const net::Client&) const;
    void shutdown();
    bool active() const { return active_; }
    const CollisionWorld* collision() const { return collision_.loaded() ? &collision_ : nullptr; }
    bool spawnPose(float* x, float* y, float* z, float* h);
    bool battleTarget(const net::Client& client, uint32_t* doId, float* x, float* y, float* z, float* h) const;
private:
    void unload();
    render::Model room_;
    render::Model prop_;
    CollisionWorld collision_;
    bool active_ = false, mint_ = false;
    uint16_t roomId_ = 0xffff;
    uint32_t lastInteract_ = 0;
    bool spawned_ = false;
};
}
