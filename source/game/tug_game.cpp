#include "game/tug_game.h"
#include "world/toon_avatar.h"

#include <cmath>
#include <cstdio>
#include "core/log.h"

namespace tt::game {

namespace {
constexpr const char* kDir = "sdmc:/3ds/toontown3ds/data/minigames/";

// DistributedTugOfWarGame.load / onstage: the dock room at (0, 18.39, -FloorOffset), scale 0.4.
constexpr float kRoomY = 18.39f;
constexpr float kFloorOffset = 0.025f; // ToontownGlobals.FloorOffset
constexpr float kRoomScale = 0.4f;
// onstage: sky.setZ(-5); TT_sky is baked at scale 2.8 for the cannon game, so undo that.
constexpr float kSkyZ = -5.0f;
constexpr float kSkyBakedScale = 2.8f;
// The toons stand on the dock at z 0.1 (dockPositions); the Cog too (origSuitPosHpr).
constexpr float kDockZ = tuggame::kDockZ;
// The rope runs from the right hand (about 0.5 in front of the toon) to the opponent's hand, at
// hand height. The upstream Rope is a 0.2-thick billboard (rope.ropeNode.setThickness(0.2)).
constexpr float kHandReach = 0.5f;
constexpr float kRopeZ = 2.0f;
constexpr float kRopeThickness = 0.2f;
// initCamera: the game camera at (0, 1.0838, 2.745) with hpr (-1.13, 1.042, 0).
constexpr float kCamX = 0.0f;
constexpr float kCamY = 1.0838f;
constexpr float kCamZ = 2.745f;
constexpr float kCamHeadingDeg = -1.13f;
constexpr float kCamPitchDeg = 1.042f;
constexpr float kCamLookDistance = 20.0f; // the target only sets the view direction
constexpr float kDegToRad = 3.14159265f / 180.0f;
} // namespace

void TugGame::init(net::Client& client, uint32_t nowMs, bool autoPlay) {
    char path[128];
    snprintf(path, sizeof(path), "%sminigame_sky.ttm", kDir);
    assetBatch_.load(sky_, path);
    snprintf(path, sizeof(path), "%stug_dock.ttm", kDir);
    assetBatch_.load(dock_, path);
    snprintf(path, sizeof(path), "%stug_rope.ttm", kDir);
    assetBatch_.load(rope_, path);

    const net::Minigame* mg = client.minigame();
    slot_ = 0;
    numPlayers_ = mg ? mg->numParticipants : 1;
    if (numPlayers_ < 1) numPlayers_ = 1;
    if (numPlayers_ > net::kMinigamePlayers) numPlayers_ = net::kMinigamePlayers;
    if (mg) {
        for (int i = 0; i < numPlayers_; ++i) {
            if (mg->participants[i] == client.avatarId()) slot_ = i;
        }
        suitLevel_ = mg->tugSuitType;
    }
    // calculatePositions (toon vs Cog): every toon's dock slot, and the Cog's.
    for (int i = 0; i < net::kMinigamePlayers; ++i) {
        toonX_[i] = i < numPlayers_ ? tuggame::toonX(numPlayers_, i, 0.0f) : 0.0f;
    }
    toonBaseX_ = tuggame::dockX(tuggame::toonDockIndex(numPlayers_, slot_));
    cogBaseX_ = tuggame::dockX(tuggame::suitDockIndex(numPlayers_));

    state_ = State::WaitGo;
    done_ = false;
    autoPlay_ = autoPlay;
    startMs_ = nowMs;
    lastUpdateMs_ = nowMs;
    toonOffset_ = 0.0f;
    suitOffset_ = 0.0f;
    won_ = false;
    tracker_.reset();

    readySent_ = false; // reportPlayerReady waits for setGameStart (see update)

    snprintf(lines_[0], sizeof(lines_[0]), "Tug-of-War!");
    snprintf(lines_[1], sizeof(lines_[1]), "Alternate buttons as fast as you can!");
    snprintf(lines_[2], sizeof(lines_[2]), "Get Ready...");

    TT_LOG("tug_game: init slot %d/%d, waiting for go signal", slot_, numPlayers_);
}

void TugGame::shutdown() {
    assetBatch_.clear();
    C3D_FrameSync();
    sky_.unload();
    dock_.unload();
    rope_.unload();
}

void TugGame::update(net::Client& client, const circlePosition& pad, u32 kDown, float dt, uint32_t nowMs) {
    const net::Minigame* mg = client.minigame();

    if (state_ == State::WaitGo) {
        // The AI's game FSM only enters waitClientsReady on setGameStart; a reportPlayerReady
        // before that is lost and the contest never gets its sendGoSignal.
        if (mg && mg->started && !readySent_) {
            client.sendTugPlayerReady(0); // toons are side 0 in toon vs cog
            readySent_ = true;
            startMs_ = nowMs;
        }
        if ((mg && mg->tugGo) || (readySent_ && nowMs - startMs_ > 15000)) {
            state_ = State::Tugging;
            startMs_ = nowMs;
            lastUpdateMs_ = nowMs;
            TT_LOG("tug_game: Go signal received, contest started!");
        }
        return;
    }

    const float elapsedSec = static_cast<int>(nowMs - startMs_) / 1000.0f;
    const float idealRate = tuggame::targetRateAt(elapsedSec);
    const int timeLeft = static_cast<int>(tuggame::kGameDuration - elapsedSec);

    if (state_ == State::Tugging) {
        // Player input: alternate Left / Right or A / B
        if (kDown & KEY_DLEFT) tracker_.press(0);
        else if (kDown & KEY_DRIGHT) tracker_.press(1);
        else if (kDown & KEY_A) tracker_.press(2);
        else if (kDown & KEY_B) tracker_.press(3);
        else if (pad.dx < -70) tracker_.press(0);
        else if (pad.dx > 70) tracker_.press(1);

        // AutoPlay: press keys automatically at ideal rate
        if (autoPlay_) {
            static float autoTimer = 0.0f;
            static int autoKey = 0;
            autoTimer += dt;
            const float interval = 1.0f / (idealRate > 0 ? idealRate : 6.0f);
            if (autoTimer >= interval) {
                autoTimer = 0.0f;
                autoKey = 1 - autoKey;
                tracker_.press(autoKey);
            }
        }

        tracker_.update(dt, idealRate);

        // Send current key rate every 0.2s
        if ((nowMs - lastUpdateMs_) >= 200) {
            lastUpdateMs_ = nowMs;
            const float force = tuggame::computeForce(tracker_.rate(), idealRate, 1.0f, tracker_.award());
            client.sendTugKeyRate(tracker_.rate(), force);
        }

        // Apply offsets from server
        if (mg) {
            toonOffset_ = mg->tugOffsets[slot_] / tuggame::kHandycap;
            suitOffset_ = mg->tugSuitOffset;
            for (int i = 0; i < numPlayers_; ++i) {
                toonX_[i] = tuggame::toonX(numPlayers_, i, mg->tugOffsets[i]);
            }

            if (mg->tugStopped) {
                state_ = State::ContestOver;
                done_ = true;
            }
        }

        const float toonX = toonBaseX_ + toonOffset_;
        const float cogX = cogBaseX_ + suitOffset_;

        // Check falling into water/mud
        if (tuggame::checkToonFallen(toonX)) {
            client.sendTugEndOfContest(0); // toon lost
            state_ = State::ContestOver;
            done_ = true;
            won_ = false;
            TT_LOG("tug_game: Toon fell into water!");
        } else if (tuggame::checkCogFallen(cogX)) {
            client.sendTugEndOfContest(1); // cog lost
            state_ = State::ContestOver;
            done_ = true;
            won_ = true;
            TT_LOG("tug_game: Cog fell into water!");
        } else if (elapsedSec >= tuggame::kGameDuration) {
            state_ = State::ContestOver;
            done_ = true;
            won_ = (toonX - toonBaseX_) < (cogX - cogBaseX_);
        }

        snprintf(lines_[0], sizeof(lines_[0]), "Pull! Alternate Left / Right");
        snprintf(lines_[1], sizeof(lines_[1]), "Speed: %d (Target: %.0f)", tracker_.rate(), idealRate);
        snprintf(lines_[2], sizeof(lines_[2]), "Time: %d", timeLeft > 0 ? timeLeft : 0);
    }

    if (state_ == State::ContestOver) {
        if (won_) {
            snprintf(lines_[0], sizeof(lines_[0]), "You Won! The Cog got soaked!");
            snprintf(lines_[1], sizeof(lines_[1]), "You earned %d jellybeans!", tuggame::kWinBeans);
        } else {
            snprintf(lines_[0], sizeof(lines_[0]), "Too bad! You took a mud bath.");
            snprintf(lines_[1], sizeof(lines_[1]), "You got %d jellybeans.", tuggame::kLossBeans);
        }
        lines_[2][0] = 0;
    }
}

void TugGame::toonPose(float* x, float* y, float* z, float* h) const {
    *x = toonBaseX_ + toonOffset_;
    *y = tuggame::kDockY;
    *z = kDockZ;
    *h = tuggame::kToonHeading; // facing right towards cog
}

void TugGame::camera(C3D_FVec* eye, C3D_FVec* target) const {
    // Heading/pitch to a forward vector (Panda: heading 0 looks along +y, pitch up is positive).
    const float h = kCamHeadingDeg * kDegToRad, p = kCamPitchDeg * kDegToRad;
    const float fx = -sinf(h) * cosf(p), fy = cosf(h) * cosf(p), fz = sinf(p);
    *eye = FVec3_New(kCamX, kCamY, kCamZ);
    *target = FVec3_New(kCamX + fx * kCamLookDistance, kCamY + fy * kCamLookDistance, kCamZ + fz * kCamLookDistance);
}

int TugGame::cogs(MinigameCog* out, int max) const {
    if (max < 1) return 0;
    // createSuits: the Cog's dock slot, hpr 120, pulled by the server's suit offset.
    MinigameCog& c = out[0];
    c.x = cogBaseX_ + suitOffset_;
    c.y = tuggame::kDockY;
    c.z = kDockZ;
    c.h = tuggame::kCogHeading;
    std::snprintf(c.code, sizeof(c.code), "%s", tuggame::suitCode(suitLevel_));
    c.walking = false; // the tug-o-war pose is not in the Suits animation set; it holds neutral
    return 1;
}

void TugGame::draw(render::Renderer& renderer) {
    C3D_Mtx m;
    Mtx_Identity(&m);

    // The sky dome (TT_sky) at z -5, then the dock room with its water.
    if (sky_.loaded()) {
        Mtx_Identity(&m);
        Mtx_Translate(&m, 0.0f, 0.0f, kSkyZ, true);
        Mtx_Scale(&m, 1.0f / kSkyBakedScale, 1.0f / kSkyBakedScale, 1.0f / kSkyBakedScale);
        renderer.drawModel(sky_, m);
    }
    if (dock_.loaded()) {
        Mtx_Identity(&m);
        Mtx_Translate(&m, 0.0f, kRoomY, -kFloorOffset, true);
        Mtx_Scale(&m, kRoomScale, kRoomScale, kRoomScale);
        renderer.drawModel(dock_, m);
    }

    // One rope per toon, from its hand to the Cog's hand (numRopes = numPlayers for toon vs Cog).
    if (rope_.loaded()) {
        const float cogX = cogBaseX_ + suitOffset_;
        for (int i = 0; i < numPlayers_; ++i) {
            const float fromX = toonX_[i] + kHandReach;
            const float toX = cogX - kHandReach;
            const float midX = (fromX + toX) * 0.5f;
            const float len = std::abs(toX - fromX);
            if (len <= 0.0f) continue;

            Mtx_Identity(&m);
            Mtx_Translate(&m, midX, tuggame::kDockY, kRopeZ, true);
            Mtx_Scale(&m, len, 1.0f, kRopeThickness);
            renderer.drawModel(rope_, m);
        }
    }
}

void TugGame::applyAnimation(world::ToonAvatar& avatar) const {
    if(state_==State::Tugging && tracker_.rate()>0) avatar.playAction(world::kAnimTugOWar,world::ClipMode::Loop);
    else avatar.hold(world::kAnimTugOWar,3/24.0f);
}

} // namespace tt::game
