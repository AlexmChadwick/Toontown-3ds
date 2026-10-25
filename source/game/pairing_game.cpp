#include "game/pairing_game.h"
#include "core/log.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace tt::game {
namespace {
constexpr const char* kDir = "sdmc:/3ds/toontown3ds/data/minigames/";
constexpr float kFlipSeconds = .25f; // PairingGameCard.FlipTime
constexpr float kAutoSeconds = .5f; // one deliberate test press per bonus-glow tick
constexpr float kRadians = 3.14159265f / 180;
}
void PairingGame::init(net::Client& client, uint32_t nowMs, bool autoPlay) {
    done_ = false;
    autoPlay_ = autoPlay;
    startMs_ = nowMs;
    matches_ = flips_ = score_ = cursor_ = 0;
    pendingCard_ = -1;
    autoDelay_ = 0;
    deckReady_ = false;
    totalCards_ = pairing::kSoloDeckCards;
    const auto* mg = client.minigame();
    // PairingGameGlobals.calcGameDuration, MinigameGlobals.getDifficulty.
    const u32 safeZones[] = {2000, 1000, 5000, 4000, 3000, 9000};
    float difficulty = 0;
    if (mg) {
        for (int i = 0; i < 6; ++i)
            if (safeZones[i] == mg->trolleyZone / 1000 * 1000) difficulty = i / 5.0f;
        if (mg->difficultyOverride != net::kNoDifficultyOverride) difficulty = mg->difficultyOverride / 65536.0f;
        pairing::generateDeck(mg->pairingDeckSeed, mg->numParticipants, deck_, totalCards_);
        deckReady_ = true; // zero is a valid random seed
    }
    duration_ = timeRemaining_ = 120 - 30 * difficulty;
    for (int i = 0; i < pairing::kMaxDeckCards; ++i) {
        matchedAge_[i] = 0;
        cardStatus_[i] = FaceDown;
        roll_[i] = rollFrom_[i] = 180;
        flipAge_[i] = kFlipSeconds;
    }
    char path[112];
    std::snprintf(path, sizeof(path), "%smemory_room.ttm", kDir);
    assetBatch_.load(memoryRoomModel_, path);
    for (int rank = 0; rank < pairing::kMaxRank; ++rank) {
        std::snprintf(path, sizeof(path), "%spairing_card_%d.ttm", kDir, rank);
        assetBatch_.load(cardModel_[rank], path);
    }
    for (auto& line : lines_) line[0] = 0;
}
void PairingGame::shutdown() {
    assetBatch_.clear();
    memoryRoomModel_.unload();
    for (auto& card : cardModel_) card.unload();
    done_ = true;
}
void PairingGame::turn(int card, CardStatus status) {
    if (card < 0 || card >= totalCards_) return;
    rollFrom_[card] = roll_[card];
    flipAge_[card] = 0;
    cardStatus_[card] = status;
    if (status == Matched) matchedAge_[card] = 0;
}
void PairingGame::update(net::Client& client, const circlePosition& pad, u32 down, float dt, uint32_t nowMs) {
    (void)pad;
    if (done_) return;
    const auto* mg = client.minigame();
    if (!mg || !mg->started) return;
    // Start at the AI's timestamp, not during four seconds of rules/loading.
    timeRemaining_ = std::max(0.0f, duration_ - float(int32_t(nowMs - mg->gameStartMs)) / 1000);
    net::PairingAnswer answer;
    while (client.popPairingAnswer(answer)) {
        if (answer.up < 0 || answer.up >= totalCards_) continue;
        if (answer.avatar == client.avatarId()) pendingCard_ = -1;
        turn(answer.up, FaceUp);
        if (answer.match >= 0 && answer.match < totalCards_ &&
            cardStatus_[answer.match] != Matched) {
            turn(answer.up, Matched);
            turn(answer.match, Matched);
            ++matches_;
        }
        for (int i = 0; i < answer.count; ++i) turn(answer.down[i], FaceDown);
        ++flips_; // DistributedPairingGame.openCardResult: once per accepted wire answer
        score_ = pairing::calculateScore(answer.points, matches_, flips_, totalCards_);
        TT_LOG("pairing: answer card %d avatar %lu match %d points %d flips %d", answer.up,
               static_cast<unsigned long>(answer.avatar), answer.match, answer.points, flips_);
    }
    for (int i = 0; i < totalCards_; ++i) {
        if (cardStatus_[i] == Matched) matchedAge_[i] += dt;
        flipAge_[i] = std::min(kFlipSeconds, flipAge_[i] + dt);
        const float goal = cardStatus_[i] == FaceDown ? 180.0f : 0.0f;
        roll_[i] = rollFrom_[i] + (goal - rollFrom_[i]) * flipAge_[i] / kFlipSeconds;
    }
    if (timeRemaining_ <= 0 || matches_ == totalCards_ / 2 || mg->pairingEveryoneDone) {
        client.sendPairingDone();
        done_ = true;
        return;
    }
    if ((down & KEY_DLEFT) && cursor_ % 8) --cursor_;
    if ((down & KEY_DRIGHT) && cursor_ % 8 < 7) ++cursor_;
    if ((down & KEY_DUP) && cursor_ >= 8) cursor_ -= 8;
    if ((down & KEY_DDOWN) && cursor_ + 8 < totalCards_) cursor_ += 8;
    // Cards are on the top screen. Bottom-screen touches belong to its panel.
    autoDelay_ += dt;
    bool request = down & KEY_A;
    if (autoPlay_ && autoDelay_ >= kAutoSeconds && pendingCard_ < 0) {
        for (int n = 1; n <= totalCards_; ++n) {
            const int index = (cursor_ + n) % totalCards_;
            if (cardStatus_[index] == FaceDown) { cursor_ = index; request = true; break; }
        }
    }
    if (request && deckReady_ && pendingCard_ < 0 && cursor_ < totalCards_ && cardStatus_[cursor_] == FaceDown) {
        int traversal[pairing::kMaxDeckCards], count = 0;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 2; ++j)
                for (int row = 0; row < 5; ++row) {
                    const int card = row * 8 + i + j * 4;
                    if (card < totalCards_) traversal[count++] = card;
                }
        const int tick = int((nowMs - mg->gameStartMs) / 500u) % count;
        if (client.sendPairingOpenCard(int16_t(cursor_), int16_t(traversal[tick]))) {
            pendingCard_ = cursor_;
            autoDelay_ = 0;
            TT_LOG("pairing: request card %d glow %d", cursor_, traversal[tick]);
        }
    }
    std::snprintf(lines_[0], sizeof(lines_[0]), "Matches: %d / %d", matches_, totalCards_ / 2);
    std::snprintf(lines_[1], sizeof(lines_[1]), "Flips: %d  Time: %d s", flips_, int(std::ceil(timeRemaining_)));
    std::snprintf(lines_[2], sizeof(lines_[2]), "Card %d: D-pad select, A flip", cursor_ + 1);
}
void PairingGame::draw(render::Renderer& renderer) {
    C3D_Mtx m;
    Mtx_Identity(&m);
    if (memoryRoomModel_.loaded()) renderer.drawModel(memoryRoomModel_, m);
    for (int i = 0; i < totalCards_; ++i) {
        if (cardStatus_[i] == Matched && matchedAge_[i] >= 1) continue;
        auto& model = cardModel_[deck_[i].rank];
        if (!model.loaded()) continue;
        Mtx_Identity(&m);
        // DistributedPairingGame.getCardPos: 8 columns, solo rows expanded by 1.25.
        Mtx_Translate(&m, (i % 8) * 4.0f, (i / 8) * (totalCards_ <= 32 ? 5.0f : 4.0f), 0, true);
        Mtx_RotateY(&m, roll_[i] * kRadians, true); // Panda roll is about Y
        renderer.drawModel(model, m);
    }
}
void PairingGame::toonPose(float* x, float* y, float* z, float* h) const {
    if (x) *x = 0;
    if (y) *y = 0;
    if (z) *z = 0;
    if (h) *h = -45;
}
void PairingGame::camera(C3D_FVec* eye, C3D_FVec* target) const {
    // DistributedPairingGame.cameraThreeQuarterView; H=0, P=-62.89.
    if (eye) *eye = FVec3_New(14, -8.93352f, 33.4497f);
    if (target) *target = FVec3_New(14, -8.93352f + cosf(62.89f*kRadians)*30,
                                   33.4497f - sinf(62.89f*kRadians)*30);
}
} // namespace tt::game
