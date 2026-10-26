#pragma once
#include "world/asset_batch.h"

#include <3ds.h>
#include <citro3d.h>

#include "net/client.h"
#include "game/golf_logic.h"
#include "render/model.h"
#include "render/renderer.h"

namespace tt::world {

class GolfSession {
public:
    void shutdown();
    void begin(net::Client& client, uint32_t zone, int8_t courseId, uint32_t nowMs);
    void pumpAssets() {
        assetBatch_.pump();
    }
    bool loadingAssets() const {
        return assetBatch_.loading();
    }
    bool active() const { return active_; }
    void handleEvent(const net::Event& ev, net::Client& client, uint32_t nowMs);
    void update(net::Client& client, const circlePosition& pad, uint32_t keysDown, float dt, uint32_t nowMs);
    void draw(render::Renderer& renderer);
    void drawKart(render::Renderer& renderer, const net::Client& client);
    void camera(C3D_FVec* eye, C3D_FVec* target) const;
    void toonPose(float* x, float* y, float* z, float* h) const;
    bool toonRunning() const { return false; }
    uint32_t popReturn();
    const char* title() const { return title_; }
    const char* line(int i) const { return i >= 0 && i < 3 ? lines_[i] : ""; }

private:
    BasicAssetBatch<5> assetBatch_;
    void loadHole(int holeId);
    void setText();

    bool active_ = false;
    bool sentCourseReady_ = false;
    bool sentHoleReady_ = false;
    bool sentJoined_ = false;
    bool sentTee_ = false;
    bool swung_ = false;
    bool waitingMovie_ = false;
    bool sentRewardDone_ = false;
    uint32_t zone_ = 0;
    uint32_t startedMs_ = 0;
    uint32_t swingMs_ = 0;
    uint32_t returnZone_ = 17000;
    uint32_t returnPending_ = 0;
    float aim_ = 0.0f;
    float power_ = 3.0f;
    float ballX_ = 0.0f, ballY_ = 0.0f, ballZ_ = 0.25f;
    golf::Ball ball_;
    render::Model hole_;
    render::Model kart_;
    render::Model ballModel_;
    char title_[40] = {};
    char lines_[3][64] = {};
};

} // namespace tt::world
