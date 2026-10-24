#pragma once

#include <3ds.h>

#include "net/client.h"
#include "render/model.h"
#include "render/renderer.h"

namespace tt::world {

class CogInterior {
public:
    void handleEvent(const net::Event& ev, net::Client& client, uint32_t nowMs);
    void update(net::Client& client, uint32_t nowMs);
    void draw(render::Renderer& renderer) const;
    void shutdown();
    bool active() const { return active_; }
    uint32_t zoneId() const { return zoneId_; }
    const char* popMessage();

private:
    void loadFloor(int floor);
    bool active_ = false;
    uint32_t zoneId_ = 0;
    uint32_t interiorDoId_ = 0;
    int floor_ = 0;
    char state_[24] = {};
    bool sentElevatorDone_ = false;
    bool sentReserveDone_ = false;
    render::Model room_;
    const char* message_ = nullptr;
};

} // namespace tt::world
