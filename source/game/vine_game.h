#pragma once
#include "world/asset_batch.h"

#include <3ds.h>
#include <citro3d.h>

#include "game/panda_rng.h"
#include "game/vine_logic.h"
#include "net/client.h"
#include "render/model.h"
#include "render/renderer.h"

namespace tt::game {

class VineGame {
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

    void toonPose(float* x, float* y, float* z, float* h) const;
    // DistributedVineGame.calcToonOffset: the left hold joint in 'swing' (SwingVine.calcOffset frame 86),
    // Toon-local and already body-scaled. The Toon hangs from the vine by it.
    void setHandOffset(float x, float y, float z) { hand_[0] = x; hand_[1] = y; hand_[2] = z; }
    // The 'swing' frame (24 fps source) the local Toon shows: synced to the held vine, JumpingFrame in the air.
    float swingFrame() const;
    // Another participant's toon from its reported vine state (see vine::reportedPosition); false if none.
    bool remotePose(const net::Minigame::VineRemote& v, uint32_t nowMs, float* x, float* z, float* h) const;
    bool toonRunning() const { return false; }
    void camera(C3D_FVec* eye, C3D_FVec* target) const;

    const char* line(int i) const { return (i >= 0 && i < 3) ? lines_[i] : ""; }
    bool isDone() const { return done_; }

private:
    world::BasicAssetBatch<16> assetBatch_;
    void loadBats(uint32_t hood, game::RandomNumGen& rng);
    uint8_t hazardHit(float t) const; // vine::kFallingSpider / kFallingBat, 0 for none
    bool batPose(int i, float t, float* x, float* z) const;

    // VineGameGlobals.BatInfo per hood: each bat's lap time, first delay, first-lap start multiplier and
    // its randomNumGen heights (createBatIval: randrange(BatMinHeight, BatMaxHeight) per lap).
    struct Bat {
        float traverse = 0.0f, delay = 0.0f, startMultiplier = 1.0f;
        int laps = 0;
        float height[8] = {};
    };
    static constexpr int kMaxBats = 3;
    Bat bats_[kMaxBats];
    int numBats_ = 0;
    float bananaZ_[vine::kNumVines - 1] = {};
    float hand_[3] = {-0.78f, 0.22f, 3.92f};
    render::Model spiderModel_[2]; // vine_game '**/spider_2', '**/spider_3' (SequenceNode, 2 fps)
    render::Model batModel_[3];    // '**/bat__1', '**/bat2', '**/bat3' (SequenceNode pingpong, 12 fps)
    render::Model segmentModel_; // SwingVine cable piece (bake_minigame.bake_vine_parts)
    render::Model staticModel_;  // SwingVine.setupStaticPart
    bool done_ = false;
    bool autoPlay_ = false;
    float gameTime_ = 0.0f; // seconds since the game started: the swings run on it
    float timeRemaining_ = vine::kGameDuration;
    // What this toon has announced (DistributedVineGame.b_setNewVine / b_setNewVineT): the start shelf,
    // the climb direction last sent, and when the climb position was last sent.
    bool announced_ = false;
    int sentClimbDir_ = 0;
    uint32_t lastClimbSendMs_ = 0;

    vine::VineSpec course_[vine::kNumVines] = {};
    vine::VineToonState toon_;
    bool bananaTaken_[vine::kNumVines - 1] = {};
    int bananas_ = 0;
    int score_ = 1;

    char lines_[3][48] = {};

    render::Model background_; // vine_game '**/background': the backdrop at y 15.5
    render::Model bananaModel_; // vine_game '**/bananas'
    render::Model startShelf_; // vine_game_shelf '**/start1'
    render::Model endShelf_;   // vine_game_shelf '**/end1'
};

} // namespace tt::game
