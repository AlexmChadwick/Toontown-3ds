#include "core/gpu_sync.h"
#include "world/golf.h"

#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstring>

#include "core/log.h"
#include "net/net_platform.h"

namespace tt::world {

void GolfSession::shutdown() {
    // A network event may replace resources before this frame's GPU fence.
    if (hole_.loaded() || kart_.loaded()) ::tt::gpu::frameSync();
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
    // bake_golf / bake_golf_physics: each HoleInfo entry has its own terrain and physics bake
    // with only its blockers.
    snprintf(path, sizeof(path), "sdmc:/3ds/toontown3ds/data/golf/hole_%d.ttm", holeId);
    assetBatch_.load(hole_, path);
    snprintf(path, sizeof(path), "sdmc:/3ds/toontown3ds/data/golf/hole_%d.gph", holeId);
    if (!physics_.load(path)) TT_LOG("golf: missing physics %s", path);
    ballX_ = 0.0f; ballY_ = 0.0f; ballZ_ = 0.25f; aim_ = 0.0f; power_ = 50.0f; swung_ = false;
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
            if (h->recordPending && h->recordAvatar == client.avatarId() && h->recordCycle != recordedCycle_) {
                golf::Movie movie;
                golf::Ball start; start.x = h->ballX; start.y = h->ballY; start.z = h->ballZ;
                // Cup contact is represented by the baked cup mesh; the final distance
                // check remains the stock server's responsibility.
                golf::recordSwing(movie, physics_, start, static_cast<float>(h->recordPower), h->recordDirX,
                                  h->recordDirY, ballX_, ballY_, ballZ_);
                if (client.sendGolfMovie(h->recordCycle, h->recordAvatar, movie)) {
                    recordedCycle_ = h->recordCycle;
                    TT_LOG("golf: recorder movie accepted frames %u", unsigned(movie.count));
                }
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
    if (power_ > 100.0f) power_ = 100.0f;
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

namespace {

// DistributedGolfKart.setColor: main_body takes the colour; cart_base a colour scale with the
// same hue at a third of the saturation (its hand-rolled HSV round trip, quirks kept).
void kartTints(const net::GolfKart& k, render::Tints* t) {
    t->setAll(1, 1, 1);
    const float red = k.color[0] / 255.0f, green = k.color[1] / 255.0f, blue = k.color[2] / 255.0f;
    t->rgba[1][0] = red; t->rgba[1][1] = green; t->rgba[1][2] = blue;
    float h = 0, s = 0, v = 0;
    if (red >= green && red > blue) {
        s = (red - blue) / red; v = red;
        h = green > blue ? (green - blue) / (red - blue) : (green - blue) / (red - green);
    } else if (green >= blue) {
        if (green <= 0.0f) return; // black: Python divides by zero here; keep white
        s = (green - blue) / green; v = green;
        h = red > blue ? 2 + (blue - red) / (green - blue) : 2 + (blue - red) / (green - red);
    } else {
        s = red > green ? (blue - green) / blue : (blue - red) / blue;
        h = red > green ? 4 + (red - green) / (blue - green) : 4 + (red - green) / (blue - red);
        v = blue;
    }
    if (!std::isfinite(h)) return;
    if (h < 0) h = (h * 60 + 360) / 60;
    s /= 3;
    float r = v, g = v, b = v;
    if (s != 0) {
        const int i = static_cast<int>(h);
        const float f = h - i, p = v * (1 - s), q = v * (1 - s * f), u = v * (1 - s * (1 - f));
        switch (i) {
        case 0: r = v; g = u; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = u; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = u; g = p; b = v; break;
        case 5: r = v; g = p; b = q; break;
        default: break; // Python leaves the input colour for other sectors
        }
    }
    t->rgba[2][0] = r; t->rgba[2][1] = g; t->rgba[2][2] = b;
}

// The kart's place on its parking spot: 'entering' rises from 10 below over 5 s, 'leaving'
// drives 45 units along heading - 90 over 5 s and stays there (trolleyEnter/ExitTrack).
void kartPos(const net::GolfKart& k, uint32_t nowMs, float* x, float* y, float* z) {
    *x = k.x; *y = k.y; *z = k.z;
    const float t = std::min(1.0f, static_cast<float>(nowMs - k.stateMs) / 5000.0f);
    if (!strcmp(k.state, "entering")) {
        *z = k.z - 10.0f * (1.0f - t);
    } else if (!strcmp(k.state, "leaving")) {
        const float a = (k.h - 90.0f) * 3.14159265f / 180.0f;
        *x += cosf(a) * 45.0f * t;
        *y += sinf(a) * 45.0f * t;
    }
}

} // namespace

void GolfSession::drawKart(render::Renderer& renderer, const net::Client& client) {
    if (!kart_.loaded()) return;
    const uint32_t nowMs = net::plat::nowMs();
    for (int i = 0; i < net::kMaxGolfKarts; ++i) {
        const net::GolfKart* k = client.golfKartAt(i);
        if (!k) continue;
        float x, y, z;
        kartPos(*k, nowMs, &x, &y, &z);
        C3D_Mtx m;
        Mtx_Identity(&m);
        Mtx_Translate(&m, x, y, z, true);
        Mtx_RotateZ(&m, k->h * 3.14159265f / 180.0f, true);
        render::Tints tints;
        kartTints(*k, &tints);
        renderer.drawModel(kart_, m, &tints);
    }
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
