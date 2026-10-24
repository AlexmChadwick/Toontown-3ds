#pragma once

#include <3ds.h>

#include "net/client.h"
#include "render/model.h"
#include "render/renderer.h"

namespace tt::world {

class Factory {
public:
    void handleEvent(const net::Event& ev, const net::Client& client);
    void update(net::Client& client, float x, float y, float z, uint32_t nowMs);
    void draw(render::Renderer& renderer, const net::Client& client) const;
    void shutdown();
    bool active() const { return active_; }
    uint32_t zoneId() const { return zoneId_; }

private:
    struct Prop {
        render::Model model;
        net::FactoryEntityType type = net::FactoryEntityType::Unknown;
        bool loaded = false;
    };
    void loadAssets();
    void unloadAssets();
    render::Model level_;
    render::Model cell_;
    Prop props_[6];
    bool active_ = false;
    uint32_t zoneId_ = 0;
    uint32_t lastInteraction_ = 0;
};

} // namespace tt::world
