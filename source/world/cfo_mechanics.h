#pragma once

#include "net/client.h"
#include "world/asset_loader.h"
#include "world/cfo_logic.h"

namespace tt::world {
struct BossFrame;
class CfoMechanics {
public:
    void reset();
    void release();
    bool loadGoons(const render::Model& model);
    bool failed() const { return goonFailed_; }
    bool controlling(const net::Client& client) const;
    void update(BossFrame& frame, uint32_t nowMs);
    void draw(BossFrame& frame, render::Model* models, uint32_t nowMs);
    void push(float* x, float* y, float z, float radius) const;
    bool floor(float x, float y, float z, float* height) const;
    bool magnetOn() const { return magnetOn_; }
private:
    struct Object {
        uint32_t doId = 0;
        uint32_t revision = 0;
        uint32_t targetMs = 0;
        uint32_t actionMs = 0;
        uint32_t sendMs = 0;
        char state = 'O';
        Vec3 position{};
        Vec3 velocity{};
        Vec3 start{};
        Vec3 previous{};
        float heading = 0;
        bool hit = false;
        bool onHead = false;
        bool floorHit = false;
        bool grabbedStunned = false;
        net::CfoKind kind = net::CfoKind::None;
        float scale = 1;
    };
    struct Crane {
        uint32_t doId = 0;
        uint32_t owner = 0;
        uint32_t cableMs = 0;
        uint32_t sendMs = 0;
        CfoCable cable;
        Vec3 previousMagnet{};
        Vec3 velocity{};
        float remoteY = 20;
        float remoteH = 0;
        Vec3 remoteLinks[3] = {};
        uint8_t sequence = 0;
    };
    Crane cranes_[4];
    Object objects_[net::kMaxCfoChildren];
    render::Anim goonClips_[3];
    LoadTicket goonTicket_ = 0;
    int goonIndex_ = 0;
    bool goonFailed_ = false;
    bool magnetOn_ = false;
    uint32_t requestMs_ = 0;
    uint32_t attackRevision_ = 0;
    uint32_t attackHitMs_ = 0;
    bool attackActive_ = false;
    Vec3 attackOrigin_{};
    Vec3 attackVelocity_{};
    Vec3 attackPosition_{};
    float attackTime_ = 0;
};
} // namespace tt::world
