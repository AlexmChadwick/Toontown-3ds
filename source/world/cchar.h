// Classic characters (Mickey, Minnie, Goofy, ...): DistributedCCharBase objects driven
// by tt::net::Client. The class id picks the baked actor; setWalk(src, dst, ts) names a
// leg of the walk graph, which is replayed like CharStateDatas.CharWalkState.
//
// Files (tools/pipeline/bake_char.py, char_paths_bin.py; docs/design/chars.md):
//   sdmc:/3ds/toontown3ds/data/chars/<asset>.ttm, <asset>_<clip>.tta
//   sdmc:/3ds/toontown3ds/data/chars/<char>_<hood>.tcp   walk graph for that playground
#pragma once

#ifdef __3DS__
#include <3ds.h>
#include <citro3d.h>

#include "render/anim.h"
#include "render/model.h"
#include "render/renderer.h"
#include "ui/balloons.h"
#include "world/boat.h"
#else
namespace tt::world { class BoatActor; }
#include <cstdint>
using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using s32 = int32_t;
struct C3D_Mtx {};
namespace tt::render {
class Renderer;
class Model {
public:
    bool load(const char*) { return false; }
    void unload() {}
    bool loaded() const { return false; }
    int numVertices() const { return 0; }
};
class Anim {
public:
    bool load(const char*) { return false; }
    void unload() {}
    bool loaded() const { return false; }
    float duration() const { return 0.0f; }
    int numFrames() const { return 1; }
    float fps() const { return 12.0f; }
    int numVertices() const { return 0; }
};
} // namespace tt::render
namespace tt::ui {
class Balloons {
public:
    void addSpeaker(float, float, const char*, const char*, float, float = 0.0f, uint32_t = 0) {}
};
} // namespace tt::ui
#endif

#include "net/client.h"
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

    struct Segment {
        float start[3], end[3];
        float h0, h1;
        float turnTime, moveTime, length;
    };
    struct DaleOrbit {
        float x, y, z, h;
    };
    static int buildSegments(Segment* seg, int maxSeg, const float pts[][3], int n, float speed, float initialHeading);
    static DaleOrbit computeDaleOrbit(float t, float duration, const float srcPos[3], const float dstPos[3],
                                      float srcTheta, float destTheta, float revs, float orbitRadius);

private:
    enum Clip : u8 { kNeutral, kWalk, kWalkStart, kNeutralStart, kClipCount };
    struct Actor {
        bool active = false;
        u32 doId = 0;
        const CharInfo* info = nullptr;
        render::Model model;
        render::Anim clips[kClipCount];
        // Background load (world/asset_loader.h): tickets[0] = model, 1 + clip = clips. The actor
        // draws nothing until the model is swapped in; walk timing is a pure function of the
        // clock, so the replay is exact whenever it becomes ready.
        bool loading = false;
        u32 tickets[1 + kClipCount] = {};
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
        // Chatter balloon & turn
        char chatPhrase[160] = {};
        float chatTimer = 0.0f;
        float chatAge = 0.0f;
        bool turning = false;
        float turnH0 = 0.0f, turnH1 = 0.0f;
        float turnTime = 0.0f, turnElapsed = 0.0f;
        // Dale orbit
        bool isDale = false;
        uint32_t chipId = 0;
        float daleOffsets[kMaxNodes][2] = {};
        bool hasDaleOffset[kMaxNodes] = {};
        float daleSrcOffset[2] = {};
        float daleDstOffset[2] = {};
        float daleSrcTheta = 0.0f, daleDestTheta = 0.0f;
        float daleRevs = 0.0f;
        float daleOrbitDist = 3.0f;
        float daleChipSrc[3] = {}, daleChipDst[3] = {};
    };
    bool spawn(Actor& a, const net::CChar& c);
    void pollLoads(Actor& a);
    void cancelLoads(Actor& a);
    bool loadGraph(Actor& a, const char* path);
    int nodeIndex(const Actor& a, const char* name) const;
    void startWalk(Actor& a, const net::CChar& c);
    void sample(Actor& a, float t, const CollisionWorld* collision);

    Actor actors_[net::kMaxCChars];
    u32 hood_ = 2000;
    const BoatActor* boat_ = nullptr;
};

} // namespace tt::world
