#pragma once
#include "render/model.h"
#include "render/renderer.h"
#include "world/asset_loader.h"
namespace tt::world {
class ClerkKartPreview {
  public:
    void select(const int8_t *dna);
    // Racing reuses the same verified components, with upstream LOD 0/1/2.
    void setRacingLod(int lod);
    void pump(); // main thread, after the GPU frame fence
    void draw(render::Renderer &renderer, float x, float y, float z, float heading, float pitch = 0,
              float roll = 0, float scale = 1);
    void shutdown();
    bool active() const { return active_; }
    bool ready() const {
        if (!active_ || changed_ || ticket_ || failed_)
            return false;
        for (int i = 0; i < 7; ++i)
            if (loaded_[i] != wanted_[i])
                return false;
        return true;
    }
    bool failed() const { return failed_; }

  private:
    int racingLod_ = -1;
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
