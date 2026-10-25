// Interior and street life (docs/design/interior_life.md):
// 1. Toon HQ Leaderboard (DistributedHQInterior): empty_board decoding, rows, trophy stars
// 2. Animated interior props: Pet Shop fish, Toon HQ periscope
// 3. Knock-knock doors (DistributedKnockKnockDoor): street joke sequences, dialog, timings, SFX
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

    bool init();
    void shutdown();

    // Called on zone load / transition
    void setZone(uint32_t zoneId, const char* ptsPath);

    // Network event dispatch
    void handleEvent(const net::Event& ev, const net::Client& client, uint32_t nowMs);

    // Simulation tick
    void update(float dt, float playerX, float playerY, float playerZ, net::Client& client, uint32_t nowMs);

    // 3D world draw (for fish, periscope, trophy stars)
    void draw(render::Renderer& renderer, const C3D_FVec& eye);

    // Top-screen 2D pass (for HQ leaderboard text on empty_board)
    void draw2D(const C3D_Mtx& proj, const C3D_Mtx& view);

    // Adds speech balloons for knock-knock interactions
    void addBalloons(ui::Balloons& balloons, const C3D_Mtx& proj, const C3D_Mtx& view,
                     float playerX, float playerY, float playerZ, float nametagH, const char* playerName);

    // Autotest helpers
    void setTestLeaderboard();
    bool triggerKnockKnock(uint32_t zoneId, uint16_t propId);
    bool isKnockKnockActive() const { return knockActive_; }
    bool isHqActive() const { return hqActive_; }
    bool isPetShopActive() const { return petShopActive_; }

private:
    void updateLeaderboardText();
    void parsePtsPoints(const char* ptsPath);

    uint32_t zoneId_ = 0;
    bool hqActive_ = false;
    bool petShopActive_ = false;
    bool streetActive_ = false;

    // --- Toon HQ Leaderboard ---
    render::Model starModel_;
    net::Client::HQLeaderboardState hqState_ = {};
    uint32_t hqRevision_ = 0;
    float starAngles_[10] = {}; // star rotation angles in degrees

    // Cached 2D text for board
    C2D_TextBuf textBuf_ = nullptr;
    C2D_Text titleText_ = {};
    struct RowText {
        C2D_Text nameText = {};
        C2D_Text scoreText = {};
        bool hasStar = false;
        float starScale = 0.6f;
        float spinRate = 0.0f; // deg/s
        float starColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    } rows_[10] = {};
    bool textValid_ = false;

    // --- Animated Interior Props ---
    // Pet shop fish
    render::Model fishModel_;
    render::Anim fishAnim_;
    float fishTime_ = 0.0f;

    // HQ periscope
    render::Model periscopeModel_;
    render::Anim periscopeAnim_;
    float periscopeTime_ = 0.0f;

    // --- Knock-Knock Doors ---
    struct StreetDoorAnchor {
        uint16_t propId = 0;
        float x = 0.0f, y = 0.0f, z = 0.0f, h = 0.0f;
    };
    static constexpr size_t kMaxStreetDoors = 32;
    StreetDoorAnchor streetDoors_[kMaxStreetDoors] = {};
    size_t numStreetDoors_ = 0;

    bool knockActive_ = false;
    uint16_t knockPropId_ = 0;
    uint32_t knockDoId_ = 0;
    float knockDoorX_ = 0.0f, knockDoorY_ = 0.0f, knockDoorZ_ = 0.0f;
    float knockTimer_ = 0.0f;
    int knockBeat_ = -1; // -1 = idle, 0 = knock, 1 = who's there, 2 = setup, 3 = setup who, 4 = punchline
    game::KnockKnockJoke currentJoke_ = {};
    char jokeSetupWho_[96] = {};

    audio::SfxId sfxKnock_ = audio::kInvalidSfx;
    audio::SfxId sfxJoke_ = audio::kInvalidSfx;
};

} // namespace tt::world
