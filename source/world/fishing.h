// Fishing in the playgrounds (docs/design/fishing.md): walk into a fishing spot to sit down on
// it, hold A to charge and let go to cast, the bob lands on the pond, a bite gives a fish /
// boot / jellybeans, B leaves.
//
// Two modes share one state machine:
//   server  the AI generated DistributedFishingSpot objects (net::Client::findFishSpot); enter,
//           cast and exit are requests, and the PullIn setMovie decides the catch.
//   local   ("practice") stock Open Toontown generates ponds but no spots and has no spot logic
//           (ToontownAIRepository.findFishingSpots is a TODO, DistributedFishingSpotAI is an
//           empty class), so spots come from the zone's .pts anchors (fishing_spot_DNARoot) and
//           the catch is rolled here with FishGlobals' rules (world/fish_logic.*). Nothing is
//           charged or credited: the server owns jellybeans and the fish tank.
// Body clips use the bounded Toon cache; rod/line transforms remain separate.
#pragma once

#include <3ds.h>
#include <citro3d.h>

#include "audio/audio.h"
#include "net/client.h"
#include "render/model.h"
#include "render/renderer.h"
#include "ui/fishing_panel.h"
#include "world/fish_logic.h"
#include "world/player.h"

namespace tt::world {

class ToonAvatar;

class Fishing {
public:
    static constexpr int kMaxSpots = 8;

    // player: moved onto the spot and told where the camera goes; net: may be offline.
    void init(Player* player, net::Client* net);
    void shutdown();
    void cancel(); // context lost: stop immediately, never leave a stale practice panel
    // New zone: reload the local spot anchors from its .pts file and drop any session.
    void setZone(u32 zoneId, const char* pointsPath);
    void handleEvent(const net::Event& ev);

    // True from sitting down until the toon has left the spot again.
    bool active() const { return state_ != State::Idle && state_ != State::Requesting; }
    // Before Player::update each frame: detects spot triggers, and while fishing consumes the
    // circle pad, A and B from `in` (so the toon does not walk or jump). canEnter is false while
    // a zone transition runs.
    void handleInput(Input& in, float dt, u32 nowMs, bool canEnter);
    // This frame's touch gesture from the bottom-screen panel (call before handleInput).
    void setTouch(const ui::FishingTouch& t) {
        const bool started = touch_.started, released = touch_.released, exit = touch_.exit;
        touch_ = t;
        touch_.started |= started, touch_.released |= released, touch_.exit |= exit;
    }
    // After Player::update: overrides the follow camera while fishing.
    void applyCamera();
    void draw(render::Renderer& renderer);
    void applyAnimation(ToonAvatar& avatar) const;
    // View for the bottom-screen panel; null when not fishing.
    const ui::FishingView* view() const { return active() ? &view_ : nullptr; }

    // Test helper: the spot the player would enter (nearest anchor / server spot), if any.
    bool nearestSpot(float x, float y, float* sx, float* sy, float* sz, float* sh) const;
    int numSpots() const;

private:
    enum class State : u8 { Idle, Requesting, Entering, Ready, Charging, Casting, Waiting, Reeling, Result, Leaving };
    struct Spot {
        u32 doId = 0; // 0 = local anchor
        float x = 0, y = 0, z = 0, h = 0;
        bool armed = true; // re-arms after the toon walked away (enter once per approach)
    };

    void collectSpots();
    bool serverMode() const;
    void enter(const Spot& s, u32 nowMs);
    void beginLeave();
    void setState(State s);
    void setStatus(const char* text);
    void setResultNone();
    void startCharge();
    void releaseCast(u32 nowMs);
    void finishCatch(const fish::Catch& c, bool newEntry);
    void onPullIn(const net::FishMovieMsg& m);
    void loadAssets(int rod);
    void unloadAssets();
    void updateBob(float dt);
    void updateRodPitch(float dt);
    void landBob(u32 nowMs);
    void rodMatrix(C3D_Mtx* out) const;
    void rodTip(float* tip) const;
    void syncView();
    int currentRod() const;
    int currentArea() const;

    Player* player_ = nullptr;
    net::Client* net_ = nullptr;
    u32 zoneId_ = 0;

    // Local anchors of the current zone, plus the spots the server generated.
    Spot anchors_[kMaxSpots];
    int numAnchors_ = 0;
    Spot spots_[kMaxSpots];
    int numSpots_ = 0;

    enum class ToonPhase : u8 { Equip, Ready, Cast, Reel, Exit };
    ToonPhase toonPhase_ = ToonPhase::Ready;
    float toonTime_ = 0;
    State state_ = State::Idle;
    float stateTime_ = 0.0f;
    Spot cur_{};
    bool curLocal_ = true;
    u32 requestMs_ = 0;
    u32 sessionSpot_ = 0; // doId of the spot we are fishing at (server mode)

    float aim_ = 0.0f;     // degrees, + = left, relative to the spot heading
    ui::FishingTouch touch_{};
    bool touchCharge_ = false; // this charge follows a drag from the touch Cast button
    u32 dpadAimHeld_ = 0;  // padnav: D-Left / D-Right held last frame (logs the aim on release)
    float power_ = 0.0f;   // 0..1
    float castPower_ = 0.0f, castAim_ = 0.0f;
    float castTime_ = 0.0f; // seconds since release
    fish::Landing landing_{};
    float bobX_ = 0, bobY_ = 0, bobZ_ = 0;
    bool bobVisible_ = false;
    float biteIn_ = 0.0f;   // seconds until the bite (local mode); < 0 = never
    float waitTime_ = 0.0f;
    float reelTime_ = 0.0f;
    fish::Catch pending_{};
    bool pendingNewEntry_ = false;
    bool gotResult_ = false;
    float rodPitch_ = 35.0f;
    float bobBob_ = 0.0f;

    // Camera.
    float camT_ = 0.0f;
    C3D_FVec camStartEye_{}, camStartTarget_{};
    bool camStarted_ = false;

    fish::Rng rng_;
    int localTank_ = 0;
    int casts_ = 0;

    render::Model rod_, bob_, line_;
    int loadedRod_ = -1;
    bool linesLoaded_ = false;
    audio::SfxId splash_[2] = {audio::kInvalidSfx, audio::kInvalidSfx};
    bool sfxTried_ = false;

    ui::FishingView view_;
};

} // namespace tt::world
