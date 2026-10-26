// A trolley minigame from hand-off to the way back (docs/design/trolley.md section 2):
// changeZone into the dynamic zone, DistributedMinigame framework (setAvatarJoined, rules,
// setAvatarReady, setGameStart, setGameExit -> setAvatarExited), the game itself, then the
// PurchaseManager (requestExit, setInventory done=1 on setPurchaseExit) and home.
//
// Supported games:
// - Catching (DistributedCatchGame, id 8)
// - Cannon (DistributedCannonGame, id 2)
// - Tug of War (DistributedTugOfWarGame, id 7)
// - Ring (DistributedRingGame, id 5)
// Any other game is aborted at once with requestExit, which the AI pays 1..7 jellybeans for (trolley.md 2.6).
#pragma once

#include "game/minigame_cog.h"
#include "world/asset_batch.h"

#include <3ds.h>
#include <citro3d.h>

#include "game/cannon_game.h"
#include "game/catch_logic.h"
#include "game/cog_thief_game.h"
#include "game/diving_game.h"
#include "game/ice_game.h"
#include "game/maze_game.h"
#include "game/pairing_game.h"
#include "game/pattern_game.h"
#include "game/photo_game.h"
#include "game/race_game.h"
#include "game/ring_game.h"
#include "game/tag_game.h"
#include "game/target_game.h"
#include "game/tug_game.h"
#include "game/two_d_game.h"
#include "game/vine_game.h"
#include "net/client.h"
#include "render/model.h"
#include "render/renderer.h"
#include "ui/minigame_purchase_panel.h"

namespace tt::world { class ToonAvatar; }

namespace tt::game {

class MinigameSession {
public:
    void pumpAssets() {
        assetBatch_.pump();
        cannon_.pumpAssets();
        tug_.pumpAssets();
        ring_.pumpAssets();
        maze_.pumpAssets();
        pairing_.pumpAssets();
        diving_.pumpAssets();
        vine_.pumpAssets();
        target_.pumpAssets();
        cogThief_.pumpAssets();
        twoD_.pumpAssets();
        tag_.pumpAssets();
        pattern_.pumpAssets();
        ice_.pumpAssets();
        race_.pumpAssets();
        photo_.pumpAssets();
    }
    bool loadingAssets() const {
        return assetBatch_.loading() || cannon_.loadingAssets() || tug_.loadingAssets() ||
               ring_.loadingAssets() || maze_.loadingAssets() || pairing_.loadingAssets() ||
               diving_.loadingAssets() || vine_.loadingAssets() || target_.loadingAssets() ||
               cogThief_.loadingAssets() || twoD_.loadingAssets() || tag_.loadingAssets() ||
               pattern_.loadingAssets() || ice_.loadingAssets() || race_.loadingAssets() ||
               photo_.loadingAssets();
    }
    static constexpr int kMaxLive = 24; // falling objects on screen at once

    void shutdown();
    // setMinigameZone: go there (no vis list) and remember the playground to return to.
    void begin(net::Client& client, u32 zone, u16 gameId, u32 homeHood, u32 nowMs);
    bool photoScene() const { return isPhoto_; }
    // The Cogs to draw this frame; 0 when the game has none.
    int cogs(MinigameCog* out, int max) const;
    bool startedGame() const { return startedGame_; }
    // From setGameStart until the game ends: our toon stands in the game's scene.
    bool inGame() const { return startedGame_ && !endedGame_ && phase_ == Phase::Play; }
    // Games whose client calls startSmooth() on the other participants
    // (Distributed{Catch,CogThief,Diving,Maze,Pairing,Ring,Tag,Target,TwoD}Game): their
    // broadcast setSmPosHpr places them; the other games place everyone from game state.
    // The other participants this game places itself (Race lanes, Pattern back row); 0 when none.
    int placedPlayers(const net::Client& client, uint32_t nowMs, MinigamePlayer* out, int max) const;
    bool smoothedPlayers() const {
        return isCatch_ || isCogThief_ || isDiving_ || isMaze_ || isPairing_ || isRing_ || isTag_ ||
               isTarget_ || isTwoD_;
    }
    bool endedGame() const { return endedGame_; }
    bool completedPurchase() const { return completedPurchase_; }
    bool active() const { return phase_ != Phase::Off; }
    // A minigame stage is shown instead of the playground (from the rules until cleanup).
    bool inScene() const {
        // Purchase and Leaving keep the game's last scene (the purchase panel is on the bottom screen).
        return (isCatch_ || isCannon_ || isTug_ || isRing_ || isMaze_ || isPairing_ || isDiving_ || isVine_ ||
                isTarget_ || isCogThief_ || isTwoD_ || isTag_ || isPattern_ || isIce_ || isRace_ || isPhoto_) &&
               (phase_ == Phase::Rules || phase_ == Phase::WaitStart || phase_ == Phase::Play ||
                phase_ == Phase::Done || phase_ == Phase::Purchase || phase_ == Phase::Leaving);
    }
    void handleEvent(const net::Event& ev, net::Client& client, u32 nowMs);
    // pad: circle pad, moves the toon on the stage (OrthoWalk: screen up = +y).
    void update(net::Client& client, const circlePosition& pad, float dt, u32 nowMs);
    void draw(render::Renderer& renderer);
    // Test hook (autotest): steer toon / auto-play minigame.
    void setAutoPlay(bool on) { autoPlay_ = on; }

    void toonPose(float* x, float* y, float* z, float* h) const;
    // Renderer state the game's world pass needs (far plane, background, fog). applyWorld runs
    // before beginWorld while a game is active; releaseWorld restores the defaults after it.
    void applyWorld(render::Renderer& renderer);
    void releaseWorld(render::Renderer& renderer);
    void applyAnimation(world::ToonAvatar& avatar) const;
    bool toonRunning() const;
    const char* toonNetworkState() const;
    void camera(C3D_FVec* eye, C3D_FVec* target) const;
    // Home playground once the purchase step is over (once; 0 = keep going).
    u32 popReturn();
    // Bottom-screen lines (title and up to 3 lines of text).
    const char* title() const { return title_; }
    const char* line(int i) const;

private:
    bool startedGame_ = false, endedGame_ = false, completedPurchase_ = false;
    world::BasicAssetBatch<32> assetBatch_;
    enum class Phase : u8 { Off, Joining, Rules, WaitStart, Play, Done, Purchase, Leaving };
    struct Live {
        int num = -1;     // drop index (objNum)
        double spawnT = 0; // game time it was spawned
        float x = 0, y = 0, z = 0, h = 0;
        bool caught = false;
    };
    void setPhase(Phase p);
    void loadCatch(int numPlayers);
    void unloadCatch();
    void unloadCurrent();

    Phase phase_ = Phase::Off;
    u32 zone_ = 0, home_ = 0;
    u16 gameId_ = 0;
    bool isCatch_ = false;
    bool isCannon_ = false;
    bool isTug_ = false;
    bool isRing_ = false;
    bool worldFog_ = false; // applyWorld set the fog, so releaseWorld must clear it
    bool isMaze_ = false;
    bool isPairing_ = false;
    bool isDiving_ = false;
    bool isVine_ = false;
    bool isTarget_ = false;
    bool isCogThief_ = false;
    bool isTwoD_ = false;
    bool isTag_ = false;
    bool isPattern_ = false;
    bool isIce_ = false;
    bool isRace_ = false;
    bool isPhoto_ = false;

    u32 phaseMs_ = 0;
    u32 returnHood_ = 0;
    bool joinedSent_ = false, readySent_ = false, doneSent_ = false, exitedSent_ = false, purchaseExitSent_ = false;

    // Catch
    catchgame::Schedule sched_;
    int nextDrop_ = 0;
    Live live_[kMaxLive];
    int fruitsCaught_ = 0, anvilsHit_ = 0;
    float stunned_ = 0.0f, eating_ = 0.0f;
    float tx_ = 0, ty_ = 0, th_ = 0, speed_ = 0;
    render::Model ground_;
    render::Model objects_[catchgame::kNumDropTypes];

    // Other minigames
    CannonGame cannon_;
    TugGame tug_;
    RingGame ring_;
    MazeGame maze_;
    PairingGame pairing_;
    DivingGame diving_;
    VineGame vine_;
    TargetGame target_;
    CogThiefGame cogThief_;
    TwoDGame twoD_;
    TagGame tag_;
    PatternGame pattern_;
    IceGame ice_;
    RaceGame race_;
    PhotoGame photo_;

    ui::MinigamePurchasePanel purchasePanel_;

    bool autoPlay_ = false;
    char title_[40] = {};
    char lines_[3][64] = {};
};

} // namespace tt::game
