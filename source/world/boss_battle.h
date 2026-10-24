// Sellbot VP place controller. One frame hook owns simulation and both displays
// while a VP object is present; ordinary places and facilities remain independent.
#pragma once

#include "core/autotest.h"
#include "render/ui.h"
#include "world/asset_loader.h"
#include "world/battle.h"
#include "world/battle_fx.h"
#include "world/boss_logic.h"
#include "world/place.h"
#include "world/player.h"
#include "world/remote_toons.h"
#include "world/suits.h"

namespace tt::world {

struct BossFrame {
    net::Client& client;
    Player& player;
    Place& place;
    Battle& battle;
    BattleFx& effects;
    SuitActors& suits;
    RemoteToons& remotes;
    ToonAvatar& toon;
    ToonCache& cache;
    render::Renderer& renderer;
    ui::Ui& ui;
    ui::State& uiState;
    autotest::Runner& test;
    C3D_RenderTarget* bottom;
    Input input;
    touchPosition touch;
    float dt;
    const char*& shot;
    render::FrameStats& stats;
    float& commandUsage;
    bool& stereoActive;
};

class BossBattle {
public:
    bool frame(BossFrame& frame);
    ~BossBattle();

private:
    static constexpr int kParts = 4;
    static constexpr int kModels = 12;
    static constexpr int kProjectiles = 16;
    struct Projectile {
        bool active = false;
        Vec3 origin{};
        Vec3 previous{};
        float heading = 0;
        float pitch = 0;
        float power = 0;
        float age = 0;
    };
    void release();
    void leave(BossFrame& frame);
    void loadAssets();
    static bool rampFloor(void* user, float x, float y, float z, float* top);
    void selectClip(const char* name);
    void publishClip();
    void updateFight(BossFrame& frame, const net::BossState& state, uint32_t nowMs);
    void draw(BossFrame& frame, const net::BossState& state, uint32_t nowMs);
    void board(BossFrame& frame, uint32_t nowMs);
    bool active_ = false;
    bool ready_ = false;
    bool budgetChecked_ = false;
    bool failed_ = false;
    bool entered_ = false;
    bool near_ = false;
    uint32_t bossId_ = 0;
    uint32_t home_ = 11100;
    uint32_t boardMs_ = 0;
    uint32_t moveMs_ = 0;
    uint32_t cageMs_ = 0;
    uint32_t contactMs_ = 0;
    uint32_t autoPieMs_ = 0;
    uint32_t attackRevision_ = 0;
    bool attackHit_ = false;
    float charge_ = 0;
    bool charging_ = false;
    float pitch_ = 0;
    float clipTime_ = 0;
    char phase_[32] = {};
    char currentClip_[32] = {};
    char pendingClip_[32] = {};
    char failedClip_[32] = {};
    uint16_t acknowledged_[8] = {};
    bool haveAcknowledged_[8] = {};
    render::Model models_[kModels];
    render::Anim animations_[kParts];
    LoadTicket modelTicket_ = 0;
    int modelIndex_ = 0;
    LoadTicket clipTickets_[kParts] = {};
    CollisionWorld collision_;
    CollisionWorld rampCollision_[3];
    Vec3 rampRetract_[3] = {};
    Vec3 rampOffset_[3] = {};
    float rampFraction_[3] = {};
    Vec3 doorHinge_ = {};
    DynamicSolids solids_;
    Projectile pies_[kProjectiles];
    Projectile gears_[kProjectiles];
    uint32_t strafeRevision_ = 0;
    uint32_t reserveBattle_ = 0;
    uint16_t reserveContext_ = 0;
    bool reserveAcknowledged_ = false;
    BattleView battleView_;
    ToonLook rescuedLook_;
    ToonAvatar rescuedToon_;
    uint32_t rescuedNpc_ = 0;
    char status_[112] = {};
    char details_[112] = {};
};

} // namespace tt::world
