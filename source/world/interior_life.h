// Interior and street life (docs/design/interior_life.md):
// 1. Toon HQ leaderboard (DistributedHQInterior): rows, names, scores and trophy stars on the
//    HQ's empty_board, from the server's setLeaderBoard.
// 2. The pet shop fish (DistributedPetshopInterior.setup), swimming while that object is here.
// 3. Knock-knock doors (DistributedKnockKnockDoor): the local toon entering a door's sphere asks
//    the AI to interact; every client plays the AI's 'playing' state as the joke sequence.
#pragma once

#include <3ds.h>
#include <citro2d.h>
#include <cstdint>

#include "audio/audio.h"
#include "game/knockknock_jokes.h"
#include "net/client.h"
#include "render/anim.h"
#include "render/model.h"
#include "render/renderer.h"
#include "ui/balloons.h"

namespace tt::world {

class InteriorLife {
public:
    InteriorLife() = default;
    ~InteriorLife();

    // A line a toon says during a knock-knock joke (avatar.setChatAbsolute upstream). The caller
    // routes it to the local toon's or a remote toon's own chat balloon.
    struct ToonLine {
        uint32_t avatarId = 0;
        const char* phrase = nullptr;
    };

    bool init();
    void shutdown();

    // On every zone load. Street zones read their knock-knock spheres (data/zones/<zone>.kkd).
    void setZone(uint32_t zoneId);

    // Simulation tick. Returns true and fills *line when a toon starts a knock-knock line.
    bool update(float dt, float playerX, float playerY, float playerZ, net::Client& client, uint32_t nowMs,
                ToonLine* line);

    // 3D world draw: the pet shop fish and the leaderboard trophy stars.
    void draw(render::Renderer& renderer);

    // Top-screen 2D pass: the leaderboard text on empty_board.
    void draw2D(const C3D_Mtx& proj, const C3D_Mtx& view);

    // The door's own balloon (TTLocalizer.DoorNametag) during a knock-knock joke.
    void addBalloons(ui::Balloons& balloons, const C3D_Mtx& proj, const C3D_Mtx& view, float nametagH) const;

    bool knockKnockActive() const { return joke_.active; }
    uint32_t zone() const { return zoneId_; }
    // Test-only: the centre of knock-knock sphere i in this zone.
    bool knockSphere(int i, float* x, float* y, float* z) const {
        if (i < 0 || i >= numSpheres_) return false;
        *x = spheres_[i].x;
        *y = spheres_[i].y;
        *z = spheres_[i].z;
        return true;
    }

private:
    void updateLeaderboardText(const net::Client::HQLeaderboardState& hq);
    void loadKnockSpheres(uint32_t zoneId);
    void setHqResident(bool resident);
    void setPetshopResident(bool resident);

    uint32_t zoneId_ = 0;

    // --- Toon HQ leaderboard ---
    bool hqActive_ = false;
    uint32_t hqRevision_ = 0;
    uint8_t hqTutorial_ = 0;
    render::Model starModel_;
    float starAngles_[10] = {};
    C2D_TextBuf textBuf_ = nullptr;
    C2D_Text titleText_ = {};
    struct RowText {
        C2D_Text nameText = {};
        C2D_Text scoreText = {};
        bool used = false;
        bool hasStar = false;
        float starScale = 0.6f;
        float spinRate = 0.0f; // deg/s
        float starColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    } rows_[10] = {};
    bool textValid_ = false;

    // --- Pet shop fish ---
    bool petshopActive_ = false;
    render::Model fishModel_;
    render::Anim fishAnim_;
    float fishTime_ = 0.0f;

    // --- Knock-knock doors ---
    struct KnockSphere {
        uint16_t propId = 0;
        float x = 0.0f, y = 0.0f, z = 0.0f, radius = 0.0f;
    };
    static constexpr int kMaxKnockSpheres = 64; // largest street: 42 (bake_knock_doors.py)
    KnockSphere spheres_[kMaxKnockSpheres] = {};
    int numSpheres_ = 0;
    int insideSphere_ = -1;          // the sphere our toon is in (one request per entry)
    bool requestPending_ = false;    // inside a sphere, requestInteract not yet sent
    uint32_t requestedDoId_ = 0;

    struct Joke {
        bool active = false;
        uint32_t doId = 0;
        uint32_t serial = 0;
        uint32_t avatarId = 0;
        uint32_t startMs = 0;
        int beat = -1;               // last beat started (see kJokeBeats in the .cpp)
        int sphere = -1;
        game::KnockKnockJoke text = {};
        char setupWho[96] = {};
        const char* doorPhrase = nullptr;
    } joke_;

    audio::SfxId sfxKnock_ = audio::kInvalidSfx;
    audio::SfxId sfxRimshot_ = audio::kInvalidSfx;
};

} // namespace tt::world
