// The zone the player is in (Toontown's "Place"): its model, collision and anchor
// points, resolved from a zone id, plus the exits (tunnels, doors) that lead elsewhere.
//
// Zone ids (hood/ZoneUtil.py): zoneId % 1000 < 100 playground, >= 500 interior
// (hood + 500 + block), otherwise a street. Files:
//   any zone with a .tz           sdmc:/3ds/toontown3ds/data/zones/<branch>.tz (streamed; preferred)
//   2000 (TTC playground)       romfs:/baked/ttc_sz.{ttm,ttc,pts}
//   streets                     sdmc:/3ds/toontown3ds/data/zones/<branch>.{ttm,ttc,pts} (2132 -> 2100)
//   other playgrounds           sdmc:/3ds/toontown3ds/data/zones/<zone>.{ttm,ttc,pts}
//   interiors                   sdmc:/3ds/toontown3ds/data/interiors/<zone>.{ttm,ttc,pts}
#pragma once

#include <3ds.h>

#include "render/model.h"
#include "render/renderer.h"
#include "world/collision.h"
#include "world/zone.h"

namespace tt::world {

enum class PlaceKind : u8 { Playground, Street, Interior };
PlaceKind placeKind(u32 zoneId);
u32 hoodOf(u32 zoneId); // 2000 for every TTC zone (2000, 21xx, 25xx, ...)

struct Exit {
    enum Kind : u8 { None, Tunnel, Door } kind = None;
    u32 targetZone = 0;
    char name[64] = {}; // the trigger owner (DNA root names reach 40+ characters)
};

class Place {
public:
    static constexpr int kMaxPoints = 256; // streets: every door and tunnel anchor (up to 177 so far)
    struct Point {
        char name[64]; // e.g. "sz13:toon_landmark_TT_toonhall_DNARoot_door_origin"
        float x, y, z, h;
    };

    // True if this zone's files are present (so a tunnel/door can lead there).
    static bool available(u32 zoneId);
    bool load(u32 zoneId);
    void unload();
    u32 zoneId() const { return zoneId_; }
    PlaceKind kind() const { return placeKind(zoneId_); }

    render::Model& model() { return model_; }
    // Streets (and playgrounds baked as .tz) stream per visgroup through a Zone.
    bool streamed() const { return zone_.loaded(); }
    Zone& zone() { return zone_; }
    const CollisionWorld& collision() const { return zone_.loaded() ? zone_.collision() : collision_; }
    const float* boundsMin() const { return zone_.loaded() ? zone_.boundsMin() : model_.boundsMin(); }
    const float* boundsMax() const { return zone_.loaded() ? zone_.boundsMax() : model_.boundsMax(); }
    // Streams chunks for the toon's cell; cb fires when the server cell (street visgroup)
    // changes, with [branch] + vis zones for the interest. Call once per frame.
    // setCamera before update: the draw/triangle cap follows that view.
    void setCamera(const C3D_FVec& eye, const C3D_FVec& target);
    void update(float x, float y, float z, Zone::CellChanged cb, void* user);
    // Queues the draw plan, then a ring of neighbouring chunks. The loader thread
    // does the reads. Call update() until streamReady(). streamReady is the part of
    // the cone that covers most of the screen; near upgrades and the ring continue
    // after the card drops.
    void beginStreaming(float x, float y, float z, Zone::CellChanged cb, void* user);
    bool streamReady() const;
    float streamProgress() const;
    // Blocks until the chunks around (x, y, z) are resident (boss / cogdo arrival).
    void finishLoading(float x, float y, float z, Zone::CellChanged cb, void* user);
    void draw(render::Renderer& renderer, const C3D_FVec& eye);
    const char* pointsPath() const { return pointsPath_; }
    const Point* point(const char* name) const;
    const Point* pointWithPrefix(const char* prefix) const;
    // A building's DNA root ("tb<block>:<node>" on streets, "sz<block>:<node>" in playgrounds),
    // or a plain "tb<block>" / "sz<block>" anchor; nullptr if neither exists.
    const Point* buildingPoint(u32 block) const;
    // A Toon HQ's outside door <door> (door_0 / door_1) of that block, or nullptr.
    const Point* buildingDoorPoint(u32 block, int door) const;
    int numPoints() const { return numPoints_; }
    const Point& pointAt(int idx) const { return points_[idx]; }

    // The exit the toon at (x, y, z) is standing in (tunnel/door trigger), if any.
    Exit exitAt(float x, float y, float z) const;
    // Where to stand after arriving from `fromZone` (0 = default spawn).
    void arrival(u32 fromZone, float* x, float* y, float* z, float* h) const;

private:
    bool resolve(u32 zoneId, const char* ext, char* out, size_t outSize) const;
    void loadPoints();

    u32 zoneId_ = 0;
    render::Model model_;
    CollisionWorld collision_;
    Zone zone_;
    char pointsPath_[96] = {};
    Point points_[kMaxPoints];
    int numPoints_ = 0;
};

} // namespace tt::world
