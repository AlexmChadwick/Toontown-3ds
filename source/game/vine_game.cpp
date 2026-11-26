#include "game/vine_game.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include "core/log.h"

namespace tt::game {

namespace {
constexpr const char* kDir = "sdmc:/3ds/toontown3ds/data/minigames/";
// DistributedVineGame.onstage: the start shelf at (-16, 0, 15), the end shelf at the last vine + 20,
// both scaled (1, 1, 0.75). The shelf models are baked with their own offsets.
constexpr float kStartShelfX = -16.0f;
constexpr float kShelfZ = 15.0f;
constexpr float kShelfScaleZ = 0.75f;
constexpr float kEndShelfOffset = 20.0f;
// DistributedVineGame.onstage: the treasures are placed at vine x + 15 with a height of
// randomNumGen.randrange(10, 25) (and an unused randrange(12, 18)).
constexpr float kBananaOffsetX = 15.0f;
// VineBat / VineSpider model scales; bats fly from VineXIncrement * NumVines to -VineXIncrement.
constexpr float kSpiderScale = 0.2f;
constexpr float kSpiderY = -0.5f;
constexpr float kBatScale = 0.15f;
constexpr float kBatStartX = vine::kVineSpacing * vine::kNumVines;
constexpr float kBatEndX = -vine::kVineSpacing;
constexpr int kBatMinHeight = 10, kBatMaxHeight = 28; // VineGameGlobals.BatMinHeight / BatMaxHeight
// The local Toon's catch spheres (radius 0.7) on its legs, torso, head and hands: a column from the
// feet to the hold joint stands in for them.
constexpr float kToonSphereRadius = 0.7f;
constexpr float kBananaScale = 0.25f; // VineTreasure: nodePath.setScale(0.25)
// DistributedVineGame.makeCameraFollowJumpingToon: the side camera at (x, -63.2, 16.3), looking along +y.
constexpr float kCamY = -53.0f; // cameraTwoVineSidePos (TwoVineView)
constexpr float kCamZ = 17.3f;
constexpr float kCamLookDistance = 40.0f;
// Climbing input (the Circle Pad, like the up/down arrows) and the autoplay's timing candidates.
constexpr int kPadDeadzone = 20;
constexpr float kPadFull = 150.0f;
constexpr float kAutoStartT = 0.25f;
constexpr float kAutoEndT = 0.95f;
constexpr float kAutoStepT = 0.05f;
constexpr float kBananaGrabX = 3.0f;
constexpr float kBananaGrabZ = 5.0f;
// d_setNewVineT: sent at once when the climb direction changes, otherwise after more than 0.2 s.
constexpr uint32_t kClimbSendMs = 200;
} // namespace

void VineGame::init(net::Client& client, uint32_t nowMs, bool autoPlay) {
    (void)nowMs;
    done_ = false;
    autoPlay_ = autoPlay;
    gameTime_ = 0.0f;
    timeRemaining_ = vine::kGameDuration;

    // setVineSections: the server's section per four vines picks the course (VineGameGlobals.CourseSections).
    const net::Minigame* mg = client.minigame();
    uint8_t sections[8] = {};
    int numSections = 0;
    if (mg && mg->vineSectionCount > 0) {
        numSections = mg->vineSectionCount;
        for (int i = 0; i < numSections; ++i) sections[i] = mg->vineSections[i];
    }
    if (!vine::buildCourse(sections, numSections, course_)) {
        // No usable sections from the server: the first course section keeps the game playable.
        TT_LOG("vine: no usable vineSections (%d), using section 0", numSections);
        const uint8_t fallback[4] = {0, 0, 0, 0};
        vine::buildCourse(fallback, 4, course_);
    }

    // DistributedMinigame.randomNumGen = RandomNumGen(doId): load() draws the bat heights, then
    // onstage() the banana heights, in that order.
    game::RandomNumGen rng(mg ? mg->doId : 0);
    loadBats(mg ? mg->trolleyZone - mg->trolleyZone % 1000 : 2000, rng);
    for (int i = 0; i < vine::kNumVines - 1; ++i) {
        bananaZ_[i] = float(rng.randrange(10, 25));
        rng.randrange(12, 18);
    }

    toon_ = {};
    vine::attachToVine(toon_, 0, course_[0], vine::swingAngle(course_[0], gameTime_), vine::kVineStartingT);
    for (bool& taken : bananaTaken_) taken = false;
    bananas_ = 0;
    score_ = 1;

    char path[96];
    std::snprintf(path, sizeof(path), "%svine_background.ttm", kDir);
    assetBatch_.load(background_, path);
    std::snprintf(path, sizeof(path), "%svine_bananas.ttm", kDir);
    assetBatch_.load(bananaModel_, path);
    std::snprintf(path, sizeof(path), "%svine_start.ttm", kDir);
    assetBatch_.load(startShelf_, path);
    std::snprintf(path, sizeof(path), "%svine_end.ttm", kDir);
    assetBatch_.load(endShelf_, path);
    for (int i = 0; i < 2; ++i) {
        std::snprintf(path, sizeof(path), "%svine_spider%d.ttm", kDir, i + 1);
        assetBatch_.load(spiderModel_[i], path);
    }
    for (int i = 0; i < 3; ++i) {
        std::snprintf(path, sizeof(path), "%svine_bat%d.ttm", kDir, i + 1);
        assetBatch_.load(batModel_[i], path);
    }
    std::snprintf(path, sizeof(path), "%svine_segment.ttm", kDir);
    assetBatch_.load(segmentModel_, path);
    std::snprintf(path, sizeof(path), "%svine_static.ttm", kDir);
    assetBatch_.load(staticModel_, path);

    std::snprintf(lines_[0], sizeof(lines_[0]), "Vine: 1 / 20");
    std::snprintf(lines_[1], sizeof(lines_[1]), "Bananas: 0  (Time: 70 s)");
    std::snprintf(lines_[2], sizeof(lines_[2]), "Jellybeans: 1");
}

void VineGame::shutdown() {
    assetBatch_.clear();
    background_.unload();
    segmentModel_.unload();
    staticModel_.unload();
    bananaModel_.unload();
    startShelf_.unload();
    endShelf_.unload();
    for (render::Model& m : spiderModel_) m.unload();
    for (render::Model& m : batModel_) m.unload();
    done_ = true;
}

void VineGame::loadBats(uint32_t hood, game::RandomNumGen& rng) {
    // VineGameGlobals.BatInfo: (timeToTraverseField, initialDelay[, 0.25 first-lap start multiplier]).
    struct Info { float traverse, delay, mult; };
    static constexpr Info kTT[] = {{60, 0, 0.35f}}, kDD[] = {{60, 0, 0.25f}, {30, 30, 1}},
                          kDG[] = {{60, 0, 0.25f}, {15, 30, 1}}, kMM[] = {{60, 0, 0.25f}, {10, 25, 1}},
                          kBR[] = {{60, 0, 0.25f}, {30, 30, 1}, {30, 20, 1}},
                          kDL[] = {{60, 0, 0.25f}, {30, 30, 1}, {10, 20, 1}};
    const Info* info = kTT;
    int n = 1;
    switch (hood) {
    case 1000: info = kDD; n = 2; break;
    case 5000: info = kDG; n = 2; break;
    case 4000: info = kMM; n = 2; break;
    case 3000: info = kBR; n = 3; break;
    case 9000: info = kDL; n = 3; break;
    default: break;
    }
    numBats_ = n;
    for (int b = 0; b < n; ++b) {
        Bat& bat = bats_[b];
        bat = {};
        bat.traverse = info[b].traverse;
        bat.delay = info[b].delay;
        // createBatIval: a third tuple entry means startMultiplier 0.25, whatever its value.
        bat.startMultiplier = info[b].mult < 1.0f ? 0.25f : 1.0f;
        float duration = bat.delay;
        while (duration < vine::kGameDuration && bat.laps < 8) {
            bat.height[bat.laps] = float(rng.randrange(kBatMinHeight, kBatMaxHeight));
            duration += bat.laps == 0 ? bat.traverse * bat.startMultiplier : bat.traverse;
            ++bat.laps;
        }
    }
}

// The bat's LerpPosInterval laps: parked past the course until its delay, then from startX (times the
// first lap's multiplier) to endX at the lap's height; it stays at endX after the last lap.
bool VineGame::batPose(int i, float t, float* x, float* z) const {
    const Bat& bat = bats_[i];
    float at = t - bat.delay;
    if (at < 0.0f || bat.laps == 0) return false;
    for (int lap = 0; lap < bat.laps; ++lap) {
        const float startX = lap == 0 ? kBatStartX * bat.startMultiplier : kBatStartX;
        const float dur = lap == 0 ? bat.traverse * bat.startMultiplier : bat.traverse;
        if (at <= dur || lap == bat.laps - 1) {
            const float u = std::min(at / dur, 1.0f);
            *x = startX + (kBatEndX - startX) * u;
            *z = bat.height[lap];
            return true;
        }
        at -= dur;
    }
    return false;
}

// localToonHitSpider / localToonHitBat: a hazard sphere meets the Toon's catch spheres.
uint8_t VineGame::hazardHit(float t) const {
    const float footX = toon_.posX - (toon_.facingRight ? hand_[1] : -hand_[1]);
    const float footZ = toon_.posZ - hand_[2];
    auto touches = [&](float hx, float hz) {
        const float dx = toon_.posX - footX, dz = toon_.posZ - footZ, len2 = dx * dx + dz * dz;
        float u = len2 > 0.0f ? ((hx - footX) * dx + (hz - footZ) * dz) / len2 : 0.0f;
        u = std::clamp(u, 0.0f, 1.0f);
        const float ex = footX + u * dx - hx, ez = footZ + u * dz - hz;
        const float r = vine::kHazardRadius + kToonSphereRadius;
        return ex * ex + ez * ez < r * r;
    };
    for (int i = 0; i < vine::kNumVines; ++i) {
        float sx, sz;
        if (vine::spiderPose(i, course_[i], t, &sx, &sz, nullptr) && touches(sx, sz)) return vine::kFallingSpider;
    }
    for (int i = 0; i < numBats_; ++i) {
        float bx, bz;
        if (batPose(i, t, &bx, &bz) && touches(bx, bz)) return vine::kFallingBat;
    }
    return 0;
}

float VineGame::swingFrame() const {
    if (toon_.state != vine::State::OnVine) return vine::kJumpingFrame;
    const int v = std::clamp(toon_.currentVine, 0, vine::kNumVines - 1);
    return vine::swingFrame(course_[v], gameTime_, toon_.vineT, toon_.facingRight);
}

void VineGame::update(net::Client& client, const circlePosition& pad, u32 kDown, float dt, uint32_t nowMs) {
    if (done_) return;
    if (!announced_) {
        // enterPlay: attachLocalToonToVine(0, VineStartingT) broadcasts the start shelf's vine.
        announced_ = true;
        client.sendVineNewVine(client.avatarId(), int8_t(toon_.currentVine), toon_.vineT, 1);
    }

    timeRemaining_ -= dt;
    if (timeRemaining_ <= 0.0f) {
        timeRemaining_ = 0.0f;
        done_ = true;
    }
    gameTime_ += dt;
    const int last = vine::kNumVines - 1;

    if (toon_.state == vine::State::OnVine) {
        const int cur = toon_.currentVine;
        // Climb (upArrowKeyHandler / downArrowKeyHandler): ToonVerticalRate of the cable per second.
        // Pad up shortens the reach towards the pivot; the end vine cannot be climbed.
        if (cur != last) {
            float t = toon_.vineT;
            if (pad.dy > kPadDeadzone) t -= vine::kToonVerticalRate * dt * (pad.dy / kPadFull);
            else if (pad.dy < -kPadDeadzone) t += vine::kToonVerticalRate * dt * (-pad.dy / kPadFull);
            toon_.vineT = std::clamp(t, 0.0f, 1.0f);
        }
        // setNewVineT: the climb direction (-1 towards the pivot, as upstream's climbDir) and the position.
        const int climbDir = cur == last ? 0 : (pad.dy > kPadDeadzone ? -1 : (pad.dy < -kPadDeadzone ? 1 : 0));
        if (climbDir != sentClimbDir_ || (climbDir != 0 && nowMs - lastClimbSendMs_ > kClimbSendMs)) {
            client.sendVineNewVineT(client.avatarId(), toon_.vineT, int8_t(climbDir));
            sentClimbDir_ = climbDir;
            lastClimbSendMs_ = nowMs;
        }
        float angle = vine::swingAngle(course_[cur], gameTime_);
        vine::attachToVine(toon_, cur, course_[cur], angle, toon_.vineT);

        // makeLocalToonFallFromVine: a spider or bat knocks the Toon off (the end vine is safe: the game is won).
        if (cur != last) {
            if (const uint8_t hit = hazardHit(gameTime_)) {
                toon_.state = vine::State::Jumping;
                toon_.lastJumpVine = cur;
                toon_.jumpTime = gameTime_;
                toon_.velX = 0.0f;
                toon_.velZ = -0.1f;
                toon_.falling = hit;
                sentClimbDir_ = 0;
                client.sendVineFallingFromVine(client.avatarId(), int8_t(cur), toon_.facingRight ? 1 : 0, toon_.posX,
                                               toon_.posZ, toon_.velX, toon_.velZ, int8_t(hit));
                return;
            }
        }

        // Jumps: right (or A) leaves towards the next vine, left towards the previous one
        // (rightArrowKeyHandler / leftArrowKeyHandler: no forward jump from the end vine, no back jump from vine 0).
        bool jumpForward = (kDown & (KEY_A | KEY_DRIGHT)) != 0 && cur != last;
        bool jumpBack = (kDown & KEY_DLEFT) != 0 && cur != 0 && cur != last;
        if (autoPlay_ && cur != last && !jumpBack) {
            // Test autoplay: pick the climb t whose jump the physics will carry onto the next vine.
            for (float cand = kAutoStartT; cand <= kAutoEndT + 1e-4f && !jumpForward; cand += kAutoStepT) {
                vine::VineToonState probe = toon_;
                probe.vineT = cand;
                vine::attachToVine(probe, cur, course_[cur], angle, cand);
                if (vine::jumpReaches(probe, course_, gameTime_, cur + 1, true)) {
                    toon_.vineT = cand;
                    vine::attachToVine(toon_, cur, course_[cur], angle, cand);
                    jumpForward = true;
                }
            }
        }
        if (jumpForward || jumpBack) {
            angle = vine::swingAngle(course_[cur], gameTime_);
            vine::jumpFromVine(toon_, angle, jumpForward, gameTime_);
            sentClimbDir_ = 0;
            client.sendVineJumpingFromVine(client.avatarId(), int8_t(cur), toon_.facingRight ? 1 : 0, toon_.posX,
                                           toon_.posZ, toon_.velX, toon_.velZ);
        }
    } else if (toon_.state == vine::State::Jumping) {
        vine::updateJump(toon_, dt);
        if (!toon_.falling) {
            if (const uint8_t hit = hazardHit(gameTime_)) {
                // makeLocalToonFallFromMidair: the velocity drops to (0, -0.1) and the Toon falls.
                toon_.velX = 0.0f;
                toon_.velZ = -0.1f;
                toon_.falling = hit;
                client.sendVineFallingFromMidair(client.avatarId(), toon_.facingRight ? 1 : 0, toon_.posX, toon_.posZ,
                                                 toon_.velX, toon_.velZ, int8_t(hit));
            }
        }

        if (toon_.posZ < 0.0f) {
            // handleLocalToonFellDown: back onto the vine we jumped from, near its top (one vine further
            // back when a spider on that vine knocked us off).
            int back = toon_.lastJumpVine;
            if (toon_.falling == vine::kFallingSpider && back >= 0 && course_[back].spiderPeriod > 0.0f) --back;
            back = std::max(0, back);
            vine::attachToVine(toon_, back, course_[back], vine::swingAngle(course_[back], gameTime_),
                               vine::kVineFellDownT);
            client.sendVineNewVine(client.avatarId(), int8_t(back), toon_.vineT, toon_.facingRight ? 1 : 0);
        } else {
            // Banana pickup between vines (the treasure grab sphere).
            const int between = toon_.lastJumpVine;
            if (between >= 0 && between < last && !bananaTaken_[between]) {
                const float bananaX = between * vine::kVineSpacing + kBananaOffsetX;
                if (std::abs(toon_.posX - bananaX) < kBananaGrabX && std::abs(toon_.posZ - bananaZ_[between]) < kBananaGrabZ) {
                    bananaTaken_[between] = true;
                    bananas_++;
                    client.sendVineClaimTreasure(between);
                    score_ = vine::calculateScore(bananas_, timeRemaining_, false);
                }
            }

            int caughtVine = 0;
            float caughtT = 0.0f;
            if (vine::findCatch(toon_, course_, gameTime_, &caughtVine, &caughtT)) {
                vine::attachToVine(toon_, caughtVine, course_[caughtVine],
                                   vine::swingAngle(course_[caughtVine], gameTime_), caughtT);
                client.sendVineNewVine(client.avatarId(), int8_t(caughtVine), toon_.vineT, toon_.facingRight ? 1 : 0);
                sentClimbDir_ = 0;
                if (caughtVine == last) {
                    toon_.state = vine::State::Finished;
                    client.sendVineReachedEndVine(int8_t(caughtVine));
                    score_ = vine::calculateScore(bananas_, timeRemaining_, true);
                    done_ = true;
                }
            }
        }
    }

    std::snprintf(lines_[0], sizeof(lines_[0]), "Vine: %d / 20", toon_.currentVine + 1);
    std::snprintf(lines_[1], sizeof(lines_[1]), "Bananas: %d  (Time: %d s)", bananas_, int(std::ceil(timeRemaining_)));
    std::snprintf(lines_[2], sizeof(lines_[2]), "Jellybeans: %d", score_);
}

void VineGame::draw(render::Renderer& renderer) {
    C3D_Mtx m;
    // The backdrop is fixed at the origin (upstream gameBoard.setPosHpr(0, 0, 0, ...)).
    if (background_.loaded()) {
        // load: gameBoard.setTransparency(1), setColorScale(1, 1, 1, 0.5) over the default grey clear.
        Mtx_Identity(&m);
        renderer.setBaseTint(1.0f, 1.0f, 1.0f, 0.5f);
        renderer.drawModel(background_, m);
        renderer.setBaseTint(1.0f, 1.0f, 1.0f, 1.0f);
    }
    if (startShelf_.loaded()) {
        Mtx_Identity(&m);
        Mtx_Translate(&m, kStartShelfX, 0.0f, kShelfZ, true);
        Mtx_Scale(&m, 1.0f, 1.0f, kShelfScaleZ);
        renderer.drawModel(startShelf_, m);
    }
    if (endShelf_.loaded()) {
        const float endX = (vine::kNumVines - 1) * vine::kVineSpacing + kEndShelfOffset;
        Mtx_Identity(&m);
        Mtx_Translate(&m, endX, 0.0f, kShelfZ, true);
        Mtx_Scale(&m, 1.0f, 1.0f, kShelfScaleZ);
        renderer.drawModel(endShelf_, m);
    }
    // SwingVine: the static card above each pivot and the swinging cable (its Rope, here
    // kSegments straight pieces along vine::cablePoint), for the vines near the camera.
    if (segmentModel_.loaded() && staticModel_.loaded()) {
        C3D_FVec eye;
        camera(&eye, nullptr);
        for (int i = 0; i < vine::kNumVines; ++i) {
            const float vx = i * vine::kVineSpacing;
            if (std::fabs(vx - eye.x) > 75.0f) continue;
            Mtx_Identity(&m);
            Mtx_Translate(&m, vx, 0.0f, vine::kVineTopZ, true);
            renderer.drawModel(staticModel_, m);
            constexpr int kSegments = 6;
            const float angle = vine::swingAngle(course_[i], gameTime_);
            float ax = vx, az = vine::kVineTopZ;
            for (int k = 1; k <= kSegments; ++k) {
                float bx, bz;
                vine::cablePoint(i, course_[i], angle, float(k) / kSegments, bx, bz);
                const float dx = bx - ax, dz = bz - az, len = std::sqrt(dx * dx + dz * dz);
                Mtx_Identity(&m);
                Mtx_Translate(&m, ax, 0.0f, az, true);
                // The card hangs along -z: turn it about y onto (dx, dz).
                Mtx_RotateY(&m, std::atan2(-dx, -dz), true);
                Mtx_Scale(&m, 1.0f, 1.0f, len);
                renderer.setUvShift(0.0f, 0.5f * float(k - 1));
                renderer.drawModel(segmentModel_, m);
                ax = bx;
                az = bz;
            }
            renderer.setUvShift(0.0f, 0.0f);
        }
    }
    // VineSpider: spider_2 / spider_3 alternate at 2 fps; VineBat: bat__1 / bat2 / bat3 ping-pong at 12 fps.
    {
        render::Model& spider = spiderModel_[int(gameTime_ * 2.0f) & 1];
        static constexpr int kPingPong[4] = {0, 1, 2, 1};
        render::Model& bat = batModel_[kPingPong[int(gameTime_ * 12.0f) & 3]];
        C3D_FVec eye;
        camera(&eye, nullptr);
        if (spider.loaded()) {
            for (int i = 0; i < vine::kNumVines; ++i) {
                float sx, sz, roll;
                if (!vine::spiderPose(i, course_[i], gameTime_, &sx, &sz, &roll) || std::fabs(sx - eye.x) > 75.0f) continue;
                Mtx_Identity(&m);
                Mtx_Translate(&m, sx, kSpiderY, sz, true);
                Mtx_RotateY(&m, roll * vine::kPi / 180.0f, true);
                Mtx_Scale(&m, kSpiderScale, kSpiderScale, kSpiderScale);
                renderer.drawModel(spider, m);
            }
        }
        if (bat.loaded()) {
            for (int i = 0; i < numBats_; ++i) {
                float bx, bz;
                if (!batPose(i, gameTime_, &bx, &bz) || std::fabs(bx - eye.x) > 75.0f) continue;
                Mtx_Identity(&m);
                Mtx_Translate(&m, bx, 0.0f, bz, true);
                Mtx_Scale(&m, kBatScale, kBatScale, kBatScale);
                renderer.drawModel(bat, m);
            }
        }
    }
    if (bananaModel_.loaded()) {
        for (int i = 0; i < vine::kNumVines - 1; ++i) {
            if (bananaTaken_[i]) continue;
            Mtx_Identity(&m);
            Mtx_Translate(&m, i * vine::kVineSpacing + kBananaOffsetX, 0.0f, bananaZ_[i], true);
            Mtx_Scale(&m, kBananaScale, kBananaScale, kBananaScale);
            renderer.drawModel(bananaModel_, m);
        }
    }
}

void VineGame::toonPose(float* x, float* y, float* z, float* h) const {
    // The Toon hangs by its left hold joint: the attach / physics node sits on the cable point and the
    // Toon at -offset, turned H -90 (facing right) or 90: Toon-local (x, y) maps to world (y, -x) / (-y, x).
    const float sign = toon_.facingRight ? 1.0f : -1.0f;
    if (x) *x = toon_.posX - sign * hand_[1];
    if (y) *y = sign * hand_[0];
    if (z) *z = toon_.posZ - hand_[2];
    if (h) *h = 90.0f; // facing right along the jungle track
}

bool VineGame::remotePose(const net::Minigame::VineRemote& v, uint32_t nowMs, float* x, float* z, float* h) const {
    if (v.mode == net::Minigame::VineRemote::Mode::None) return false;
    vine::ReportedToon r;
    r.attached = v.mode == net::Minigame::VineRemote::Mode::Attached;
    r.vineIndex = v.vineIndex;
    r.vineT = v.vineT;
    r.climbDir = v.climbDir;
    r.posX = v.posX;
    r.posZ = v.posZ;
    r.velX = v.velX;
    r.velZ = v.velZ;
    const float elapsed = static_cast<int32_t>(nowMs - v.arrivalMs) / 1000.0f;
    float px = 0.0f, pz = 0.0f;
    if (!vine::reportedPosition(r, course_, gameTime_, elapsed, px, pz)) return false;
    // Hung by its hold joint like the local Toon; a medium Toon's offset (3.92) stands in for its own.
    if (x) *x = px;
    if (z) *z = pz - 3.92f;
    // The toon faces right along the track; a jump or fall heads with its velocity (makeOtherToonJump).
    if (h) *h = r.attached ? 90.0f : (v.velX >= 0.0f ? -90.0f : 90.0f);
    return true;
}

// TwoVineView: the camera at (x, -53, 17.3), level. On a vine, getFocusCameraPos centres it
// between that vine and the next, cameraTwoVineAdj (5) back toward the held one (the last vine:
// past it, 5 forward); in the air makeCameraFollowJumpingToon follows the Toon between the
// vines either side of the last one jumped from.
void VineGame::camera(C3D_FVec* eye, C3D_FVec* target) const {
    const int last = vine::kNumVines - 1;
    const int v = std::clamp(toon_.currentVine, 0, last);
    float ex;
    if (toon_.state == vine::State::Jumping)
        ex = std::clamp(toon_.posX, std::max(v - 1, 0) * vine::kVineSpacing, std::min(v + 1, last) * vine::kVineSpacing);
    else
        ex = v * vine::kVineSpacing + (v == last ? 20.0f : 10.0f);
    if (eye) *eye = FVec3_New(ex, kCamY, kCamZ);
    if (target) *target = FVec3_New(ex, kCamY + kCamLookDistance, kCamZ);
}

} // namespace tt::game
