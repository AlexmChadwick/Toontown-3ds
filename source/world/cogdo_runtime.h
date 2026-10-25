#pragma once
#include "world/boss_battle.h"
#include "world/cogdo_assets.h"
#include "world/cogdo_crane.h"
#include "world/cogdo_maze.h"
namespace tt::world {
// Reuse the place-frame references; no changes to the boss controllers.
class CogdoRuntime {
public:
    bool frame(BossFrame &frame);

private:
    void board(BossFrame &frame);
    void leave(BossFrame &frame);
    void simulate(BossFrame &frame, const net::CogdoState &interior, const net::CogdoState *game, uint32_t now);
    void maze(BossFrame &frame, const net::CogdoState &game, uint32_t now, Input *movement);
    void crane(BossFrame &frame, const net::CogdoState &game, uint32_t now);
    void draw(BossFrame &frame, const net::CogdoState &interior, const net::CogdoState *game, uint32_t now);
    CogdoScene select(const net::CogdoState &interior, const net::CogdoState *game) const;
    CogdoAssets assets_;
    CogdoMaze maze_;
    CogdoCable cables_[4];
    struct Bag {
        uint32_t id = 0;
        uint32_t revision = 0;
        Vec3 position{};
        Vec3 velocity{};
        char state = 0;
        bool hitFloor = false;
        bool freed = false;
    } bags_[8];
    struct Memo {
        Vec3 position{};
        uint32_t requested = 0;
    } memos_[256];
    int numMemos_ = 0;
    uint8_t deaths_ = 0;
    bool active_ = false;
    bool spawned_ = false;
    bool mazeReady_ = false;
    bool logicFailed_ = false;
    bool acknowledged_ = false;
    bool introReady_ = false;
    bool enteredDoor_ = false;
    bool magnet_ = false;
    uint32_t interior_ = 0;
    uint32_t game_ = 0;
    uint32_t exterior_ = 2100;
    uint32_t building_ = 0;
    uint32_t victor_ = 0;
    uint32_t moveMs_ = 0;
    uint32_t actionMs_ = 0;
    uint32_t contactMs_ = 0;
    uint32_t stateMs_ = 0;
    char state_[32] = {};
    Vec3 exit_{};
    float exitHeading_ = 0;
    bool exitValid_ = false;
    bool balloon_ = false;
    Vec3 balloonOrigin_{};
    Vec3 balloonPosition_{};
    float balloonHeading_ = 0;
    float balloonAge_ = 0;
    BattleView battleView_;
    ToonLook ownerLook_;
    ToonAvatar ownerToon_;
    uint32_t ownerNpc_ = 0;
    char title_[80] = {};
    char status_[112] = {};
    char detail_[112] = {};
};
} // namespace tt::world
