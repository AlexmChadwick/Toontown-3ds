// Local toon controller + third-person follow camera, with collision (GravityWalker-style).
#pragma once

#include <3ds.h>
#include <citro3d.h>

#include "world/collision.h"
#include "world/toon_avatar.h"

namespace tt::world {

// Moving and interactive solids on top of the static world (facility doors, buttons, lifts,
// platforms: world/level.*). All callbacks are optional; positions are toon feet in the world.
struct DynamicSolids {
    // Highest solid top under (x, y) that the feet at z can stand on (step-up reach kFloorReach).
    bool (*floor)(void* user, float x, float y, float z, float* top) = nullptr;
    // Moves the toon's (x, y) out of solids taller than a step at feet height z.
    void (*push)(void* user, float* x, float* y, float z, float radius) = nullptr;
    // This frame's motion of the solid the feet stand on (a riding lift/platform).
    bool (*carry)(void* user, float x, float y, float z, float* dx, float* dy, float* dz) = nullptr;
    void* user = nullptr;
};

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
    void setHeading(float headingDeg) { heading_ = headingDeg; }
    // Moving / interactive solids (world/level.*); nullptr when there are none.
    void setDynamic(const DynamicSolids* solids) { dynamic_ = solids; }
    // Replaces this frame's follow camera (call after update; e.g. fishing's pond view).
    void overrideCamera(const C3D_FVec& eye, const C3D_FVec& target) {
        camEye_ = eye;
        camTarget_ = target;
        camDist_ = cameraDistance();
    }
    // Same as overrideCamera, then snap the lens in along head -> eye if a camera
    // solid is in the way. The look-at stays `target`.
    void overrideCameraCollide(const C3D_FVec& eye, const C3D_FVec& target);
    // Moving platform (Donald's Dock boat). `deck` is the platform's collision in its own frame;
    // (x, y, z, headingDeg) is the platform's current pose in the world (boat-local -> world is
    // translate * rotateZ). Pass deck = nullptr when there is none. Call every frame before update().
    // The toon boards when it walks/falls onto a deck floor and leaves when it walks off; while
    // aboard it is simulated in platform space (cheaper than moving the collision mesh: one
    // inverse transform per boarding, one forward transform per frame), so it is carried by the
    // platform's translation and rotation exactly, like a node parented to it.
    void setPlatform(const CollisionWorld* deck, float x, float y, float z, float headingDeg);
    void update(const Input& in, float dt);

    float x() const { return x_; }
    float y() const { return y_; }
    float z() const { return z_; }
    // The horizontal position the toon tried to reach this frame, before the wall slide. Trigger
    // tests (doors, tunnels) use it: GravityWalker's event sphere is traversed on the moved, not yet
    // pushed-back, position, so a toon pressed against a flush door still touches its trigger.
    float probeX() const { return probeX_; }
    float probeY() const { return probeY_; }
    float headingDeg() const { return heading_; }
    bool moving() const { return speed_ != 0.0f; }
    float groundZ() const { return ground_; }
    bool onPlatform() const { return onPlat_; }
    // Pose to send over the network: platform-local while aboard (setParent token), else world.
    void netPose(float* x, float* y, float* z, float* headingDeg) const;
    bool airborne() const { return z_ > ground_ + 0.01f || vz_ > 0.0f; }
    // Clip + play rate for the current motion (Toon.setSpeed: run >= 8, walk > 0.5,
    // reverse = walk at -1, turning in place = walk; airborne = jump-idle). B uses the
    // OTP slow speeds, so a full-stick walk stays under RunCutOff and plays walk at rate 1.
    ToonAnim animState(float* rate) const;

    // World matrix for the toon model (Panda convention: toon faces +Y at H=0).
    void worldMatrix(C3D_Mtx* out) const;

    C3D_FVec cameraEye() const { return camEye_; }
    C3D_FVec cameraTarget() const { return camTarget_; }
    // Head-to-eye distance after wall avoidance; very small means the camera is inside the toon.
    float cameraDistance() const;

private:
    // Pull the boom in so a sphere around the lens misses camera solids, then ease it
    // back out. dt <= 0 snaps (teleport, talk camera) so the lens never rests in a wall.
    void avoidCamera(const C3D_FVec& head, float dt);
    void locomote(const Input& in, float dt, const CollisionWorld* world);
    void camera(const Input& in, float dt);
    bool tryBoard();
    void toWorld(); // platform-local x_.. -> world, keeps the local copy in l*_
    void leavePlatform();

    float x_ = 0, y_ = 0, z_ = 0;
    float probeX_ = 0, probeY_ = 0; // see probeX()
    float heading_ = 0; // degrees, CCW from +Y
    float speed_ = 0;
    float turnRate_ = 0; // deg/s this frame
    float ground_ = 0;   // ground height under the toon (flat until collision lands)
    float vz_ = 0;
    float camYawOffset_ = 0; // degrees relative to toon heading
    float camPitch_ = 14;    // degrees above horizontal
    float camDist_ = 12.0f;  // smoothed boom length (kCamDistance until the first avoid)
    C3D_FVec camEye_{};
    C3D_FVec camTarget_{};
    const CollisionWorld* collision_ = nullptr;
    const DynamicSolids* dynamic_ = nullptr;

    // Platform state. While onPlat_, l*_ is the platform-local truth and x_.. the derived world pose.
    const CollisionWorld* deck_ = nullptr;
    float px_ = 0, py_ = 0, pz_ = 0, ph_ = 0; // platform pose (heading in degrees)
    bool onPlat_ = false;
    float lx_ = 0, ly_ = 0, lz_ = 0, lh_ = 0, lground_ = 0;
};

} // namespace tt::world
