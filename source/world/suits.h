// Street cogs (DistributedSuit), driven by tt::net::Client. The AI never sends a position:
// it sends a path through the street's suit graph, the leg the path was at and when, and a
// path state. This class rebuilds the walk from the street's .tsp graph exactly as
// DistributedSuit does (docs/design/combat.md section 3, "Client implementation status").
//
// Files:
//   sdmc:/3ds/toontown3ds/data/zones/<branch>.tsp   suit graph (tools/pipeline/bake_suit_paths.py)
//   romfs:/baked/suit_placeholder.ttm               stand-in model until the cog assets arrive
//
// Walking itself lives in world/suit_path.* (host tested against libtoontown).
#pragma once

#include <3ds.h>
#include <citro3d.h>

#include "net/client.h"
#include "render/model.h"
#include "render/renderer.h"
#include "ui/balloons.h"
#include "world/collision.h"
#include "world/suit_path.h"

namespace tt::world {

class SuitActors {
public:
    static constexpr int kMax = net::kMaxSuits;

    bool init(); // loads the placeholder model; false leaves suits invisible (never fatal)
    void shutdown();
    // Zone the player is in (the street branch is zone - zone % 100); suits of other zones
    // never reach us because the server only sends objects in our interest.
    void setZone(u32 zoneId);
    void handleEvent(const net::Event& ev, const net::Client& client, u32 nowMs);
    // collision: ground for the door legs (DistributedSuit enables its floor ray there).
    void update(float dt, u32 nowMs, const CollisionWorld* collision);
    void draw(render::Renderer& renderer);
    void addBalloons(ui::Balloons& balloons, const C3D_Mtx& proj, const C3D_Mtx& view) const;
    // x y z scale per visible cog standing on the ground; returns the count written.
    int shadowPositions(float* xyzs, int maxOut) const;
    int count() const;
    // Autotest helper: position of the first visible cog, false when none.
    bool firstPosition(float* x, float* y, float* z) const;
    void logState() const; // one log line per cog (debugging)
    // A walking cog whose body tube the toon at (x, y, z) touches (DistributedSuit's
    // CollisionTube, combat.md section 3), or 0. Only cogs on a street walk leg can fight.
    u32 touching(float x, float y, float z) const;
    // Current pose of a cog (world/battle.* CogPoseSource); false when unknown or hidden.
    bool pose(u32 doId, float* x, float* y, float* z, float* h) const;
    static bool poseThunk(void* self, uint32_t doId, float* x, float* y, float* z, float* h) {
        return static_cast<const SuitActors*>(self)->pose(doId, x, y, z, h);
    }
    // Battles place their cogs themselves: call after update() each frame while in battle.
    void overridePose(u32 doId, float x, float y, float z, float h, bool hidden, float flash);

private:
    // What the cog is doing: Walk replays the leg list; the others hold or fly from a pose.
    enum class Mode : u8 { Hidden, Walk, Frozen, FlyAway, DanceFly };
    struct Actor {
        bool active = false;
        u32 doId = 0;
        int type = -1;            // index into kSuitTypes
        char label[48] = {};      // "Flunky Level 1"
        // Path (rebuilt when setPathEndpoints changes).
        u16 pathStart = 0, pathEnd = 0, pathMin = 0, pathMax = 0;
        bool haveEndpoints = false;
        bool needBuild = false;   // endpoints changed or the graph was reloaded
        char dept = 'c';
        u32 zone = 0;             // visgroup it was last seen in (battle-cell fallback)
        SuitLegList legs;
        int legIndex = 0;         // search hint for legIndexAtTime
        // Timing: elapsed = (now - posMs) + legs.startTime(posIndex).
        u32 posMs = 0;
        u16 posIndex = 0;
        // Mode and state.
        Mode mode = Mode::Hidden;
        s8 state = 0;
        u32 modeSinceMs = 0;
        float baseX = 0, baseY = 0, baseZ = 0, baseH = 0; // where a fly-away takes off from
        // Current pose.
        SuitPose pose{};
        bool poseValid = false;
        u8 legType = kLegOff;
        float flash = 0.0f; // battle hit tint, set by overridePose for one frame
    };
    void refresh(Actor& a, const net::Suit& s, const net::Client& client, u32 nowMs);
    void enterState(Actor& a, s8 state, u32 nowMs);
    bool ensureGraph(u32 branch);
    void buildPath(Actor& a);
    void evaluate(Actor& a, u32 nowMs, const CollisionWorld* collision);
    static float heightOf(const Actor& a);

    Actor actors_[kMax];
    SuitGraph graph_;
    render::Model model_;
    u32 branch_ = 0;
    u32 graphBranch_ = 0;
    bool graphFailed_ = false;
};

} // namespace tt::world
