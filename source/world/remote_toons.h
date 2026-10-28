// Other players' toons: spawned/updated from tt::net::Client events, smoothed
// toward their latest network pose, animated and labelled.
#pragma once

#include <3ds.h>
#include <citro3d.h>

#include "net/client.h"
#include "render/model.h"
#include "render/renderer.h"
#include "ui/balloons.h"
#include "world/boat.h"
#include "world/toon_avatar.h"
#include "world/toon_cache.h"
#include "world/toon_dna.h"

namespace tt::world {

class RemoteToons {
public:
    static constexpr int kMax = 8; // nearest-first visible cap (AGENTS.md M3.5 suggests 8-16)

    void init(ToonCache* cache) { cache_ = cache; }
    // Toons whose setParent token is kParentDonaldsBoat have boat-local poses; drawn through this boat.
    void setBoat(const BoatActor* boat) { boat_ = boat; }
    void handleEvent(const net::Event& ev, const net::Client& client);
    void clear();
    // Puts a phrase in a remote toon's chat balloon (avatar.setChatAbsolute), e.g. the
    // knock-knock lines; ignored for toons that are not here.
    void say(u32 doId, const char* phrase);
    void update(float dt);
    // A game that places players from its own state (Race lanes, Pattern rows): the toon walks
    // (smoothed, with the speed-chosen clip) to this pose instead of its last broadcast sample.
    void placeExternal(u32 doId, float x, float y, float z, float h);
    // Draws the nearest kMax toons to (cx, cy).
    void draw(render::Renderer& renderer, float cx, float cy);
    // Adds nametags/balloons for toons in front of the camera.
    void addBalloons(ui::Balloons& balloons, const C3D_Mtx& proj, const C3D_Mtx& view) const;
    int count() const { return count_; }
    const ToonAvatar* avatar(u32 doId) const; // nullptr if not visible
    // Appends up to maxOut map markers (x, y) for active toons; returns how many were written.
    int mapPositions(float* xy, int maxOut) const;
    // Feet positions + body scale (x, y, z, scale) of active toons, for drop shadows.
    int shadowPositions(float* xyzs, int maxOut) const;

private:
    struct Remote {
        bool active = false;
        u32 doId = 0;
        float x = 0, y = 0, z = 0, h = 0;     // displayed, in the parent's frame (boat-local while parent is the boat)
        float wx = 0, wy = 0, wz = 0, wh = 0; // displayed pose in the world (== x.. unless parented to the boat)
        u32 parent = 0;                       // setParent token last applied to the pose
        float tx = 0, ty = 0, tz = 0, th = 0; // latest network sample
        // Dead reckoning: velocity from the last two samples (units/s, deg/s), the sample's local
        // time, and whether the sender is moving (setSmStop clears it).
        float vx = 0, vy = 0, vz = 0, vh = 0;
        u32 sampleMs = 0;
        bool moving = false;
        char name[32] = {};
        const char* phrase = nullptr;
        float phraseAge = 99.0f;
        ToonDna dna = defaultToonDna();
        bool hasDna = false;
        ToonLod lod = ToonLod::High;
        ToonLook look;
        ToonAvatar avatar;
        bool locomotion = true; // anim state "run"/"walk"/"Happy": clip follows speed
        ToonAnim stateAnim = kAnimNeutral;
        float stateRate = 1;
        u32 emoteSerial = 0;
        float catchFallAge = 2;
        float speed = 0;        // smoothed forward speed (units/s), for walk/run choice
        float turnRate = 0;
        float distSq = 0;
        // Tunnel transition playback (source/world/transition.*)
        bool gamePlaced = false; // placeExternal owns the pose; broadcast samples are ignored
        bool inTunnelTrack = false;
        bool isTunnelOut = false;
        float tunnelAge = 0.0f;
        float tunnelDuration = 0.0f;
        float tunnelStraightDur = 0.0f;
        float tunnelStartX = 0.0f, tunnelStartY = 0.0f;
        float tunnelEndX = 0.0f;
        float tunnelOriginX = 0.0f, tunnelOriginY = 0.0f, tunnelOriginZ = 0.0f, tunnelOriginH = 0.0f;
    };
    Remote* find(u32 doId);
    void applyDna(Remote& r, const net::RemoteToon& t);
    void refreshLook(Remote& r);
    void despawn(Remote& r);
    void updateWorldPose(Remote& r) const;

    ToonCache* cache_ = nullptr;
    const BoatActor* boat_ = nullptr;
    Remote toons_[net::kMaxRemoteToons];
    int count_ = 0;
};

// Maps a network anim *state* (ToontownGlobals.ToonAnimStates) to a clip. Sets
// *locomotion when the state means "pick walk/run/neutral from movement" (Toon.enterHappy).
ToonAnim animFromState(const char* state, bool* locomotion);

} // namespace tt::world
