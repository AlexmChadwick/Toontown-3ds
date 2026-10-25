#pragma once
#include "world/asset_batch.h"

#include <3ds.h>
#include <citro3d.h>

#include "game/pairing_logic.h"
#include "net/client.h"
#include "render/model.h"
#include "render/renderer.h"

namespace tt::game {

class PairingGame {
public:
    void pumpAssets() {
        assetBatch_.pump();
    }
    bool loadingAssets() const {
        return assetBatch_.loading();
    }
    void init(net::Client& client, uint32_t nowMs, bool autoPlay);
    void shutdown();
    void update(net::Client& client, const circlePosition& pad, u32 kDown, float dt, uint32_t nowMs);
    void draw(render::Renderer& renderer);

    void toonPose(float* x, float* y, float* z, float* h) const;
    bool toonRunning() const { return false; }
    void camera(C3D_FVec* eye, C3D_FVec* target) const;

    const char* line(int i) const { return (i >= 0 && i < 3) ? lines_[i] : ""; }
    bool isDone() const { return done_; }

private:
    world::BasicAssetBatch<8> assetBatch_;
    enum CardStatus : uint8_t {
        FaceDown,
        FaceUp,
        Matched,
    };

    bool done_ = false;
    bool autoPlay_ = false;
    uint32_t startMs_ = 0;
    float timeRemaining_ = 90.0f;

    pairing::Card deck_[pairing::kSoloDeckCards];
    CardStatus cardStatus_[pairing::kSoloDeckCards] = {};
    int totalCards_ = pairing::kSoloDeckCards;
    int matches_ = 0;
    int flips_ = 0;
    int score_ = 0;

    int cursor_ = 0; // 0..31
    int openCard1_ = -1;
    int openCard2_ = -1;

    char lines_[3][48] = {};

    render::Model memoryRoomModel_;
    render::Model cardModel_;
};

} // namespace tt::game
