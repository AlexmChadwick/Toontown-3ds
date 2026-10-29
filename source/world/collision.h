// Runtime side of the TTC collision format (writer + spec: tools/pipeline/collide.py).
//
// The whole file is read into one regular-heap block (no linear memory) and queried in
// place. Queries never allocate. Kept free of libctru so tools/collision_test can build
// it with the host compiler.
#pragma once

#include <cstdint>
#include <cstdio>

namespace tt::world {

struct Vec3 {
    float x, y, z;
};

// Upstream constants (see the citations in collision.cpp).
constexpr float kToonRadius = 1.4f;   // LocalAvatar.setupControls avatarRadius
constexpr float kFloorReach = 4.0f;   // LocalAvatar.setupControls reach (lifter step-up)
constexpr float kFloorOffset = 0.025f; // OTPGlobals.FloorOffset (lifter offset)

enum CollideFlag : uint32_t {
    kCollideWall = 1,     // OTPGlobals.WallBitmask
    kCollideFloor = 2,    // OTPGlobals.FloorBitmask
    kCollideCamera = 4,   // OTPGlobals.CameraBitmask
    kCollideTrigger = 0x80,
    kCollideFallback = 0x100, // floor taken from visible geometry
};

struct CollTri {
    float v0[3], v1[3], v2[3];
    float n[3];
    float d; // n . v0
    uint32_t flags;
};
static_assert(sizeof(CollTri) == 56, "must match collide.py TRI_SIZE");

struct CollCap { // capsule a-b of radius r; a sphere when a == b
    float a[3], b[3];
    float r;
    uint32_t flags;
};
static_assert(sizeof(CollCap) == 32, "must match collide.py CAP_SIZE");

struct CollPlane { // n . p = d; solid side is n . p < d
    float n[3];
    float d;
    uint32_t flags;
};
static_assert(sizeof(CollPlane) == 20, "must match collide.py PLANE_SIZE");

struct CollTrigger {
    uint8_t kind; // 0: capsules [first, first + count), 1: triangles
    uint8_t pad;
    uint16_t count;
    uint32_t first;
    uint32_t intoMask; // original Panda into-collide-mask
    float min[3], max[3];
    char name[32];  // CollisionNode name, e.g. "door_double_round_ur_trigger"
    char owner[32]; // DNA owner node, e.g. "tb2:toon_landmark_TT_A1_DNARoot"
};
static_assert(sizeof(CollTrigger) == 100, "must match collide.py TRIGGER_SIZE");

class CollisionWorld {
public:
    CollisionWorld() = default;
    ~CollisionWorld();
    CollisionWorld(const CollisionWorld&) = delete;
    CollisionWorld& operator=(const CollisionWorld&) = delete;

    // Loads "romfs:/baked/x.ttc" / "sdmc:/...". Returns false (and logs why) on a missing
    // file, bad magic/version or inconsistent sections; the world is then empty and every
    // query reports "nothing hit".
    bool load(const char* path);
    // A TTC1 blob embedded at `base` of an open file (e.g. inside a .tz). File stays open.
    bool loadFrom(FILE* f, uint32_t base, uint32_t size, const char* label = nullptr);
    // Copies and validates an already-prefetched TTC1 blob. This avoids reopening
    // an enclosing container on the main thread.
    bool loadMemory(const void* data, uint32_t size, const char* label = nullptr);
    void unload();
    bool loaded() const { return blob_ != nullptr; }

    // Highest floor surface under (x, y) whose height is <= zFrom + stepHeight. With
    // zFrom = the toon's feet and the default step this matches the upstream lifter
    // (CollisionHandlerGravity with reach 4.0). *found = false (and the return value is
    // zFrom) when there is no floor; found may be null.
    float groundHeight(float x, float y, float zFrom, bool* found,
                       float stepHeight = kFloorReach) const;

    // Moves a vertical capsule (feet at pos, radius, total height >= 2 * radius) by
    // desiredDelta and returns the resolved feet position. Walls push only horizontally;
    // delta.z is applied unchanged (the caller owns vertical motion via groundHeight).
    // The move is split into sub-steps of at most radius / 2, so no wall is skipped at
    // any speed. A sub-step that cannot be resolved (acute concave corner) is undone,
    // which keeps the toon still instead of jittering.
    Vec3 slideCapsule(const Vec3& pos, float radius, float height, const Vec3& desiredDelta) const;

    // Segment from -> to against camera-mask solids. On a hit returns true with *hitT in
    // [0, 1] (fraction along the segment of the first hit).
    bool cameraRaycast(const Vec3& from, const Vec3& to, float* hitT) const;

    // Trigger volumes overlapped by a sphere (centre p, radius). Writes up to maxOut
    // trigger indices, returns how many overlap in total.
    int triggersAt(const Vec3& p, float radius, int* out, int maxOut) const;
    int numTriggers() const { return static_cast<int>(numTriggers_); }
    const CollTrigger& trigger(int i) const { return triggers_[i]; }

    uint32_t numTris() const { return numTris_; }
    uint32_t numCaps() const { return numCaps_; }
    uint32_t heapBytes() const { return heapBytes_; }
    const CollTri* tris() const { return tris_; }
    const CollCap* caps() const { return caps_; }

private:
    bool adoptBlob(uint8_t* blob, uint32_t size, const char* label);
    struct CellLists { // pointers into cell data for one cell
        const uint16_t* counts; // floorT, floorC, wallT, wallC, camT, camC
        const uint16_t* idx;
    };
    bool cellCoords(float x, float y, int* cx, int* cy) const;
    CellLists cell(int cx, int cy) const;
    // Gathers wall solids whose bounds touch the XY box and z range into the scratch
    // lists (deduplicated with stamps). Returns false if the scratch overflowed.
    bool gatherWalls(float x0, float y0, float x1, float y1, float z0, float z1) const;
    uint32_t nextStamp() const;
    // One Gauss-Seidel pass of horizontal wall pushes over the gathered solids for the
    // spheres at pos + offs[k]. apply = false only tests for any contact.
    bool pushPass(Vec3* pos, float radius, const float* offs, int nSph, bool apply) const;

    uint8_t* blob_ = nullptr;
    uint32_t heapBytes_ = 0;
    float originX_ = 0, originY_ = 0, cellSize_ = 8, invCell_ = 0.125f;
    int gridW_ = 0, gridH_ = 0;
    uint32_t numTris_ = 0, numCaps_ = 0, numPlanes_ = 0, numTriggers_ = 0;
    const CollTri* tris_ = nullptr;
    const CollCap* caps_ = nullptr;
    const CollPlane* planes_ = nullptr;
    const uint32_t* cellTable_ = nullptr;
    const uint16_t* cellData_ = nullptr;
    uint32_t cellDataCount_ = 0;
    const CollTrigger* triggers_ = nullptr;

    // Per-query scratch, allocated once at load (queries are main-thread only).
    static constexpr int kMaxGather = 256;
    mutable uint32_t* stamps_ = nullptr; // numTris + numCaps
    mutable uint32_t stamp_ = 0;
    mutable uint16_t gatherTris_[kMaxGather];
    mutable uint16_t gatherCaps_[kMaxGather];
    mutable int numGatherTris_ = 0, numGatherCaps_ = 0;
};

} // namespace tt::world
