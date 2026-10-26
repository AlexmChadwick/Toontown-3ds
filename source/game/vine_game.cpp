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
// DistributedVineGame.load: the treasures are placed at vine x + 15 with a random height in
// randrange(10, 25) (per-client RNG upstream); the midpoint stands in for it here.
constexpr float kBananaOffsetX = 15.0f;
constexpr float kBananaZ = 17.5f;
constexpr float kBananaScale = 0.25f; // VineTreasure: nodePath.setScale(0.25)
// DistributedVineGame.makeCameraFollowJumpingToon: the side camera at (x, -63.2, 16.3), looking along +y.
constexpr float kCamY = -63.2f;
constexpr float kCamZ = 16.3f;
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

    std::snprintf(lines_[0], sizeof(lines_[0]), "Vine: 1 / 20");
    std::snprintf(lines_[1], sizeof(lines_[1]), "Bananas: 0  (Time: 70 s)");
    std::snprintf(lines_[2], sizeof(lines_[2]), "Jellybeans: 1");
}

void VineGame::shutdown() {
    assetBatch_.clear();
    background_.unload();
    bananaModel_.unload();
    startShelf_.unload();
    endShelf_.unload();
    done_ = true;
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

        if (toon_.posZ < 0.0f) {
            // handleLocalToonFellDown: back onto the vine we jumped from, near its top.
            const int back = std::max(0, toon_.lastJumpVine);
            vine::attachToVine(toon_, back, course_[back], vine::swingAngle(course_[back], gameTime_),
                               vine::kVineFellDownT);
            client.sendVineNewVine(client.avatarId(), int8_t(back), toon_.vineT, toon_.facingRight ? 1 : 0);
        } else {
            // Banana pickup between vines (the treasure grab sphere).
            const int between = toon_.lastJumpVine;
            if (between >= 0 && between < last && !bananaTaken_[between]) {
                const float bananaX = between * vine::kVineSpacing + kBananaOffsetX;
                if (std::abs(toon_.posX - bananaX) < kBananaGrabX && std::abs(toon_.posZ - kBananaZ) < kBananaGrabZ) {
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
        Mtx_Identity(&m);
        renderer.drawModel(background_, m);
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
    if (bananaModel_.loaded()) {
        for (int i = 0; i < vine::kNumVines - 1; ++i) {
            if (bananaTaken_[i]) continue;
            Mtx_Identity(&m);
            Mtx_Translate(&m, i * vine::kVineSpacing + kBananaOffsetX, 0.0f, kBananaZ, true);
            Mtx_Scale(&m, kBananaScale, kBananaScale, kBananaScale);
            renderer.drawModel(bananaModel_, m);
        }
    }
}

void VineGame::toonPose(float* x, float* y, float* z, float* h) const {
    if (x) *x = toon_.posX;
    if (y) *y = 0.0f;
    if (z) *z = toon_.posZ;
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
    if (x) *x = px;
    if (z) *z = pz;
    // The toon faces right along the track; a jump or fall heads with its velocity (makeOtherToonJump).
    if (h) *h = r.attached ? 90.0f : (v.velX >= 0.0f ? -90.0f : 90.0f);
    return true;
}

void VineGame::camera(C3D_FVec* eye, C3D_FVec* target) const {
    // The side view follows the toon along the course, between the start and the next vine.
    const float maxX = (std::min(toon_.currentVine + 1, vine::kNumVines - 1)) * vine::kVineSpacing;
    const float ex = std::clamp(toon_.posX, 0.0f, maxX);
    if (eye) *eye = FVec3_New(ex, kCamY, kCamZ);
    if (target) *target = FVec3_New(ex, kCamY + kCamLookDistance, kCamZ);
}

} // namespace tt::game
