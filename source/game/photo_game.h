#pragma once
#include "world/asset_batch.h"

#include <3ds.h>
#include <citro3d.h>

#include "game/photo_logic.h"
#include "net/client.h"
#include "render/model.h"
#include "render/renderer.h"

namespace tt::game {

class PhotoGame {
public:
    void pumpAssets() {
        assetBatch_.pump();
    }
    bool loadingAssets() const {
        return assetBatch_.loading();
    }
    PhotoGame() = default;

    bool init(const char* root, unsigned hood = 2000, uint32_t gameDoId = 0);
    void shutdown();

    // autoAim: test autoplay steers the tripod at matching subjects and snaps when on one.
    void update(net::Client& client, const circlePosition& pad, u32 keysDown, u32 keysHeld, float dt, u32 nowMs,
                bool autoAim);
    void draw(render::Renderer& renderer);

    void camera(C3D_FVec* eye, C3D_FVec* target) const;
    void toonPose(float* x, float* y, float* z, float* h) const;
    bool isDone() const { return sim_.isDone; }
    int score() const { return sim_.scores[0]; }
    void onAIScore(uint32_t avId, uint8_t assignment, float stars);

    const char* title() const { return "Photo Fun"; }
    const char* line(int i) const;

private:
    world::BasicAssetBatch<8> assetBatch_;
    photogame::Simulation sim_;
    // The camera is the tripod swivel (upstream __putCameraOnTripod), not a free camera.
    photogame::Swivel swivel_;
    float eyeX_ = 0.0f;
    float eyeY_ = 0.0f;
    float eyeZ_ = 0.0f;
    float shotCooldown_ = 0.0f;
    float timeRemaining_ = 60.0f;
    float elapsed_ = 0.0f;

    render::Model sceneModel_;
    render::Model subjectModel_;

    mutable char lines_[3][64] = {};
};

} // namespace tt::game
