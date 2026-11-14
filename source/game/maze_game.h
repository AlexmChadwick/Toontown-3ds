#pragma once
#include "world/asset_batch.h"

#include <3ds.h>
#include <citro3d.h>

#include "game/maze_logic.h"
#include "game/minigame_cog.h"
#include "game/panda_rng.h"
#include "net/client.h"
#include "render/model.h"
#include "render/renderer.h"

namespace tt::game {

// DistributedMazeGame: the maze for the player count, the shuffled start spots, 4 Flunkies per
// player on MazeSuit's seeded walk, MazeTreasures, and the hit-by-suit fly to a random tile.
class MazeGame {
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
    // setTreasureGrabbed from the AI (another player's grab hides it here too).
    void onTreasureGrabbed(uint32_t avId, uint32_t index);

    void toonPose(float* x, float* y, float* z, float* h) const;
    int cogs(MinigameCog* out, int max) const;
    bool toonRunning() const { return isRunning_ && !flying_; }
    void camera(C3D_FVec* eye, C3D_FVec* target) const;

    const char* line(int i) const { return (i >= 0 && i < 3) ? lines_[i] : ""; }
    bool isDone() const { return done_; }

private:
    void startHit(net::Client& client, uint32_t nowMs);

    world::BasicAssetBatch<8> assetBatch_;
    bool done_ = false;
    bool autoPlay_ = false;
    bool isRunning_ = false;
    bool started_ = false;
    uint32_t avId_ = 0;

    float toonX_ = 0.0f;
    float toonY_ = 0.0f;
    float toonZ_ = 0.0f;
    float toonH_ = 180.0f;

    int numTreasures_ = 0;
    bool treasureTaken_[maze::kMaxTreasures] = {};
    int treasuresCollected_ = 0; // ours
    int treasuresGone_ = 0;      // everyone's (the goal bar)
    int score_ = 0;

    RandomNumGen rng_;                          // DistributedMinigame.randomNumGen (doId)
    RandomNumGen toonRng_[net::kMinigamePlayers]; // onstage's toonRNGs
    int slot_ = 0;
    maze::Suit suits_[maze::kMaxSuits];
    int numSuits_ = 0;

    // __showToonHitBySuit's flight (Trajectory up at 50, gravity 32) to a random walkable tile.
    bool flying_ = false;
    float flyT_ = 0.0f;
    float flyFromX_ = 0, flyFromY_ = 0, flyToX_ = 0, flyToY_ = 0;
    float spinH_ = 0;

    char lines_[3][48] = {};

    render::Model mazeModel_;
    render::Model treasureModel_;
};

} // namespace tt::game
