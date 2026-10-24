#pragma once

#include <cstdint>
#include "net/client.h"
#include "render/model.h"

namespace tt::render {
class Renderer;
}

namespace tt::world {

class Battle;
class BattleFx;

struct BattleTrapState {
    bool active = false;
    int level = -1; // 0: banana, 1: rake, 2: marbles, 3: quicksand, 4: trapdoor, 5: tnt, 6: railroad
    uint32_t suitId = 0;
    float x = 0.0f, y = 0.0f, z = 0.0f, h = 0.0f;
    uint32_t placedMs = 0;
    bool triggered = false;
    uint32_t triggerStartMs = 0;
    uint32_t triggerDurMs = 0;
};

struct BattleDropState {
    bool active = false;
    int level = 0; // 0..6 (flowerpot..ship)
    uint32_t suitId = 0;
    uint32_t startMs = 0;
    uint32_t shadowMs = 0;
    uint32_t fallMs = 0;
    uint32_t impactMs = 0;
    bool hit = true;
    int hp = 0;
    bool whistlePlayed = false;
    bool impactPlayed = false;
    float targetX = 0.0f, targetY = 0.0f, targetZ = 0.0f;
};

struct BattleLureState {
    bool active = false;
    uint32_t suitId = 0;
    float currOffset = 0.0f;
    float targetOffset = 0.0f;
    uint32_t walkStartMs = 0;
    uint32_t walkDurMs = 500;
};

class BattleMovies {
public:
    BattleMovies() = default;
    ~BattleMovies() { shutdown(); }

    bool init();
    void shutdown();
    void reset();

    // Trap queries and controls
    void placeTrap(uint32_t suitId, int level, uint32_t nowMs, const Battle& battle);
    bool triggerTrap(uint32_t suitId, uint32_t nowMs, BattleFx& fx);
    void clearTrap(uint32_t suitId);
    bool hasTrap(uint32_t suitId) const;
    int trapLevel(uint32_t suitId) const;
    const BattleTrapState* getTrap(uint32_t suitId) const;

    // Lure controls
    void setLured(uint32_t suitId, bool lured, uint32_t nowMs);
    float getLureOffset(uint32_t suitId) const;

    // Drop controls
    void spawnDrop(uint32_t suitId, int level, uint32_t startMs, uint32_t impactMs, bool hit, int hp, const Battle& battle);

    // Event hooks
    void onAttack(uint32_t attackerId, uint32_t targetId, int track, int level,
                  uint32_t startMs, uint32_t durMs, uint32_t hitMs, BattleFx& fx, const Battle& battle);
    void onHit(uint32_t targetId, uint32_t attackerId, int track, int level, int hp, bool miss,
               BattleFx& fx, const Battle& battle);

    // Frame update and 3D rendering
    void update(float dt, uint32_t nowMs, const Battle& battle, BattleFx& fx);
    void draw(render::Renderer& renderer, const Battle& battle, BattleFx& fx);

    render::Model* getPropModel(int track, int level);

private:
    render::Model bananaModel_;
    render::Model rakeModel_;
    render::Model marblesModel_;
    render::Model quicksandModel_;
    render::Model trapdoorModel_;
    render::Model tntModel_;
    render::Model traintrackModel_;

    render::Model dollarModel_;
    render::Model bigMagnetModel_;
    render::Model hypnoGogglesModel_;
    render::Model slideshowModel_;

    render::Model bikehornModel_;
    render::Model whistleModel_;
    render::Model bugleModel_;
    render::Model aoogahModel_;
    render::Model elephantModel_;
    render::Model foghornModel_;
    render::Model singingModel_;

    render::Model flowerpotModel_;
    render::Model sandbagModel_;
    render::Model anvilModel_;
    render::Model weightModel_;
    render::Model safeModel_;
    render::Model pianoModel_;
    render::Model shipModel_;

    render::Model featherModel_;
    render::Model megaphoneModel_;
    render::Model lipstickModel_;
    render::Model caneModel_;
    render::Model hatModel_;
    render::Model cubesModel_;
    render::Model ladderModel_;

    render::Model phoneModel_;
    render::Model receiverModel_;
    render::Model dropShadowModel_;

    static constexpr int kMaxTraps = net::kBattleSuits;
    BattleTrapState traps_[kMaxTraps] = {};

    static constexpr int kMaxDrops = 4;
    BattleDropState drops_[kMaxDrops] = {};

    static constexpr int kMaxLures = net::kBattleSuits;
    BattleLureState lures_[kMaxLures] = {};

    uint32_t nowMs_ = 0;
    bool modelsLoaded_ = false;
};

} // namespace tt::world
