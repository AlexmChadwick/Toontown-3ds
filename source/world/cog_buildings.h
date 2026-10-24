#pragma once

#include <3ds.h>

#include "net/client.h"
#include "render/model.h"
#include "render/renderer.h"

namespace tt::world {
class Place;

class CogBuildings {
public:
    void setZone(uint32_t zone);
    void syncAnchors(const Place& place);
    void handleEvent(const net::Event& ev, const net::Client& client);
    void update(net::Client& client, float x, float y, float z, uint32_t nowMs);
    void draw(render::Renderer& renderer, const net::Client& client) const;
    void setAnchor(uint32_t block, float x, float y, float z, float h);
    void shutdown();
    const char* popMessage();
    // Where the elevator we rode is taking us (a Cog building interior or a factory / mint /
    // DA office zone), once; 0 when not leaving. The caller changes zone.
    uint32_t popDestination() {
        const uint32_t d = pendingDest_;
        pendingDest_ = 0;
        return d;
    }

private:
    struct Anchor { uint32_t block = 0; float x = 0, y = 0, z = 0, h = 0; bool used = false; };
    struct Shell { render::Model model; int8_t dept = -1; bool loaded = false; };
    static constexpr int kMaxAnchors = 16;
    Anchor anchors_[kMaxAnchors] = {};
    Shell shells_[4];
    uint32_t zone_ = 0;
    uint32_t requestedElevator_ = 0;
    // Cog HQ facility elevators: elevator_origin_<n> anchors of the HQ zone (.pts).
    struct Spot { float x = 0, y = 0, z = 0; bool used = false; };
    static constexpr int kMaxSpots = 8;
    Spot spots_[kMaxSpots] = {};
    uint32_t pendingDest_ = 0, lastDest_ = 0;
    uint32_t requestMs_ = 0;
    const char* message_ = nullptr;
};

} // namespace tt::world
