// The zone the player is in (Toontown's "Place"): its model, collision and anchor
// points, resolved from a zone id, plus the exits (tunnels, doors) that lead elsewhere.
//
// Zone ids (hood/ZoneUtil.py): zoneId % 1000 < 100 playground, >= 500 interior
// (hood + 500 + block), otherwise a street. Files:
//   2000 (TTC playground)       romfs:/baked/ttc_sz.{ttm,ttc,pts}
//   streets                     sdmc:/3ds/toontown3ds/data/zones/<branch>.{ttm,ttc,pts} (2132 -> 2100)
//   other playgrounds           sdmc:/3ds/toontown3ds/data/zones/<zone>.{ttm,ttc,pts}
//   interiors                   sdmc:/3ds/toontown3ds/data/interiors/<zone>.{ttm,ttc,pts}
#pragma once

#include <3ds.h>

#include "render/model.h"
#include "world/collision.h"

namespace tt::world {

enum class PlaceKind : u8 { Playground, Street, Interior };
PlaceKind placeKind(u32 zoneId);
u32 hoodOf(u32 zoneId); // 2000 for every TTC zone (2000, 21xx, 25xx, ...)

struct Exit {
    enum Kind : u8 { None, Tunnel, Door } kind = None;
    u32 targetZone = 0;
    char name[32] = {};
};

class Place {
public:
    static constexpr int kMaxPoints = 64;
    struct Point {
        char name[40];
        float x, y, z, h;
    };

    // True if this zone's files are present (so a tunnel/door can lead there).
    static bool available(u32 zoneId);
    bool load(u32 zoneId);
    void unload();
    u32 zoneId() const { return zoneId_; }
    PlaceKind kind() const { return placeKind(zoneId_); }

    render::Model& model() { return model_; }
    const CollisionWorld& collision() const { return collision_; }
    const char* pointsPath() const { return pointsPath_; }
    const Point* point(const char* name) const;
    const Point* pointWithPrefix(const char* prefix) const;

    // The exit the toon at (x, y, z) is standing in (tunnel/door trigger), if any.
    Exit exitAt(float x, float y, float z) const;
    // Where to stand after arriving from `fromZone` (0 = default spawn).
    void arrival(u32 fromZone, float* x, float* y, float* z, float* h) const;

private:
    bool resolve(u32 zoneId, const char* ext, char* out, size_t outSize) const;

    u32 zoneId_ = 0;
    render::Model model_;
    CollisionWorld collision_;
    char pointsPath_[96] = {};
    Point points_[kMaxPoints];
    int numPoints_ = 0;
};

} // namespace tt::world
