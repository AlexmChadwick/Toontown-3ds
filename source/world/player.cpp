#include "world/player.h"

#include <cmath>

namespace tt::world {

namespace {

// otp/otpbase/OTPGlobals.py: ToonForwardSpeed 16, ToonReverseSpeed 8, ToonRotateSpeed 80,
// all multiplied by ToonSpeedFactor 1.25.
constexpr float kForwardSpeed = 16.0f * 1.25f;
constexpr float kReverseSpeed = 8.0f * 1.25f;
constexpr float kRotateSpeed = 80.0f * 1.25f;
constexpr float kWalkFraction = 0.4f; // B held: walk
constexpr float kJumpVelocity = 24.0f;
constexpr float kStepDown = 1.5f; // largest drop followed without falling (stairs, kerbs)
constexpr float kGravity = 64.348f; // direct/controls/GravityWalker.py

constexpr float kPadDeadzone = 24.0f;
constexpr float kPadMax = 150.0f;
constexpr float kCamDistance = 12.0f;
constexpr float kCamLookHeight = 3.0f;
constexpr float kCamOrbitSpeed = 120.0f; // deg/s with L/R
constexpr float kCamRecenterRate = 3.0f; // 1/s, exponential
constexpr float kCamWallMargin = 0.05f;  // fraction of the head->eye segment kept off walls

constexpr float kDegToRad = 3.14159265f / 180.0f;
// otp/otpbase/OTPGlobals.py WalkCutOff / RunCutOff.
constexpr float kWalkCutOff = 0.5f;
constexpr float kRunCutOff = 8.0f;

float axis(s16 v) {
    float f = static_cast<float>(v);
    if (fabsf(f) < kPadDeadzone) return 0.0f;
    f = (f > 0 ? f - kPadDeadzone : f + kPadDeadzone) / (kPadMax - kPadDeadzone);
    return f > 1.0f ? 1.0f : (f < -1.0f ? -1.0f : f);
}

} // namespace

void Player::reset(float x, float y, float z, float headingDeg) {
    x_ = x;
    y_ = y;
    z_ = z;
    heading_ = headingDeg;
    speed_ = vz_ = 0;
    onPlat_ = false;
    camYawOffset_ = 0;
    update(Input{}, 0.0f);
}

void Player::setPlatform(const CollisionWorld* deck, float x, float y, float z, float headingDeg) {
    deck_ = deck && deck->loaded() ? deck : nullptr;
    px_ = x, py_ = y, pz_ = z, ph_ = headingDeg;
}

void Player::netPose(float* x, float* y, float* z, float* headingDeg) const {
    *x = onPlat_ ? lx_ : x_;
    *y = onPlat_ ? ly_ : y_;
    *z = onPlat_ ? lz_ : z_;
    *headingDeg = onPlat_ ? lh_ : heading_;
}

namespace {
float wrap180(float h) {
    while (h > 180.0f) h -= 360.0f;
    while (h < -180.0f) h += 360.0f;
    return h;
}
constexpr float kBoardStep = 1.0f;   // a deck floor at most this far above the feet boards
constexpr float kBoardDrop = 5.0f;   // ... and at most this far below (jump apex is ~4.5)
} // namespace

bool Player::tryBoard() {
    const float r = ph_ * kDegToRad, c = cosf(r), s = sinf(r);
    const float dx = x_ - px_, dy = y_ - py_;
    const float x = dx * c + dy * s, y = -dx * s + dy * c, z = z_ - pz_;
    bool found = false;
    const float g = deck_->groundHeight(x, y, z, &found, kBoardStep);
    if (!found || z - g > kBoardDrop) return false;
    lx_ = x, ly_ = y, lz_ = z;
    lh_ = wrap180(heading_ - ph_);
    lground_ = g;
    onPlat_ = true;
    return true;
}

void Player::toWorld() {
    const float r = ph_ * kDegToRad, c = cosf(r), s = sinf(r);
    x_ = px_ + lx_ * c - ly_ * s;
    y_ = py_ + lx_ * s + ly_ * c;
    z_ = pz_ + lz_;
    heading_ = wrap180(lh_ + ph_);
    ground_ = lground_ + pz_;
}

void Player::leavePlatform() {
    onPlat_ = false; // x_.. already hold the world pose from the last toWorld()
}

void Player::update(const Input& in, float dt) {
    if (!deck_) {
        if (onPlat_) leavePlatform();
    } else if (!onPlat_) {
        tryBoard();
    }
    if (onPlat_) {
        x_ = lx_, y_ = ly_, z_ = lz_, heading_ = lh_, ground_ = lground_;
        locomote(in, dt, deck_);
        bool found = false;
        deck_->groundHeight(x_, y_, z_, &found);
        lx_ = x_, ly_ = y_, lz_ = z_, lh_ = heading_, lground_ = ground_;
        toWorld();
        if (!found) leavePlatform(); // walked off the edge: the zone's collision takes over
    } else {
        locomote(in, dt, collision_);
    }
    camera(in, dt);
}

void Player::locomote(const Input& in, float dt, const CollisionWorld* world) {
    const float fwd = axis(in.pad.dy);
    const float turn = axis(in.pad.dx);

    turnRate_ = -turn * kRotateSpeed;
    heading_ += turnRate_ * dt;
    if (heading_ > 180.0f) heading_ -= 360.0f;
    if (heading_ < -180.0f) heading_ += 360.0f;

    float maxSpeed = fwd >= 0 ? kForwardSpeed : kReverseSpeed;
    if (in.held & KEY_B) maxSpeed *= kWalkFraction;
    speed_ = fwd * maxSpeed;

    const float h = heading_ * kDegToRad;
    // Panda heading: H=0 faces +Y, positive H turns left (CCW seen from above).
    const float dx = -sinf(h) * speed_ * dt, dy = cosf(h) * speed_ * dt;
    const bool grounded = !airborne();
    const bool jumping = (in.down & KEY_A) && grounded;
    if (jumping) vz_ = kJumpVelocity;
    vz_ -= kGravity * dt;

    const DynamicSolids* dyn = deck_ ? nullptr : dynamic_; // (never while aboard the boat)
    if (dyn && dyn->carry && grounded) { // ride the lift / platform we stand on
        float cx = 0, cy = 0, cz = 0;
        if (dyn->carry(dyn->user, x_, y_, z_, &cx, &cy, &cz)) x_ += cx, y_ += cy, z_ += cz, ground_ += cz;
    }
    if (world && world->loaded()) {
        // GravityWalker order: slide along walls, find the floor (step-up reach 4), apply
        // the jump arc, then slide again at the new height so ledges can't trap the toon.
        const float r = kToonRadius;
        Vec3 p{x_, y_, z_};
        p = world->slideCapsule(p, r, 2.0f * r, Vec3{dx, dy, 0.0f});
        if (dyn && dyn->push) {
            // Out of moving solids, but never through the static walls: slide the push too.
            float px = p.x, py = p.y;
            dyn->push(dyn->user, &px, &py, p.z, r);
            if (px != p.x || py != p.y) p = world->slideCapsule(p, r, 2.0f * r, Vec3{px - p.x, py - p.y, 0.0f});
        }
        bool found = false;
        float g = world->groundHeight(p.x, p.y, p.z, &found);
        float top;
        bool onSolid = false; // standing on a lift/platform: CollisionHandlerFloor sets Z directly
        if (dyn && dyn->floor && dyn->floor(dyn->user, p.x, p.y, p.z, &top) && (!found || top > g)) {
            g = top;
            found = true;
            onSolid = true;
        }
        if (found) ground_ = g;
        float nz = z_ + vz_ * dt;
        // Walking down stairs or a kerb: stay on the floor instead of starting a fall
        // (which would play the jump/fall clip on every step).
        if (grounded && !jumping && nz > ground_ && nz - ground_ <= kStepDown) nz = ground_;
        if (nz <= ground_) {
            nz = ground_;
            vz_ = 0.0f;
        }
        if (onSolid && nz == ground_) p.z = nz; // carried up a shaft: no static ceiling stops a lift
        else if (nz != p.z) p = world->slideCapsule(p, r, 2.0f * r, Vec3{0.0f, 0.0f, nz - p.z});
        x_ = p.x;
        y_ = p.y;
        z_ = p.z;
    } else {
        x_ += dx;
        y_ += dy;
        z_ += vz_ * dt;
        ground_ = 0.0f;
        if (z_ < ground_) {
            z_ = ground_;
            vz_ = 0.0f;
        }
    }

}

void Player::camera(const Input& in, float dt) {
    // Camera: L/R orbit; C-stick (New 3DS only) orbits/pitches; otherwise drift back
    // behind the toon while it moves.
    float orbit = 0.0f;
    if (in.held & KEY_L) orbit += kCamOrbitSpeed;
    if (in.held & KEY_R) orbit -= kCamOrbitSpeed;
    orbit -= axis(in.cstick.dx) * kCamOrbitSpeed;
    camYawOffset_ += orbit * dt;
    camPitch_ += axis(in.cstick.dy) * 60.0f * dt;
    camPitch_ = camPitch_ < -5.0f ? -5.0f : (camPitch_ > 60.0f ? 60.0f : camPitch_);
    if (orbit == 0.0f && speed_ != 0.0f) camYawOffset_ -= camYawOffset_ * (1.0f - expf(-kCamRecenterRate * dt));

    const float yaw = (heading_ + camYawOffset_) * kDegToRad;
    const float pitch = camPitch_ * kDegToRad;
    camTarget_ = FVec3_New(x_, y_, z_ + kCamLookHeight);
    camEye_ = FVec3_New(x_ + sinf(yaw) * cosf(pitch) * kCamDistance, y_ - cosf(yaw) * cosf(pitch) * kCamDistance,
                        z_ + kCamLookHeight + sinf(pitch) * kCamDistance);
    avoidCamera(camTarget_);
    if (onPlat_) {
        // The boat's walls and rails: the same segment in platform space (a rigid transform
        // keeps the hit fraction), against the head -> (possibly already shortened) eye.
        const float r = ph_ * kDegToRad, c = cosf(r), s = sinf(r);
        auto local = [&](float wx, float wy, float wz) {
            const float dx = wx - px_, dy = wy - py_;
            return Vec3{dx * c + dy * s, -dx * s + dy * c, wz - pz_};
        };
        const Vec3 head = local(camTarget_.x, camTarget_.y, camTarget_.z);
        const Vec3 eye = local(camEye_.x, camEye_.y, camEye_.z);
        float t = 1.0f;
        if (deck_->cameraRaycast(head, eye, &t)) {
            t = t > kCamWallMargin ? t - kCamWallMargin : 0.0f;
            camEye_ = FVec3_New(camTarget_.x + (camEye_.x - camTarget_.x) * t,
                                camTarget_.y + (camEye_.y - camTarget_.y) * t,
                                camTarget_.z + (camEye_.z - camTarget_.z) * t);
        }
    }
}

void Player::avoidCamera(const C3D_FVec& headPosition) {
    if (collision_ && collision_->loaded()) {
        // Pull the camera in front of walls between it and the toon's head.
        const Vec3 head{headPosition.x, headPosition.y, headPosition.z};
        const Vec3 eye{camEye_.x, camEye_.y, camEye_.z};
        float t = 1.0f;
        if (collision_->cameraRaycast(head, eye, &t)) {
            t = t > kCamWallMargin ? t - kCamWallMargin : 0.0f;
            camEye_ = FVec3_New(head.x + (eye.x - head.x) * t, head.y + (eye.y - head.y) * t,
                                head.z + (eye.z - head.z) * t);
        }
    }
}

void Player::overrideCameraCollide(const C3D_FVec& eye, const C3D_FVec& target) {
    overrideCamera(eye, target);
    // NPC talk uses the same head-to-eye collision pull-in as the follow camera.
    avoidCamera(FVec3_New(x_, y_, z_ + kCamLookHeight));
}

float Player::cameraDistance() const {
    const float dx = camEye_.x - camTarget_.x, dy = camEye_.y - camTarget_.y, dz = camEye_.z - camTarget_.z;
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

ToonAnim Player::animState(float* rate) const {
    *rate = 1.0f;
    if (airborne()) return kAnimJumpIdle;
    if (speed_ >= kRunCutOff) return kAnimRun;
    if (speed_ > kWalkCutOff) return kAnimWalk;
    if (speed_ < -kWalkCutOff) {
        *rate = -1.0f;
        return kAnimWalk;
    }
    if (turnRate_ != 0.0f) return kAnimWalk;
    return kAnimNeutral;
}

void Player::worldMatrix(C3D_Mtx* out) const {
    Mtx_Identity(out);
    Mtx_Translate(out, x_, y_, z_, true);
    Mtx_RotateZ(out, heading_ * kDegToRad, true);
}

} // namespace tt::world
