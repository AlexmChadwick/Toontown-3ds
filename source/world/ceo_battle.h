#pragma once
#include "world/ceo_assets.h"
#include "world/battle.h"
#include "world/toon_avatar.h"
namespace tt::world {
struct BossFrame;
class CeoBattle {
public:
    bool frame(BossFrame& frame);
private:
    struct Shot {
        bool active = false;
        uint32_t owner = 0;
        uint32_t object = 0;
        uint8_t attack = 0;
        Vec3 origin{};
        Vec3 target{};
        Vec3 previous{};
        float age = 0;
        float duration = 0;
        float heading = 0;
        uint8_t power = 0;
        bool golf = false;
    };
    void board(BossFrame& frame, uint32_t nowMs);
    void begin(BossFrame& frame, const net::BossState& state);
    void leave(BossFrame& frame, bool sad);
    void update(BossFrame& frame, const net::BossState& state, uint32_t nowMs);
    void banquet(BossFrame& frame, const net::BossState& state, uint32_t nowMs);
    void court(BossFrame& frame, const net::BossState& state, uint32_t nowMs);
    void draw(BossFrame& frame, const net::BossState& state, uint32_t nowMs);
    void drawBoss(BossFrame& frame, const net::BossState& state);
    Vec3 pitcher(int table) const;
    CeoAssets assets_;
    bool active_ = false;
    bool entered_ = false;
    uint32_t bossId_ = 0;
    uint32_t boardZone_ = 0;
    bool anchorValid_ = false;
    Vec3 anchor_{};
    uint32_t boardMs_ = 0;
    uint32_t moveMs_ = 0;
    uint32_t actionMs_ = 0;
    uint32_t aimMs_ = 0;
    uint32_t attackRevision_ = 0;
    uint32_t zapMs_ = 0;
    uint32_t reserveBattle_ = 0;
    uint16_t reserveContext_ = 0;
    bool reserveAck_ = false;
    uint16_t barriers_[8] = {};
    bool acknowledged_[8] = {};
    uint32_t waterRevisions_[13] = {};
    uint32_t golfRevisions_[4] = {};
    uint32_t dinerIds_[8] = {};
    uint32_t usedFood_[2][4] = {};
    uint32_t usedToonup_[2][4] = {};
    char phase_[32] = {};
    Vec3 bossPosition_{0,355,0};
    Vec3 attackTarget_{};
    float bossHeading_ = 0;
    float turnRemaining_ = 0;
    float heading_ = 0;
    float charge_ = 0;
    bool charging_ = false;
    bool reached_ = false;
    bool tableHit_ = false;
    bool away_ = false;
    uint8_t sequence_ = 0;
    uint32_t controlledId_ = 0;
    Shot shots_[32];
    BattleView battleView_;
    char status_[112] = {};
    char details_[112] = {};
};
} // namespace tt::world
