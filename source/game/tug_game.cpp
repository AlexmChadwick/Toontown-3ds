#include "game/tug_game.h"
#include "world/toon_avatar.h"

#include <cmath>
#include <cstdio>
#include "core/log.h"

namespace tt::game {

namespace {
constexpr const char* kDir = "sdmc:/3ds/toontown3ds/data/minigames/";
} // namespace

void TugGame::init(net::Client& client, uint32_t nowMs, bool autoPlay) {
    char path[128];
    snprintf(path, sizeof(path), "%stug_dock.ttm", kDir);
    dock_.load(path);
    snprintf(path, sizeof(path), "%stug_rope.ttm", kDir);
    rope_.load(path);

    const net::Minigame* mg = client.minigame();
    slot_ = 0;
    numPlayers_ = mg ? mg->numParticipants : 1;
    if (numPlayers_ < 1) numPlayers_ = 1;
    if (mg) {
        for (int i = 0; i < numPlayers_; ++i) {
            if (mg->participants[i] == client.avatarId()) slot_ = i;
        }
    }

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
    C3D_FrameSync();
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
            toonOffset_ = mg->tugOffsets[slot_];
            suitOffset_ = mg->tugSuitOffset;

            if (mg->tugStopped) {
                state_ = State::ContestOver;
                done_ = true;
            }
        }

        const float toonX = tuggame::kLeftDockX + toonOffset_;
        const float cogX = tuggame::kRightDockX + suitOffset_;

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
            won_ = (toonX - tuggame::kLeftDockX) < (cogX - tuggame::kRightDockX);
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
    *x = tuggame::kLeftDockX + toonOffset_;
    *y = tuggame::kDockY;
    *z = 0.1f;
    *h = 240.0f; // facing right towards cog
}

void TugGame::camera(C3D_FVec* eye, C3D_FVec* target) const {
    *eye = FVec3_New(0.0f, 2.0f, 6.0f);
    *target = FVec3_New(0.0f, tuggame::kDockY, 2.0f);
}

void TugGame::draw(render::Renderer& renderer) {
    C3D_Mtx m;
    Mtx_Identity(&m);

    // Dock
    if (dock_.loaded()) {
        renderer.drawModel(dock_, m);
    }

    // Rope stretched between toon and cog
    if (rope_.loaded()) {
        const float toonX = tuggame::kLeftDockX + toonOffset_;
        const float cogX = tuggame::kRightDockX + suitOffset_;
        const float midX = (toonX + cogX) * 0.5f;
        const float len = std::abs(cogX - toonX);

        Mtx_Identity(&m);
        Mtx_Translate(&m, midX, tuggame::kDockY, 1.2f, true);
        Mtx_Scale(&m, len, 1.0f, 1.0f);
        renderer.drawModel(rope_, m);
    }
}

void TugGame::applyAnimation(world::ToonAvatar& avatar) const {
    if(state_==State::Tugging && tracker_.rate()>0) avatar.playAction(world::kAnimTugOWar,world::ClipMode::Loop);
    else avatar.hold(world::kAnimTugOWar,3/24.0f);
}

} // namespace tt::game
