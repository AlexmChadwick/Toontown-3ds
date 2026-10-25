#include "game/pairing_game.h"

#include <cmath>
#include <cstdio>
#include "core/log.h"

namespace tt::game {

static constexpr const char* kDir = "sdmc:/3ds/toontown3ds/data/minigames/";

void PairingGame::init(net::Client& client, uint32_t nowMs, bool autoPlay) {
    done_ = false;
    autoPlay_ = autoPlay;
    startMs_ = nowMs;
    timeRemaining_ = 90.0f;
    matches_ = 0;
    flips_ = 0;
    score_ = 0;
    cursor_ = 0;
    openCard1_ = -1;
    openCard2_ = -1;

    for (int i = 0; i < pairing::kSoloDeckCards; ++i) {
        cardStatus_[i] = FaceDown;
        deck_[i] = {};
    }

    uint32_t seed = (client.minigame()) ? client.minigame()->pairingDeckSeed : 0;
    if (seed != 0) {
        pairing::generateDeck(seed, 1, deck_, totalCards_);
    } else {
        totalCards_ = pairing::kSoloDeckCards;
    }

    char path[96];
    std::snprintf(path, sizeof(path), "%smemory_room.ttm", kDir);
    assetBatch_.load(memoryRoomModel_, path);
    std::snprintf(path, sizeof(path), "%splaying_card.ttm", kDir);
    assetBatch_.load(cardModel_, path);

    std::snprintf(lines_[0], sizeof(lines_[0]), "Matches: 0 / 16");
    std::snprintf(lines_[1], sizeof(lines_[1]), "Flips: 0  (Time: 90 s)");
    std::snprintf(lines_[2], sizeof(lines_[2]), "Jellybeans: 1");
}

void PairingGame::shutdown() {
    assetBatch_.clear();
    memoryRoomModel_.unload();
    cardModel_.unload();
    done_ = true;
}

void PairingGame::update(net::Client& client, const circlePosition& pad, u32 kDown, float dt, uint32_t nowMs) {
    (void)pad;
    (void)nowMs;
    if (done_) return;

    timeRemaining_ -= dt;
    if (timeRemaining_ <= 0.0f) {
        timeRemaining_ = 0.0f;
        done_ = true;
        client.sendPairingDone(); // DistributedPairingGame.timerExpired: reportDone
    }

    // Lazy deck generation once seed arrives
    if (deck_[0].value == 0 && deck_[1].value == 0 && client.minigame() && client.minigame()->pairingDeckSeed != 0) {
        pairing::generateDeck(client.minigame()->pairingDeckSeed, 1, deck_, totalCards_);
    }

    // Process server card results
    if (client.minigame()) {
        const auto* mg = client.minigame();
        if (mg->pairingEveryoneDone) {
            done_ = true;
        }
        if (mg->pairingCardToTurnUp >= 0 && mg->pairingCardToTurnUp < totalCards_) {
            const int up = mg->pairingCardToTurnUp;
            cardStatus_[up] = FaceUp;
            if (mg->pairingMatchingCard >= 0 && mg->pairingMatchingCard < totalCards_) {
                cardStatus_[up] = Matched;
                cardStatus_[mg->pairingMatchingCard] = Matched;
                matches_++;
                score_ = pairing::calculateScore(mg->pairingPoints, matches_, flips_, totalCards_);
            }
            for (int i = 0; i < mg->pairingTurnDownCount; ++i) {
                int down = mg->pairingCardsToTurnDown[i];
                if (down >= 0 && down < totalCards_ && cardStatus_[down] == FaceUp) {
                    cardStatus_[down] = FaceDown;
                }
            }
        }
    }

    // Input handling: D-pad / Circle Pad navigation
    if (kDown & KEY_DLEFT) {
        if (cursor_ % 8 > 0) cursor_--;
    } else if (kDown & KEY_DRIGHT) {
        if (cursor_ % 8 < 7) cursor_++;
    } else if (kDown & KEY_DUP) {
        if (cursor_ >= 8) cursor_ -= 8;
    } else if (kDown & KEY_DDOWN) {
        if (cursor_ + 8 < totalCards_) cursor_ += 8;
    }

    // Touch input
    touchPosition touch;
    hidTouchRead(&touch);
    if (kDown & KEY_TOUCH) {
        int col = (touch.px - 8) / 38;
        int row = (touch.py - 16) / 48;
        if (col >= 0 && col < 8 && row >= 0 && row < 4) {
            int tapped = row * 8 + col;
            if (tapped < totalCards_) {
                cursor_ = tapped;
                kDown |= KEY_A;
            }
        }
    }

    // Flip card on button A
    if ((kDown & KEY_A) || autoPlay_) {
        if (autoPlay_) {
            // Find next face-down card
            for (int i = 0; i < totalCards_; ++i) {
                if (cardStatus_[i] == FaceDown) {
                    cursor_ = i;
                    break;
                }
            }
        }
        if (cursor_ >= 0 && cursor_ < totalCards_ && cardStatus_[cursor_] == FaceDown) {
            cardStatus_[cursor_] = FaceUp;
            flips_++;
            // DistributedPairingGame.moveBonusGlowTask: the glow walks calcBonusTraversal's order
            // (8 x 5 grid, column pairs) every 0.5 s. The AI rejects an index outside the deck,
            // and its rejection path (logSuspicious) crashes the stock AI, so always send one.
            int traversal[pairing::kSoloDeckCards];
            int n = 0;
            for (int i = 0; i < 4; ++i)
                for (int j = 0; j < 2; ++j)
                    for (int row = 0; row < 5; ++row) {
                        const int card = row * 8 + i + j * 4;
                        if (card < totalCards_ && n < pairing::kSoloDeckCards) traversal[n++] = card;
                    }
            const int glowIndex = n ? static_cast<int>((nowMs - startMs_) / 500u) % n : 0;
            client.sendPairingOpenCard((int16_t)cursor_, (int16_t)(n ? traversal[glowIndex] : 0));

            // Local match resolution for responsive gameplay
            if (openCard1_ < 0) {
                openCard1_ = cursor_;
            } else if (openCard2_ < 0 && cursor_ != openCard1_) {
                openCard2_ = cursor_;
                if (pairing::isMatch(deck_[openCard1_], deck_[openCard2_])) {
                    cardStatus_[openCard1_] = Matched;
                    cardStatus_[openCard2_] = Matched;
                    matches_++;
                    score_ = pairing::calculateScore(matches_, matches_, flips_, totalCards_);
                    openCard1_ = -1;
                    openCard2_ = -1;
                    if (matches_ == totalCards_ / 2) {
                        client.sendPairingDone();
                        done_ = true;
                    }
                } else {
                    // Turn down after next move
                    cardStatus_[openCard1_] = FaceDown;
                    cardStatus_[openCard2_] = FaceDown;
                    openCard1_ = -1;
                    openCard2_ = -1;
                }
            }
        }
    }

    std::snprintf(lines_[0], sizeof(lines_[0]), "Matches: %d / %d", matches_, totalCards_ / 2);
    std::snprintf(lines_[1], sizeof(lines_[1]), "Flips: %d  (Time: %d s)", flips_, int(std::ceil(timeRemaining_)));
    std::snprintf(lines_[2], sizeof(lines_[2]), "Jellybeans: %d", score_ > 0 ? score_ : 1);
}

void PairingGame::draw(render::Renderer& renderer) {
    C3D_Mtx m;
    if (memoryRoomModel_.loaded()) {
        Mtx_Identity(&m);
        renderer.drawModel(memoryRoomModel_, m);
    }
    if (cardModel_.loaded()) {
        // Draw cards in 4x8 room grid on top screen
        for (int i = 0; i < totalCards_; ++i) {
            if (cardStatus_[i] != Matched) {
                const float x = (i % 8 - 3.5f) * 2.2f;
                const float y = (i / 8 - 1.5f) * 3.0f;
                const float pitch = (cardStatus_[i] == FaceUp) ? 180.0f : 0.0f;
                Mtx_Identity(&m);
                Mtx_Translate(&m, x, y, 0.1f, true);
                Mtx_RotateX(&m, pitch * 3.14159265f / 180.0f, true);
                Mtx_Scale(&m, 0.8f, 0.8f, 0.8f);
                renderer.drawModel(cardModel_, m);
            }
        }
    }
}

void PairingGame::toonPose(float* x, float* y, float* z, float* h) const {
    if (x) *x = 0.0f;
    if (y) *y = -8.0f;
    if (z) *z = 0.0f;
    if (h) *h = 0.0f;
}

void PairingGame::camera(C3D_FVec* eye, C3D_FVec* target) const {
    if (target) {
        target->x = 0.0f;
        target->y = 0.0f;
        target->z = 1.0f;
        target->w = 1.0f;
    }
    if (eye) {
        eye->x = 0.0f;
        eye->y = -14.0f;
        eye->z = 12.0f;
        eye->w = 1.0f;
    }
}

} // namespace tt::game
