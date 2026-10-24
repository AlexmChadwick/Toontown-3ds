// Other players' toons: spawned/updated from tt::net::Client events, smoothed
// toward their latest network pose, animated and labelled.
#pragma once

#include <3ds.h>
#include <citro3d.h>

#include "net/client.h"
#include "render/model.h"
#include "render/renderer.h"
#include "ui/balloons.h"
#include "world/toon_avatar.h"
#include "world/toon_cache.h"
#include "world/toon_dna.h"

namespace tt::world {

class RemoteToons {
public:
    static constexpr int kMax = 16; // nearest-first visible cap (AGENTS.md M3.5 suggests 8-16)

    void init(ToonCache* cache) { cache_ = cache; }
    void handleEvent(const net::Event& ev, const net::Client& client);
    void clear();
    void update(float dt);
    // Draws the nearest kMax toons to (cx, cy).
    void draw(render::Renderer& renderer, float cx, float cy);
    // Adds nametags/balloons for toons in front of the camera.
    void addBalloons(ui::Balloons& balloons, const C3D_Mtx& proj, const C3D_Mtx& view) const;
    int count() const { return count_; }
    // Appends up to maxOut map markers (x, y) for active toons; returns how many were written.
    int mapPositions(float* xy, int maxOut) const;

private:
    struct Remote {
        bool active = false;
        u32 doId = 0;
        float x = 0, y = 0, z = 0, h = 0;     // displayed
        float tx = 0, ty = 0, tz = 0, th = 0; // latest network sample
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
        float speed = 0;        // smoothed forward speed (units/s), for walk/run choice
        float turnRate = 0;
        float distSq = 0;
    };
    Remote* find(u32 doId);
    void applyDna(Remote& r, const net::RemoteToon& t);
    void refreshLook(Remote& r);
    void despawn(Remote& r);

    ToonCache* cache_ = nullptr;
    Remote toons_[net::kMaxRemoteToons];
    int count_ = 0;
};

// Maps a network anim *state* (ToontownGlobals.ToonAnimStates) to a clip. Sets
// *locomotion when the state means "pick walk/run/neutral from movement" (Toon.enterHappy).
ToonAnim animFromState(const char* state, bool* locomotion);

} // namespace tt::world
