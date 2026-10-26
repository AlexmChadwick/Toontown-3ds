#include "world/golf.h"

#include <cmath>
#include <cstdio>

#include "core/log.h"

namespace tt::world {

void GolfSession::shutdown() {
    // A network event may replace resources before this frame's GPU fence.
    if (hole_.loaded() || kart_.loaded()) C3D_FrameSync();
    assetBatch_.clear();
    hole_.unload();
    kart_.unload();
    active_ = false;
    returnPending_ = 0;
}

void GolfSession::begin(net::Client&, uint32_t zone, int8_t courseId, uint32_t nowMs) {
    shutdown();
    active_ = true;
    zone_ = zone;
    startedMs_ = nowMs;
    snprintf(title_, sizeof(title_), "Chip 'n Dale MiniGolf");
    snprintf(lines_[0], sizeof(lines_[0]), "Course %d  |  A: swing", int(courseId));
    snprintf(lines_[1], sizeof(lines_[1]), "Circle pad: aim / power");
    snprintf(lines_[2], sizeof(lines_[2]), "B: leave course");
    TT_LOG("golf: zone %u course %d", unsigned(zone), int(courseId));
}

void GolfSession::loadHole(int holeId) {
    char path[128];
    const int model = holeId == 0 ? 18 : ((holeId - 1) % 18) + 1;
    snprintf(path, sizeof(path), "sdmc:/3ds/toontown3ds/data/golf/hole%d.ttm", model);
    assetBatch_.load(hole_, path);
    ballX_ = 0.0f; ballY_ = 0.0f; ballZ_ = 0.25f; aim_ = 0.0f; power_ = 3.0f; swung_ = false;
    TT_LOG("golf: hole %d loaded", holeId);
}

void GolfSession::setText() {
    snprintf(lines_[0], sizeof(lines_[0]), "Hole in play  |  A: swing");
    snprintf(lines_[1], sizeof(lines_[1]), "Aim %3.0f  Power %2.1f", aim_, power_);
    snprintf(lines_[2], sizeof(lines_[2]), "B: exit to Acorn Acres");
}

void GolfSession::handleEvent(const net::Event& ev, net::Client& client, uint32_t nowMs) {
    if (ev.type == net::EventType::GolfKartUp || ev.type == net::EventType::GolfKartState) {
        if (!kart_.loaded())
            assetBatch_.load(kart_, "sdmc:/3ds/toontown3ds/data/golf/golf_cart3.ttm");
        return;
    }
    if (ev.type == net::EventType::GolfZone && !active_) {
        begin(client, ev.doId, client.golfKart() ? client.golfKart()->courseId : -1, nowMs);
        client.changeZone(ev.doId);
        TT_LOG("golf: change zone %u", unsigned(ev.doId));
        return;
    }
    if (!active_) return;
    if (ev.type == net::EventType::GolfCourseUp) {
        if (!sentJoined_) sentJoined_ = client.sendGolfJoined();
        if (!sentCourseReady_) sentCourseReady_ = client.sendGolfReadyCourse();
        TT_LOG("golf: course handshake joined=%d ready=%d", int(sentJoined_), int(sentCourseReady_));
    } else if (ev.type == net::EventType::GolfHoleUp) {
        const net::GolfHole* h = client.golfHole();
        loadHole(h ? h->holeId : 0);
        sentHoleReady_ = client.sendGolfReadyHole();
        setText();
        TT_LOG("golf: hole ready=%d", int(sentHoleReady_));
    } else if (ev.type == net::EventType::GolfHoleState) {
        const net::GolfHole* h = client.golfHole();
        if (h) {
            ballX_ = h->ballX;
            ballY_ = h->ballY;
            ballZ_ = h->ballZ;
            if (h->currentGolfer == static_cast<int>(client.avatarId()) && !sentTee_) {
                sentTee_ = client.sendGolfTee(0);
                TT_LOG("golf: tee selected=%d", int(sentTee_));
            }
        }
        setText();
    } else if (ev.type == net::EventType::GolfCourseState && client.golfCourse() &&
               (client.golfCourse()->exiting || client.golfCourse()->reward)) {
        if (client.golfCourse()->reward && !sentRewardDone_) {
            sentRewardDone_ = client.sendGolfDoneReward();
            TT_LOG("golf: reward done=%d", int(sentRewardDone_));
        }
        if (client.golfCourse()->exiting) {
            returnPending_ = returnZone_;
            TT_LOG("golf: exit received");
        }
    }
}

void GolfSession::update(net::Client& client, const circlePosition& pad, uint32_t keysDown, float dt, uint32_t nowMs) {
    if (!active_) return;
    aim_ += static_cast<float>(pad.dx) * dt * 0.08f;
    power_ += static_cast<float>(pad.dy) * dt * 0.01f;
    if (power_ < 1.0f) power_ = 1.0f;
    if (power_ > 7.0f) power_ = 7.0f;
    if ((keysDown & KEY_B) != 0) {
        client.sendGolfExitCourse();
        TT_LOG("golf: exit requested");
    } else if ((keysDown & KEY_A) != 0 && client.golfHole() && !swung_) {
        const float a = aim_ * 3.14159265f / 180.0f;
        const float dx = sinf(a), dy = cosf(a);
        swung_ = client.sendGolfSwing((nowMs - startedMs_) / 1000.0f, static_cast<int32_t>(power_), ballX_, ballY_,
                                      ballZ_, dx, dy);
        swingMs_ = nowMs;
        TT_LOG("golf: swing requested=%d", int(swung_));
    }
    if (swung_ && nowMs - swingMs_ > 1200) {
        client.sendGolfTurnDone();
        swung_ = false;
        TT_LOG("golf: playback acknowledged");
    }
    setText();
}

void GolfSession::draw(render::Renderer& renderer) {
    if (!active_ || !hole_.loaded()) return;
    C3D_Mtx m;
    Mtx_Identity(&m);
    renderer.drawModel(hole_, m);
}

void GolfSession::drawKart(render::Renderer& renderer, const net::Client& client) {
    const net::GolfKart* k = client.golfKart();
    if (!k || !kart_.loaded()) return;
    C3D_Mtx m;
    Mtx_Identity(&m);
    Mtx_Translate(&m, k->x, k->y, k->z, true);
    Mtx_RotateZ(&m, k->h * 3.14159265f / 180.0f, true);
    renderer.drawModel(kart_, m);
}

void GolfSession::camera(C3D_FVec* eye, C3D_FVec* target) const {
    *eye = FVec3_New(ballX_, ballY_ - 18.0f, ballZ_ + 10.0f);
    *target = FVec3_New(ballX_, ballY_ + 4.0f, ballZ_);
}

void GolfSession::toonPose(float* x, float* y, float* z, float* h) const {
    *x = ballX_ - sinf(aim_ * 3.14159265f / 180.0f) * 2.0f;
    *y = ballY_ - cosf(aim_ * 3.14159265f / 180.0f) * 2.0f;
    *z = ballZ_; *h = aim_;
}

uint32_t GolfSession::popReturn() {
    uint32_t r = returnPending_;
    returnPending_ = 0;
    return r;
}

} // namespace tt::world
