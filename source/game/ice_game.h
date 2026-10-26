#pragma once
#include "world/asset_batch.h"

#include <3ds.h>
#include <citro3d.h>

#include "game/ice_logic.h"
#include "net/client.h"
#include "render/model.h"
#include "render/renderer.h"

namespace tt::game {

class IceGame {
public:
    void pumpAssets() {
        assetBatch_.pump();
    }
    bool loadingAssets() const {
        return assetBatch_.loading();
    }
    IceGame() = default;

    bool init(const char* root, int players = 1, int slot = 0);
    void shutdown();

    void update(net::Client& client, const circlePosition& pad, u32 keysDown, float dt, u32 nowMs);
    void draw(render::Renderer& renderer);

    void camera(C3D_FVec* eye, C3D_FVec* target) const;
    void toonPose(float* x, float* y, float* z, float* h) const;
    // The tire of the participant at avIdList index 'index' (every toon rides its own tire).
    void tirePose(int index, float* x, float* y) const;
    bool isDone() const { return sim_.isDone; }
    int score() const { return sim_.scores[0]; }

    const char* title() const { return "Ice Slide"; }
    const char* line(int i) const;

    void onServer(net::EventType event, const net::Minigame& game);
    void onSetMatchAndRound(uint8_t match, uint8_t round);

private:
    world::BasicAssetBatch<8> assetBatch_;
    icegame::Simulation sim_;
    int slot_ = 0; // this toon's avIdList index: its tire is sim_.tires[slot_]
    enum class State {
        Aiming,
        WaitChoices,
        Sliding,
        WaitEnding,
        Scoring,
        WaitRound
    };
    State state_ = State::Aiming;

    float aimAngle_ = 45.0f;
    float aimForce_ = 50.0f;
    float scoreTimer_ = 0.0f;

    render::Model arenaModel_;
    render::Model backdropModel_;
    render::Model tireModel_;
    render::Model scoreCircleModel_;

    mutable char lines_[3][64] = {};
};

} // namespace tt::game
