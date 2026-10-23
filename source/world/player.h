// Local toon controller + third-person follow camera (Phase 1: flat ground, no collision).
#pragma once

#include <3ds.h>
#include <citro3d.h>

namespace tt::world {

struct Input {
    circlePosition pad{};    // circle pad, roughly [-156, 156]
    circlePosition cstick{}; // New 3DS C-stick; always zero on Old 3DS
    u32 held = 0;
    u32 down = 0;
};

class Player {
public:
    void reset(float x, float y, float z, float headingDeg);
    void update(const Input& in, float dt);

    float x() const { return x_; }
    float y() const { return y_; }
    float z() const { return z_; }
    float headingDeg() const { return heading_; }
    bool moving() const { return speed_ != 0.0f; }

    // World matrix for the toon model (Panda convention: toon faces +Y at H=0).
    void worldMatrix(C3D_Mtx* out) const;

    C3D_FVec cameraEye() const { return camEye_; }
    C3D_FVec cameraTarget() const { return camTarget_; }

private:
    float x_ = 0, y_ = 0, z_ = 0;
    float heading_ = 0; // degrees, CCW from +Y
    float speed_ = 0;
    float vz_ = 0;
    float camYawOffset_ = 0; // degrees relative to toon heading
    float camPitch_ = 14;    // degrees above horizontal
    C3D_FVec camEye_{};
    C3D_FVec camTarget_{};
};

} // namespace tt::world
