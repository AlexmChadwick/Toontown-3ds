#pragma once
#include "world/asset_batch.h"

#include <3ds.h>
#include <citro3d.h>

#include "game/tag_logic.h"
#include "net/client.h"
#include "render/model.h"
#include "render/renderer.h"

namespace tt::game {

class TagGame {
public:
    void pumpAssets() {
        assetBatch_.pump();
    }
    bool loadingAssets() const {
        return assetBatch_.loading();
    }
    TagGame() = default;

    bool init(const char* root, uint32_t itAvId);
    void shutdown();

    void update(net::Client& client, const circlePosition& pad, u32 keysDown, float dt, u32 nowMs);
    void draw(render::Renderer& renderer);

    void camera(C3D_FVec* eye, C3D_FVec* target) const;
    void toonPose(float* x, float* y, float* z, float* h) const;
    bool isDone() const { return isDone_; }
    int score() const { return sim_.localScore; }
    void setAutoPlay(bool enabled) { autoPlay_ = enabled; }

    const char* title() const { return "Tag"; }
    const char* line(int i) const;

    // TagSetIt from the AI: the new IT (DistributedTagGame.setIt).
    void setIt(uint32_t avId) { sim_.setIt(avId); }

private:
    world::BasicAssetBatch<8> assetBatch_;
    taggame::Simulation sim_;
    taggame::Walker me_;
    float tagSendCooldown_ = 0.0f;
    bool isDone_ = false;
    bool autoPlay_ = false;
    char itName_[net::kNameLen] = {};

    // The IT sign (DistributedTagGame.itPointer, reparented to the IT toon at its height).
    bool itPointerValid_ = false;
    float itPointerX_ = 0.0f;
    float itPointerY_ = 0.0f;
    float itPointerZ_ = 0.0f;

    render::Model arenaModel_;
    render::Model treasureModel_;
    render::Model skyModel_;
    render::Model pointerModel_;

    mutable char lines_[3][64] = {};
};

} // namespace tt::game
