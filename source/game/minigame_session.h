// A trolley minigame from hand-off to the way back (docs/design/trolley.md section 2):
// changeZone into the dynamic zone, DistributedMinigame framework (setAvatarJoined, rules,
// setAvatarReady, setGameStart, setGameExit -> setAvatarExited), the game itself, then the
// PurchaseManager (requestExit, setInventory done=1 on setPurchaseExit) and home.
//
// Games: Catching (DistributedCatchGame) is playable; any other game is aborted at once with
// requestExit, which the AI pays 1..7 jellybeans for (trolley.md 2.6, "stub").
#pragma once

#include <3ds.h>
#include <citro3d.h>

#include "game/catch_logic.h"
#include "net/client.h"
#include "render/model.h"
#include "render/renderer.h"

namespace tt::game {

class MinigameSession {
public:
    static constexpr int kMaxLive = 24; // falling objects on screen at once

    void shutdown();
    // setMinigameZone: go there (no vis list) and remember the playground to return to.
    void begin(net::Client& client, u32 zone, u16 gameId, u32 homeHood, u32 nowMs);
    bool active() const { return phase_ != Phase::Off; }
    // The catch stage is shown instead of the playground (from the rules until cleanup).
    bool inScene() const {
        return isCatch_ && (phase_ == Phase::Rules || phase_ == Phase::WaitStart || phase_ == Phase::Play ||
                            phase_ == Phase::Done);
    }
    void handleEvent(const net::Event& ev, net::Client& client, u32 nowMs);
    // pad: circle pad, moves the toon on the stage (OrthoWalk: screen up = +y).
    void update(net::Client& client, const circlePosition& pad, float dt, u32 nowMs);
    void draw(render::Renderer& renderer);
    // Test hook (autotest): steer the toon under the lowest falling fruit.
    void setAutoPlay(bool on) { autoPlay_ = on; }

    void toonPose(float* x, float* y, float* z, float* h) const;
    bool toonRunning() const { return speed_ > 0.5f; }
    void camera(C3D_FVec* eye, C3D_FVec* target) const;
    // Home playground once the purchase step is over (once; 0 = keep going).
    u32 popReturn();
    // Bottom-screen lines (title and up to 3 lines of text).
    const char* title() const { return title_; }
    const char* line(int i) const { return i >= 0 && i < 3 ? lines_[i] : ""; }

private:
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

    Phase phase_ = Phase::Off;
    u32 zone_ = 0, home_ = 0;
    u16 gameId_ = 0;
    bool isCatch_ = false;
    u32 phaseMs_ = 0;
    u32 returnHood_ = 0;
    bool joinedSent_ = false, readySent_ = false, doneSent_ = false, exitedSent_ = false, purchaseExitSent_ = false;

    catchgame::Schedule sched_;
    int nextDrop_ = 0;
    Live live_[kMaxLive];
    int fruitsCaught_ = 0, anvilsHit_ = 0;
    float stunned_ = 0.0f;
    bool autoPlay_ = false;
    float tx_ = 0, ty_ = 0, th_ = 0, speed_ = 0;

    render::Model ground_;
    render::Model objects_[catchgame::kNumDropTypes];
    char title_[40] = {};
    char lines_[3][64] = {};
};

} // namespace tt::game
