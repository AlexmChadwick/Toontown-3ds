#pragma once
#include "world/asset_batch.h"

#include <3ds.h>
#include <citro2d.h>
#include <citro3d.h>

#include "game/race_logic.h"
#include "game/race_tables.h"
#include "net/client.h"
#include "render/model.h"
#include "render/renderer.h"

namespace tt::game {

class RaceGame {
public:
    void pumpAssets() {
        assetBatch_.pump();
    }
    bool loadingAssets() const {
        return assetBatch_.loading();
    }
    RaceGame() = default;

    bool init(const char* root);
    void shutdown();

    void update(net::Client& client, const circlePosition& pad, u32 keysDown, float dt, u32 nowMs);
    void draw(render::Renderer& renderer);
    // showChanceCardText: the reward in red sign-font OnscreenText at aspect2d (0, 0.2), scale 0.14,
    // wordwrap 14, over the card. `nameOf` gives a lane's Toon name (getAvatar(picker).getName()).
    void draw2D(C3D_RenderTarget* target, const char* (*nameOf)(int lane, void* ctx), void* ctx);

    void camera(C3D_FVec* eye, C3D_FVec* target) const;
    void toonPose(float* x, float* y, float* z, float* h) const;
    void setLane(int lane) { lane_ = lane; }
    // Test hook (autotest): the next submitted move steps onto our chance square when it can.
    void setAutoAim(bool on) { autoAim_ = on; }
    // Where a lane's toon stands on the board (its current place), in scene units.
    void lanePose(int lane, float* x, float* y, float* z, float* h) const;
    // walkDone: the win movie starts once every turn's walk and chance card has played.
    bool isDone() const { return sim_.isDone && step_ == Step::Idle; }
    // The chance card in front of the camera (render, so it hides the 3D nametags behind it).
    bool cardUp() const;
    int score() const { return sim_.scores[0]; }
    bool toonRunning() const { return step_ == Step::Walk && walkTo_[lane_] != walkFrom_[lane_] && stepTime_ >= walkDelay_; }

    const char* title() const { return "Race Game"; }
    const char* line(int i) const;

    void onSetChancePositions(const uint8_t* positions, int count);
    // setServerChoices: `count` entries, numPlayers per server turn (a chance reward adds a turn).
    void onSetServerChoices(const int8_t* choices, const uint8_t* positions, const int8_t* rewards, int count,
                            int numPlayers);
    void shutdownText();

private:
    world::BasicAssetBatch<8> assetBatch_;
    racegame::Simulation sim_;
    int selectedChoice_ = 1;
    bool choiceSubmitted_ = false;
    float timer_ = 0.0f;
    // enterMoveAvatars' task list, played back: each turn walks (turn 0 after showNumbers /
    // showMatches' 1.25 s), then each reward drawn on it shows the chance card (showChanceRewards).
    enum class Step { Idle, Walk, Card };
    static constexpr int kMaxTurns = 4;
    int numTurns_ = 0;
    int turn_ = 0;
    int8_t turnChoice_[kMaxTurns][4] = {};
    uint8_t turnPos_[kMaxTurns][4] = {};
    int8_t turnReward_[kMaxTurns][4] = {};
    Step step_ = Step::Idle;
    float stepTime_ = 0.0f;
    float stepLength_ = 0.0f;
    float walkDelay_ = 0.0f;     // turn 0: the walk starts after the dice numbers / matches
    float walkDuration_ = 0.0f;  // getLongestLerpTime
    int walkFrom_[4] = {};
    int walkTo_[4] = {};
    int shownPos_[4] = {};
    int cardPlayer_ = -1;        // the lane whose reward card is up
    float lanePlace(int lane) const;
    void startTurn(int turn);
    void startCard(int fromPlayer);
    void nextStep();
    void cardMatrix(C3D_Mtx* out) const;

    render::Model cardModel_;
    C2D_TextBuf cardBuf_ = nullptr;
    C2D_Text cardText_ = {};
    int cardTextFor_ = -1;       // turn * 4 + player of the parsed text
    render::Model trackModel_;
    // setChancePositions: a question_mark per player on the board (all on posHprArray row 0, as
    // upstream never advances its row), hidden by hideChanceMarker once that player's reward shows.
    render::Model markerModel_;
    int markerPos_[4] = {-1, -1, -1, -1};
    float markerHideIn_[4] = {-1, -1, -1, -1};

    mutable char lines_[3][64] = {};
    int lane_ = 0; // our avIdList index: the lane we run in
    bool autoAim_ = false;
    int numRacers_ = 1;
    // DistributedRaceGame.moveCamera: eased (posQuatInterval 0.75 s) from the last view.
    void moveCamera();
    float camFrom_[6] = {}; // eye xyz, look-at xyz
    float camTo_[6] = {};
    float camTime_ = 0.0f;
};

} // namespace tt::game
