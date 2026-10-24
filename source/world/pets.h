#pragma once
#include "net/client.h"
#include "render/model.h"
#include "render/renderer.h"
namespace tt::world {
class Pets {
public:
    void handleEvent(const net::Event&);
    void update(const net::Client&);
    void draw(render::Renderer&, const net::Client&) const;
    void shutdown();
private:
    render::Model doodle_;
    bool loaded_ = false;
};
}
