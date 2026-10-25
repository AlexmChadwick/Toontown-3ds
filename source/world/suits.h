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
#include "render/anim.h"
#include "render/model.h"
#include "render/renderer.h"
#include "ui/balloons.h"
#include "world/collision.h"
#include "world/suit_path.h"

namespace tt::world {

class SuitActors {
public:
    // Street cogs (net::kMaxSuits) plus the facility cogs of the visible level zones.
    static constexpr int kMax = 32;

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
    // Test-only: draw the real Cog for head `code` at a pose (asset check without a server cog).
    void drawShowcase(render::Renderer& renderer, const char* code, float x, float y, float z, float h, bool walk, float t);
    // A walking cog whose body tube the toon at (x, y, z) touches (DistributedSuit's
    // CollisionTube, combat.md section 3), or 0. Only cogs on a street walk leg can fight.
    u32 touching(float x, float y, float z) const;
    // Current pose of a cog (world/battle.* CogPoseSource); false when unknown or hidden.
    bool pose(u32 doId, float* x, float* y, float* z, float* h) const;
    static bool poseThunk(void* self, uint32_t doId, float* x, float* y, float* z, float* h) {
        return static_cast<const SuitActors*>(self)->pose(doId, x, y, z, h);
    }
    // Battles place their cogs themselves: call after update() each frame while in battle.
    // anim: SuitAnim or a battle clip (world/battle.h kSuitAnim*), clipArg/clipStartMs with it.
    void overridePose(u32 doId, float x, float y, float z, float h, bool hidden, float flash, u8 anim = 0,
                      s8 clipArg = -1, u32 clipStartMs = 0);
    // Facility cogs (DistributedFactorySuit, world/level.*): the level runtime walks them and
    // sets their pose every frame; drawing, nametags, battles and overridePose work as above.
    // code: SuitDNA head code; levelDist: setLevelDist. False when no actor slot is free.
    bool setExternal(u32 doId, const char* code, int levelDist, float x, float y, float z, float h, bool walking,
                     bool visible, bool skelecog = false, float externalScale = 1); // cogdo: source maze actor scale
    void removeExternal(u32 doId);

private:
    // What the cog is doing: Walk replays the leg list; the others hold or fly from a pose.
    enum class Mode : u8 { Hidden, Walk, Frozen, FlyAway, DanceFly, External };
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
        float externalScale = 1; // cogdo: per-maze suit scale; ordinary actors retain 1
        bool skelecog = false; // DistributedSuitBase.setSkelecog: Suit.makeSkeleton
        float animTime = 0.0f;
        s8 clipArg = -1;
        u32 clipStartMs = 0;
    };
    void refresh(Actor& a, const net::Suit& s, const net::Client& client, u32 nowMs);
    void enterState(Actor& a, s8 state, u32 nowMs);
    bool ensureGraph(u32 branch);
    void buildPath(Actor& a);
    void evaluate(Actor& a, u32 nowMs, const CollisionWorld* collision);
    static float heightOf(const Actor& a);

    Actor actors_[kMax];
    SuitGraph graph_;
    render::Model model_; // stand-in box when the Cog bake is not installed
    // Real Cogs (docs/design/cogs.md): one body per type, heads per code, shared department pack.
    struct Body {
        bool tried = false;
        render::Model model;
        render::Anim neutral, walk;
        // Battle clips (react, attacks, lose), loaded the first time a fight needs them.
        static constexpr int kClips = 10;
        char clipName[kClips][24] = {};
        render::Anim clips[kClips];
        // Suit.getLoseActor: a defeated Cog swaps to its lose-mod model with its own 'lose'
        // animation (the head is instanced onto its joint_head); loaded the first time.
        bool loseTried = false;
        render::Model loseModel;
        render::Anim loseClip;
    };
    bool loadLose(int body);
    const render::Anim* battleClip(int body, const char* name);
    struct Head {
        char code[4] = {};
        bool tried = false;
        render::Model model;
    };
    static constexpr int kMaxHeads = 12;
    // Bodies 0-2: suitA/B/C; 3-5: their skelecog (cog*_robot) versions.
    bool loadBody(int b);
    Head* head(const char* code);
    void loadDepartments();
    void drawReal(render::Renderer& renderer, const Actor& a, const C3D_Mtx& world);
    Body bodies_[6];
    C3D_Tex tieTex_[4] = {}; // skeleton_ties.ttx: a skelecog's tie per department (c l m s)
    bool tiesLoaded_ = false;
    void loadTies();
    Head heads_[kMaxHeads];
    C3D_Tex deptTex_[4][3] = {}; // [c l m s][blazer, sleeve, legs]
    bool deptLoaded_ = false;
    u32 nowMs_ = 0;
    u32 branch_ = 0;
    u32 graphBranch_ = 0;
    bool graphFailed_ = false;
};

} // namespace tt::world
