#pragma once
#include "world/asset_loader.h"
#include "world/collision.h"
#include "world/ceo_logic.h"

namespace tt::world {
class CeoAssets {
public:
    static constexpr int kModels = 12;
    static constexpr uint32_t kClipLimit = 1600000; // Enforced aggregate active/pending cap, checked against model geometry.
    ~CeoAssets();
    void release();
    void pump(const char* clip); // Main thread, after FrameBegin GPU fence.
    bool ready() const { return ready_; }
    bool failed() const { return failed_; }
    render::Model models[kModels];
    render::Anim animations[4];
    CollisionWorld collision;
    CeoLayout layout{};
    float clipTime = 0;
    char currentClip[32] = {};
private:
    bool ready_ = false;
    bool failed_ = false;
    bool checked_ = false;
    int modelIndex_ = 0;
    LoadTicket modelTicket_ = 0;
    LoadTicket clipTickets_[4] = {};
    char pendingClip_[32] = {};
    char failedClip_[32] = {};
};
} // namespace tt::world
