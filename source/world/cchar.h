// Classic characters (Mickey, Minnie, Goofy, ...): DistributedCCharBase objects driven
// by tt::net::Client. The class id picks the baked actor; setWalk(src, dst, ts) names a
// leg of the walk graph, which is replayed like CharStateDatas.CharWalkState.
//
// Files (tools/pipeline/bake_char.py, char_paths_bin.py; docs/design/chars.md):
//   sdmc:/3ds/toontown3ds/data/chars/<asset>.ttm, <asset>_<clip>.tta
//   sdmc:/3ds/toontown3ds/data/chars/<char>_<hood>.tcp   walk graph for that playground
#pragma once

#include <3ds.h>
#include <citro3d.h>

#include "net/client.h"
#include "render/anim.h"
#include "render/model.h"
#include "render/renderer.h"
#include "ui/balloons.h"
#include "world/boat.h"
#include "world/collision.h"

namespace tt::world {

struct CharInfo;

class CCharActors {
public:
    static constexpr int kMaxNodes = 32;
    static constexpr int kMaxEdges = 64;
    static constexpr int kMaxPathPoints = 256;
    static constexpr int kMaxSegments = 16;

    void setZone(u32 zoneId) { hood_ = zoneId - zoneId % 1000; }
    // Donald at the wheel (DistributedDonaldDock) is parented to the boat; he is only drawn
    // while this boat is active. Must outlive the actors.
    void setBoat(const BoatActor* boat) { boat_ = boat; }
    void shutdown();
    void handleEvent(const net::Event& ev, const net::Client& client, u32 nowMs);
    // collision: ground for raycast legs (CharWalkState enables the floor ray there).
    void update(float dt, u32 nowMs, const CollisionWorld* collision);
    void draw(render::Renderer& renderer);
    void addBalloons(ui::Balloons& balloons, const C3D_Mtx& proj, const C3D_Mtx& view) const;
    int shadowPositions(float* xyzs, int maxOut) const;
    int count() const;
    // Autotest helper: position of the first character, false when none.
    bool firstPosition(float* x, float* y, float* z) const;

private:
    enum Clip : u8 { kNeutral, kWalk, kWalkStart, kNeutralStart, kClipCount };
    struct Segment {
        float start[3], end[3];
        float h0, h1;
        float turnTime, moveTime, length;
    };
    struct Actor {
        bool active = false;
        u32 doId = 0;
        const CharInfo* info = nullptr;
        render::Model model;
        render::Anim clips[kClipCount];
        // Walk graph (one per character and hood).
        int numNodes = 0, numEdges = 0, numPoints = 0;
        float speed = 0;
        char nodeName[kMaxNodes][8] = {};
        float nodePos[kMaxNodes][3] = {};
        u8 edgeSrc[kMaxEdges] = {}, edgeDst[kMaxEdges] = {}, edgeRay[kMaxEdges] = {}, edgeCount[kMaxEdges] = {};
        u16 edgeFirst[kMaxEdges] = {};
        float points[kMaxPathPoints][3] = {};
        // Current leg.
        Segment seg[kMaxSegments];
        int numSeg = 0;
        bool walking = false, raycast = false;
        u32 walkStartMs = 0;
        float walkDuration = 0;
        float x = 0, y = 0, z = 0, h = 0;
        float clipTime = 0;
        Clip clip = kNeutral;
        float fade = 0;
    };
    bool spawn(Actor& a, const net::CChar& c);
    bool loadGraph(Actor& a, const char* path);
    int nodeIndex(const Actor& a, const char* name) const;
    void startWalk(Actor& a, const net::CChar& c);
    void sample(Actor& a, float t, const CollisionWorld* collision);

    Actor actors_[net::kMaxCChars];
    u32 hood_ = 2000;
    const BoatActor* boat_ = nullptr;
};

} // namespace tt::world
