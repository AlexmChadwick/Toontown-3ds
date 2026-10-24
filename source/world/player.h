// Local toon controller + third-person follow camera, with collision (GravityWalker-style).
#pragma once

#include <3ds.h>
#include <citro3d.h>

#include "world/collision.h"
#include "world/toon_avatar.h"

namespace tt::world {

struct Input {
    circlePosition pad{};    // circle pad, roughly [-156, 156]
    circlePosition cstick{}; // New 3DS C-stick; always zero on Old 3DS
    u32 held = 0;
    u32 down = 0;
};

class Player {
public:
    // Optional: without collision the toon walks on a flat z=0 plane.
    void setCollision(const CollisionWorld* world) { collision_ = world; }
    void reset(float x, float y, float z, float headingDeg);
    void update(const Input& in, float dt);

    float x() const { return x_; }
    float y() const { return y_; }
    float z() const { return z_; }
    float headingDeg() const { return heading_; }
    bool moving() const { return speed_ != 0.0f; }
    float groundZ() const { return ground_; }
    bool airborne() const { return z_ > ground_ + 0.01f || vz_ > 0.0f; }
    // Clip + play rate for the current motion (Toon.setSpeed rules: run >= 8, walk > 0.5,
    // reverse = walk at -1, turning in place = walk; airborne = jump-idle).
    ToonAnim animState(float* rate) const;

    // World matrix for the toon model (Panda convention: toon faces +Y at H=0).
    void worldMatrix(C3D_Mtx* out) const;

    C3D_FVec cameraEye() const { return camEye_; }
    C3D_FVec cameraTarget() const { return camTarget_; }
    // Head-to-eye distance after wall avoidance; very small means the camera is inside the toon.
    float cameraDistance() const;

private:
    float x_ = 0, y_ = 0, z_ = 0;
    float heading_ = 0; // degrees, CCW from +Y
    float speed_ = 0;
    float turnRate_ = 0; // deg/s this frame
    float ground_ = 0;   // ground height under the toon (flat until collision lands)
    float vz_ = 0;
    float camYawOffset_ = 0; // degrees relative to toon heading
    float camPitch_ = 14;    // degrees above horizontal
    C3D_FVec camEye_{};
    C3D_FVec camTarget_{};
    const CollisionWorld* collision_ = nullptr;
};

} // namespace tt::world
