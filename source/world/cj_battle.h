// Independent Lawbot encounter; VP/CFO boarding is owned by another checkout.
#pragma once
#include "world/cj_assets.h"
#include "world/cj_logic.h"
#include "world/battle.h"
#include "world/toon_cache.h"
#include "world/toon_avatar.h"

namespace tt::world {
struct BossFrame;
class CjBattle {
public:
    bool frame(BossFrame& frame);
private:
    struct JurorFlight {
        bool active = false;
        uint32_t cannonId = 0;
        uint32_t owner = 0;
        uint32_t startMs = 0;
        uint32_t landedMs = 0;
        int npc = 0;
        CjFlight trajectory{};
        Vec3 previous{};
        Vec3 position{};
    };
    struct Evidence {
        bool active = false;
        Vec3 origin{};
        Vec3 previous{};
        float heading = 0;
        float pitch = 0;
        float age = 0;
        float power = 0;
    };
    void board(BossFrame& frame, uint32_t nowMs);
    void begin(BossFrame& frame, const net::BossState& state);
    void leave(BossFrame& frame, bool sad);
    void update(BossFrame& frame, const net::BossState& state, uint32_t nowMs);
    void updateCannons(BossFrame& frame, const net::BossState& state, uint32_t nowMs);
    void updateCourt(BossFrame& frame, const net::BossState& state, uint32_t nowMs);
    void draw(BossFrame& frame, const net::BossState& state, uint32_t nowMs);
    void drawBoss(BossFrame& frame, const net::BossState& state);
    void loadNpcs(BossFrame& frame);
    Vec3 panCenter(int model, float damage) const;
    CjAssets assets_;
    bool active_ = false;
    bool entered_ = false;
    bool defeated_ = false;
    uint32_t bossId_ = 0;
    uint32_t boardZone_ = 0;
    bool anchorValid_ = false;
    Vec3 anchor_{};
    uint32_t boardMs_ = 0;
    uint32_t moveMs_ = 0;
    uint32_t witnessMs_ = 0;
    uint32_t throwMs_ = 0;
    uint32_t aimMs_ = 0;
    uint32_t cannonRequestMs_ = 0;
    uint32_t cannonFire_[8] = {};
    uint32_t lawyerAttack_[10] = {};
    uint32_t lawyerHit_[10] = {};
    uint32_t gavelHit_[8] = {};
    uint32_t bossAttack_ = 0;
    bool bossAttackHit_ = false;
    uint32_t reserveBattle_ = 0;
    uint16_t reserveContext_ = 0;
    bool reserveAck_ = false;
    uint16_t barriers_[8] = {};
    bool acknowledged_[8] = {};
    char phase_[32] = {};
    float rotation_ = 0;
    float elevation_ = 25;
    float pitch_ = 0;
    float charge_ = 0;
    bool charging_ = false;
    bool npcsLoaded_ = false;
    int autoChair_ = 0;
    ToonLook looks_[9];
    ToonAvatar npcs_[9];
    JurorFlight flights_[kCjMaxFlights];
    Evidence evidence_[16];
    BattleView battleView_;
    char status_[112] = {};
    char details_[112] = {};
};
} // namespace tt::world
