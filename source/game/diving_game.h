#pragma once
#include "world/asset_batch.h"

#include <3ds.h>
#include <citro3d.h>

#include "game/diving_logic.h"
#include "net/client.h"
#include "render/model.h"
#include "render/renderer.h"

namespace tt::game {

class DivingGame {
public:
    void pumpAssets() {
        assetBatch_.pump();
    }
    bool loadingAssets() const {
        return assetBatch_.loading();
    }
    void init(net::Client& client, uint32_t nowMs, bool autoPlay);
    void shutdown();
    void update(net::Client& client, const circlePosition& pad, u32 kDown, float dt, uint32_t nowMs);
    void draw(render::Renderer& renderer);

    float toonRoll() const;
    void toonPose(float* x, float* y, float* z, float* h) const;
    bool toonRunning() const { return true; } // swimming animation
    void camera(C3D_FVec* eye, C3D_FVec* target) const;

    // fishSpawn: the AI spawned fish `fishCode` at `spawnerId` with lane offset `offset`.
    void spawnFish(uint32_t fishCode, uint32_t spawnerId, uint32_t offset);
    // setCrabMoving(crabId, timestamp, rand1, rand2, crabX, dir); startMs is the timestamp, local.
    void moveCrab(int crabId, uint32_t startMs, int rand1, int rand2, int crabX, int dir);

    const char* line(int i) const { return (i >= 0 && i < 3) ? lines_[i] : ""; }
    bool isDone() const { return done_; }

private:
    static constexpr int kMaxFish = 24;
    static constexpr int kFishTypeCount = 6;

    world::BasicAssetBatch<14> assetBatch_;
    // A fish the AI spawned with fishSpawn (DistributedDivingGameAI.spawnFish). It crosses the
    // tank from its spawner (DistributedDivingGame.fishSpawn) and is removed when its lerp ends.
    struct Fish {
        bool active = false;
        uint8_t type = 0;    // fishcode: 0 clown, 1 PBJ, 2 bear, 3 balloon, 4 nurse, 5 piano
        uint8_t spawner = 0;
        uint16_t id = 0;     // the spawner's running count: the AI's fishArray key
        int dir = 1;
        float offset = 0.0f; // fishSpawn lane offset
        float elapsed = 0.0f;
        float x = 0.0f;
        float z = 0.0f;
    };

    bool done_ = false;
    bool autoPlay_ = false;
    uint32_t startMs_ = 0;
    float timeRemaining_ = diving::kGameDuration;

    diving::SwimmerState swimmer_;
    diving::ChestState chests_[3];
    int recoveries_ = 0;
    int score_ = 5;

    Fish fish_[kMaxFish];
    uint16_t spawnCount_[diving::kNumSpawners] = {};
    // setGameReady's King Crabs (NUMCRABS per hood) on the seabed at z -40; each asks the AI
    // (getCrabMoving) for its next walk-and-leap Sequence once the last one ends.
    static constexpr int kMaxCrabs = 3;
    struct Crab {
        float x = 0.0f, z = -40.0f;
        int direction = 1;
        bool playing = false;   // the setCrabMoving Sequence
        bool requested = false; // getCrabMoving sent, Wait(1.0) looping until the answer
        uint32_t startMs = 0;
        float wait = 0, xTime = 0, zTime = 0, fromX = 0, goalX = 0, goalZ = -40.0f;
    };
    Crab crabs_[kMaxCrabs];
    int numCrabs_ = 1;
    float speedMult_ = 1.0f; // difficultyPatterns[hood][1]
    void updateCrabs(net::Client& client, uint32_t nowMs);

    char lines_[3][48] = {};

    render::Model environModel_;
    render::Model boatModel_;
    render::Model chestModel_;
    render::Model fishModel_[kFishTypeCount];
    render::Model crabModel_;
    render::Anim crabAnim_; // kingCrab-swimLOOP, looped
    float animTime_ = 0.0f;
};

} // namespace tt::game
