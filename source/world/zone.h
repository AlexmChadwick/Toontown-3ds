// TZ01 v1/v2 streaming container. See tools/pipeline/tz.py and docs/design/streets.md.
#pragma once
#include <cstdint>
#include "world/collision.h"

namespace tt::render { class Renderer; }
namespace tt::world {


class Zone {
public:
    static constexpr uint32_t kGeometryBudget = 6u * 1024 * 1024;
    static constexpr uint32_t kTextureBudget = 8u * 1024 * 1024;
    static constexpr int kMaxChunks = 256;
    static constexpr uint32_t kTriangleBudget = 15000;
    struct Trigger {
        float min[3], max[3];
        uint32_t kind; // 0 other, 1 door, 2 tunnel (approach volume)
        char name[64], owner[64];
    };
    // Called only after a successful residency change. Zones are unique server ids,
    // including branch + current cell + PVS. Copy if retaining beyond the callback.
    using CellChanged = void (*)(void* user, uint16_t cellZone, const uint16_t* zones, int count);

    Zone() = default;
    ~Zone();
    Zone(const Zone&) = delete;
    Zone& operator=(const Zone&) = delete;
    // Main thread, outside C3D_FrameBegin/End. Chunks and collision are read straight
    // from the open .tz (Model::loadFrom / CollisionWorld::loadFrom); every linear
    // allocation goes through tt::mem::LinearGuard, so it is safe next to the toon loader.
    bool load(const char* path);
    void unload();
    bool loaded() const { return impl_ != nullptr; }
    const char* error() const { return error_; }
    // Call once before FrameBegin, never per eye.
    // No floor hit keeps the previous PVS (jumping/outside floor cannot unload it).
    // Chunk reads happen on a loader thread; update() swaps finished chunks in, so call
    // it every frame. A chunk changing LOD keeps drawing its old LOD until then.
    bool update(const Vec3& feet, CellChanged callback = nullptr, void* user = nullptr);
    bool loading() const; // chunks of the current plan are still in flight
    // Blocks until the current plan is resident (arrival in a new zone).
    void finishLoading(const Vec3& feet, CellChanged callback = nullptr, void* user = nullptr);
    // Renderer performs frustum culling; distance is measured to chunk AABB.
    void draw(render::Renderer& renderer, const Vec3& eye, float distance = 120.0f);
    int cellAt(const Vec3& feet, float reach = kFloorReach) const; // -1 on miss
    uint16_t zoneAt(const Vec3& feet, float reach = kFloorReach) const; // 0 on miss
    uint16_t currentZone() const;
    uint16_t branchZone() const;
    int currentCell() const;
    int numChunks() const;
    const float* boundsMin() const;
    const float* boundsMax() const;
    int numVisible() const;
    int visibleZones(uint16_t* out, int capacity) const; // required count incl branch
    uint16_t version() const;
    uint32_t triangles() const;
    uint32_t drawCalls() const;
    int chunkLod(int index) const; // -1 not resident, 0 near, 1 far
    uint32_t geometryBytes() const;
    uint32_t textureBytes() const;
    int numTriggers() const;
    const Trigger* trigger(int index) const;
    // Conservative sphere vs trigger AABB. Debounce entry/exit in game code.
    int triggersAt(const Vec3& point, float radius, int* out, int capacity,
                   bool tunnelsOnly = false) const;
    CollisionWorld& collision() { return collision_; }
    const CollisionWorld& collision() const { return collision_; }

public:
    struct Impl; // public so the loader thread entry point can see it

private:
    bool fail(const char* message);
    Impl* impl_ = nullptr;
    CollisionWorld collision_;
    char error_[128] = {};
};
} // namespace tt::world
