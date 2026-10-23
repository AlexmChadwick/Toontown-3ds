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
constexpr float kGravity = 64.0f;

constexpr float kPadDeadzone = 24.0f;
constexpr float kPadMax = 150.0f;
constexpr float kCamDistance = 12.0f;
constexpr float kCamLookHeight = 3.0f;
constexpr float kCamOrbitSpeed = 120.0f; // deg/s with L/R
constexpr float kCamRecenterRate = 3.0f; // 1/s, exponential

constexpr float kDegToRad = 3.14159265f / 180.0f;

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
    camYawOffset_ = 0;
    update(Input{}, 0.0f);
}

void Player::update(const Input& in, float dt) {
    const float fwd = axis(in.pad.dy);
    const float turn = axis(in.pad.dx);

    heading_ -= turn * kRotateSpeed * dt;
    if (heading_ > 180.0f) heading_ -= 360.0f;
    if (heading_ < -180.0f) heading_ += 360.0f;

    float maxSpeed = fwd >= 0 ? kForwardSpeed : kReverseSpeed;
    if (in.held & KEY_B) maxSpeed *= kWalkFraction;
    speed_ = fwd * maxSpeed;

    const float h = heading_ * kDegToRad;
    // Panda heading: H=0 faces +Y, positive H turns left (CCW seen from above).
    x_ += -sinf(h) * speed_ * dt;
    y_ += cosf(h) * speed_ * dt;

    // Jump on flat ground (Phase 2 replaces z=0 with collision-ground height).
    if ((in.down & KEY_A) && z_ <= 0.0f) vz_ = kJumpVelocity;
    vz_ -= kGravity * dt;
    z_ += vz_ * dt;
    if (z_ < 0.0f) {
        z_ = 0.0f;
        vz_ = 0.0f;
    }

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
}

void Player::worldMatrix(C3D_Mtx* out) const {
    Mtx_Identity(out);
    Mtx_Translate(out, x_, y_, z_, true);
    Mtx_RotateZ(out, heading_ * kDegToRad, true);
}

} // namespace tt::world
