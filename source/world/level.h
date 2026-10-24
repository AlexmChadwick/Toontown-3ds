// Facility level runtime (docs/design/levels.md): one upstream level spec at a time — the
// Sellbot factory now, mint / stage / country-club rooms next. Mirrors otp/level/
// DistributedLevel on the client:
//   - geometry: <level>.tz, one chunk per spec zone, vis lists from the zone entities
//     (tools/pipeline/bake_level.py); the zone the toon stands in drives setVisibility
//   - spec: <level>.tlv (world/level_spec.*): entrance points, cog spawns/paths, battle cells
//   - cogs: DistributedFactorySuit walks its path locally, alerts/chases, requests battles
//   - battle blockers: walking into one starts (or joins) its cell's battle
// Files: sdmc:/3ds/toontown3ds/data/level/<level>.{tz,tlv}
#pragma once

#include <3ds.h>

#include "net/client.h"
#include "render/model.h"
#include "render/renderer.h"
#include "world/level_spec.h"
#include "world/zone.h"

namespace tt::world {

class SuitActors;

// What the toon's contact asks main.cpp to do with world/battle.* this frame.
struct LevelBattleAction {
    uint32_t cogDoId = 0;    // DistributedFactorySuit.requestBattle on this cog ...
    float x = 0, y = 0, z = 0, h = 0; // ... with its pose in its parent entity's space
    uint32_t joinBattle = 0; // or join this running battle (a blocker whose battle is on)
};

class Level {
public:
    void handleEvent(const net::Event& ev, net::Client& client);
    // feet: the local toon. Streams chunks, sends visibility and walks the cogs. battleIdle: no
    // battle of ours is running or pending; then touching a cog or a blocker fills *action.
    void update(net::Client& client, SuitActors& suits, float x, float y, float z, float dt, uint32_t nowMs,
                bool battleIdle, LevelBattleAction* action);
    void draw(render::Renderer& renderer, const C3D_FVec& eye);
    void shutdown();

    bool active() const { return active_; }
    const CollisionWorld* collision() const { return zone_.loaded() ? &zone_.collision() : nullptr; }
    // Once per level: where the entrance point puts us (EntrancePoint.placeToon).
    bool spawnPose(float* x, float* y, float* z, float* h);
    // World pose of one of our battle cells (Client::setLevelBattlePlacer).
    bool battleCell(uint32_t levelDoId, uint32_t cellId, float* x, float* y, float* z, float* h) const;
    static bool battleCellThunk(void* self, uint32_t levelDoId, uint32_t cellId, float* x, float* y, float* z,
                                float* h) {
        return static_cast<const Level*>(self)->battleCell(levelDoId, cellId, x, y, z, h);
    }
    // Battles lock visibility to their zone (DistributedLevelBattle.lockLevelViz).
    void lockVisibility(uint32_t zoneId) { lockZoneId_ = zoneId; }
    void unlockVisibility() { lockZoneId_ = 0; }
    uint32_t levelDoId() const { return levelDoId_; }
    int currentZoneNum() const { return zoneNum_; }
    // Autotest: world position of the nearest cog that can fight / of a blocker; false if none.
    bool nearestCog(float x, float y, float z, float* cx, float* cy, float* cz) const;
    // Closed doors are walls: moves (x, y) out of them (toon radius); true if it moved.
    bool pushOut(float* x, float* y, float z) const;
    // Autotest: the nearest closed door ahead, as a point in front of it; false if none.
    bool nearestDoor(float x, float y, float z, float* dx, float* dy, float* dz, float* dh) const;

private:
    enum class CogMode : uint8_t { Walk, Chase, Return, WaitBattle };
    struct Cog {
        uint32_t doId = 0;
        const LevelEntity* spec = nullptr;
        bool used = false;
        CogMode mode = CogMode::Walk;
        float x = 0, y = 0, z = 0, h = 0;      // current world pose
        float ox = 0, oy = 0, oz = 0, oh = 0;  // spawn (spec pos / h): originalPos
        // Path (pathWorld): walked at kWalkSpeed with a 1 s turn before each leg.
        float path[16][3] = {};
        int numPath = 0;
        float pathTime = 0;                    // seconds into the looping path track
        float pathLength = 0;                  // seconds per loop
        bool walking = false;
        bool chase = false;                    // spec behavior 'chase'
        bool alerted = false;                  // setAlert sent this walk
        float chaseClock = 0, chaseStep = 0;   // since setConfrontToon / since the last re-aim
        float tx = 0, ty = 0;                  // chase/return target
        uint32_t chaseSerial = 0, returnSerial = 0;
        uint32_t lastStrayMs = 0;
        uint32_t requestMs = 0;
    };
    bool load(const char* name);
    void begin(const net::Factory& f);
    void sendVisibility(net::Client& client, uint16_t cellZone, const uint16_t* zones, int count);
    static void cellChanged(void* self, uint16_t cellZone, const uint16_t* zones, int count);
    uint32_t zoneIdOf(int zoneNum) const;
    void setupCog(Cog& c, const net::LevelSuit& s);
    void walkPath(Cog& c, float dt);
    void moveToward(Cog& c, float speed, float dt);
    bool parentLocal(const Cog& c, float* x, float* y, float* z, float* h) const;
    void requestBattle(Cog& c, uint32_t nowMs, LevelBattleAction* action);
    Cog* cog(uint32_t doId);
    // DistributedDoorEntity (CogDoorHandShake parts, door.box; bake_level_props.py).
    enum DoorPart { kDoorFrame, kDoorTop, kDoorBottom, kDoorLeft, kDoorRight, kDoorLock0, kDoorParts = kDoorLock0 + 9 };
    struct DoorPose {
        float top = 0, bottom = 0, left = 0, right = 0; // outer z / inner x offsets
        bool outer = true, inner = true;                // shown (and solid)
        float lock[3] = {};                              // slide offset of the open halves
        bool lockClosed[3] = {}, lockOpen[3] = {};
    };
    void loadProps();
    static DoorPose doorPose(const net::LevelEntityObj& d, uint32_t nowMs);
    void updateDoors(net::Client& client, float x, float y, float z, uint32_t nowMs);
    void drawDoors(render::Renderer& renderer) const;

    Zone zone_;
    LevelSpec spec_;
    bool active_ = false;
    bool loaded_ = false;
    bool spawned_ = false;
    uint32_t levelDoId_ = 0, levelZoneId_ = 0;
    int numZones_ = 0;
    uint16_t zoneNums_[128] = {};   // spec zone entity ids, sorted (zone ids map 1:1)
    uint32_t zoneIds_[128] = {};    // setZoneIds
    uint8_t entranceId_ = 0;
    int toonIndex_ = 0, numToons_ = 1;
    int zoneNum_ = -1;              // the zone the toon stands in (lastToonZone)
    uint32_t lockZoneId_ = 0;
    uint32_t sentLock_ = 0;
    net::Client* client_ = nullptr; // for the Zone cell callback
    uint32_t pendingVis_[64] = {};
    int numPendingVis_ = 0;
    bool visDirty_ = false;
    static constexpr int kMaxCogs = net::kMaxLevelSuits;
    Cog cogs_[kMaxCogs];
    uint32_t blockerMs_ = 0;
    render::Model doorModels_[kDoorParts];
    float doorBox_[6] = {-6.6f, -0.35f, 0.0f, 6.8f, 0.4f, 14.2f};
    bool propsLoaded_ = false;
    uint32_t doorRequestMs_ = 0;
    const net::Client* doorClient_ = nullptr;
    uint32_t doorNowMs_ = 0;
};

} // namespace tt::world
