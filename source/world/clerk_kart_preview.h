#pragma once
#include "render/model.h"
#include "render/renderer.h"
#include "world/asset_loader.h"
namespace tt::world {
class ClerkKartPreview {
public:
    void select(const int8_t *dna);
    void pump(); // main thread, after the GPU frame fence
    void draw(render::Renderer &renderer, float x, float y, float z, float heading);
    void shutdown();
    bool active() const {
        return active_;
    }
    bool failed() const {
        return failed_;
    }

private:
    bool budgets() const;
    void path(int code, char *output, int capacity) const;
    render::Model models_[7];
    int wanted_[7] = {-1, -1, -1, -1, -1, -1, -1};
    int loaded_[7] = {-1, -1, -1, -1, -1, -1, -1};
    int8_t dna_[9] = {};
    LoadTicket ticket_ = 0;
    int ticketIndex_ = 0;
    bool active_ = false;
    bool changed_ = false;
    bool failed_ = false;
};
} // namespace tt::world
