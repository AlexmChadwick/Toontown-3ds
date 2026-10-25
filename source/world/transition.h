// Place transitions: tunnels, building doors, teleports, and death.
// Implements kinematics and tracks as upstream plays them:
//   - Tunnels: Place.enterTunnelOut / enterTunnelIn, DistributedToon getTunnelOutToonTrack / getTunnelInToonTrack
//   - Doors: DistributedDoor avatarEnterDoorTrack / avatarExitTrack, enterOpening, enterClosing
//   - Teleports: Toon getTeleportOutTrack / getTeleportInTrack (portal hole prop, dive / rise)
//   - Death: Toon getDiedInterval, enterDied
#pragma once

#include <3ds.h>
#include <citro2d.h>
#include <citro3d.h>

#include "audio/audio.h"
#include "net/client.h"
#include "render/model.h"
#include "render/renderer.h"
#include "world/place.h"
#include "world/player.h"
#include "world/toon_avatar.h"

namespace tt::world {

enum class TransitionType : u8 {
    None,
    Tunnel,
    Door,
    Teleport,
    Died
};

enum class TransitionPhase : u8 {
    Idle,
    Departing,      // Departure movie / walk / animation
    BlackFrame,     // Exactly one fully black frame shown; ready for place.load()
    Arriving        // Arrival movie / walk / animation
};

class Transition {
public:
    void init();
    void shutdown();
    void reset();

    bool idle() const { return phase_ == TransitionPhase::Idle; }
    bool active() const { return phase_ != TransitionPhase::Idle; }
    bool readyToLoad() const { return phase_ == TransitionPhase::BlackFrame; }
    float fade() const { return fadeAlpha_; }
    u32 targetZone() const { return targetZone_; }
    u32 fromZone() const { return fromZone_; }

    // Start departure transitions
    void startTunnelOut(u32 targetZone, const Exit& exit, const Player& player, const Place& place, net::Client* net);
    void startDoorOut(u32 targetZone, const Exit& exit, const Player& player, const Place& place, u32 doorDoId, net::Client* net);
    void startTeleportOut(u32 targetZone, const Player& player, net::Client* net);
    void startDied(u32 safezoneId, const Player& player, net::Client* net);
    void startTeleportIn(const Player& player, net::Client* net);

    // Call after place.load() and player.reset() have completed behind the black frame
    void notifyLoaded(u32 loadedZone, Place& place, Player& player, net::Client* net);

    // Per-frame tick
    void update(float dt, Player& player, Place& place, net::Client* net);
    void applyInput(circlePosition& pad, u32& held, u32& down);
    bool applyAnimation(ToonAvatar& toon, float dt);
    bool isToonVisible() const;
    float toonScale() const { return toonScale_; }
    const char* toonNetworkState() const;

    // Rendering
    void drawWorld(render::Renderer& renderer);
    void drawFade(render::Renderer& renderer, bool stereo);

private:
    void computeTunnelOrigin(const Exit& exit, const Place& place, const Player& player);
    void computeDoorOrigin(const Exit& exit, const Place& place, const Player& player);

    TransitionType type_ = TransitionType::None;
    TransitionPhase phase_ = TransitionPhase::Idle;
    float age_ = 0.0f;
    float fadeAlpha_ = 0.0f; // 0.0 (clear) .. 1.0 (black)
    int fadeDir_ = 0;        // +1 fading out (closing), -1 fading in (opening)

    u32 fromZone_ = 0;
    int fromDoor_ = -1; // HQ interior door index we left by (door_trigger_<block>_<n>)
    u32 targetZone_ = 0;
    u32 doorDoId_ = 0;

    // Toon rendering modifiers
    float toonScale_ = 1.0f;
    bool toonVisible_ = true;

    // Assets
    render::Model portalModel_;
    render::Model doorModel_;
    // [interior life: door leaves]
    render::Model doorModelLeft_;
    // [end interior life: door leaves]
    tt::audio::SfxId sfxTeleport_ = tt::audio::kInvalidSfx;
    tt::audio::SfxId sfxDoorOpen_ = tt::audio::kInvalidSfx;
    tt::audio::SfxId sfxDoorClose_ = tt::audio::kInvalidSfx;
    tt::audio::SfxId sfxLose_ = tt::audio::kInvalidSfx;

    // Tunnel kinematics (upstream DistributedToon.py)
    float tunnelOriginX_ = 0.0f, tunnelOriginY_ = 0.0f, tunnelOriginZ_ = 0.0f, tunnelOriginH_ = 0.0f;
    float tunnelStartX_ = 0.0f, tunnelStartY_ = 0.0f;
    float tunnelEndX_ = 0.0f;
    float tunnelStraightDur_ = 0.0f;
    float tunnelDuration_ = 0.0f;
    float tunnelStartAvH_ = 0.0f;

    // Door kinematics (upstream DistributedDoor.py)
    float doorOriginX_ = 0.0f, doorOriginY_ = 0.0f, doorOriginZ_ = 0.0f, doorOriginH_ = 0.0f;
    float doorStartX_ = 0.0f, doorStartY_ = 0.0f;
    float doorSwing_ = 0.0f; // degrees (0 .. 100)
    bool doorActive_ = false;

    // Portal hole kinematics (upstream Toon.py)
    float portalX_ = 0.0f, portalY_ = 0.0f, portalZ_ = 0.0f;
    float portalScale_ = 0.0f;
    bool portalActive_ = false;

    // Camera override during transition
    bool overrideCam_ = false;
    C3D_FVec camEye_{};
    C3D_FVec camTarget_{};
};

} // namespace tt::world
