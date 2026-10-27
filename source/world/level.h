#include "world/asset_batch.h"
// Facility level runtime (docs/design/levels.md): the Sellbot factory (one level), or one floor
// of a mint / DA office (stage) / country club (a chain of level rooms joined by hallways).
// Mirrors otp/level/DistributedLevel and DistributedMint/Stage/CountryClub on the client:
//   - geometry: <level>.tz per level, one chunk per spec zone (tools/pipeline/bake_level.py).
//     The factory's PVS follows the zone the toon stands in (setVisibility); a floor places
//     each room by its baked transform (floors/<kind>_<id>_<layout>_<floor>.tlf,
//     bake_facility_floors.py), shows the rooms around the toon's (camEnterRoom) and keeps
//     every room's zones in interest (gotAllRooms)
//   - spec: <level>.tlv per level, merged into one floor-space LevelSpec (world/level_spec.*):
//     entrance points, cog spawns/paths, battle cells
//   - cogs: DistributedFactorySuit walks its path locally, alerts/chases, requests battles
//   - battle blockers: walking into one starts (or joins) its cell's battle
// Files: sdmc:/3ds/toontown3ds/data/level/<level>.{tz,tlv}, floors/*.tlf, props/*
#pragma once

#include <3ds.h>

#include "audio/audio.h"
#include "net/client.h"
#include "render/anim.h"
#include "render/model.h"
#include "render/renderer.h"
#include "world/level_spec.h"
#include "world/player.h"
#include "world/zone.h"
// ---- level games (world/level_games.*) ----
#include "world/level_games.h"
// ---- end level games ----

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

    void pumpAssets() {
        assetBatch_.pump();
        games_.pumpAssets();
        if (entryLoading_ && spawned_ && !assetBatch_.loading() && !games_.loadingAssets()) {
            bool pending = false;
            for (int room = 0; room < numRooms_; ++room)
                pending |= rooms_[room].zone.loading();
            if (!pending)
                entryLoading_ = false;
        }
    }
    bool loadingAssets() const {
        return entryLoading_ || assetBatch_.loading() || games_.loadingAssets();
    }
    bool active() const { return active_; }
    // The factory's own collision, or a floor's assembled one (rooms, hallways, door frames).
    const CollisionWorld* collision() const {
        if (floor_) return floorCollision_.loaded() ? &floorCollision_ : nullptr;
        return rooms_[0].zone.loaded() ? &rooms_[0].zone.collision() : nullptr;
    }
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
    // One of our DistributedLevels (the factory, or a room of the floor).
    bool ownsLevel(uint32_t levelDoId) const { return active_ && roomOf(levelDoId) >= 0; }
    // setDefeated (the factory, or a floor's last room) -> the interior's handle*WinEvent:
    // teleport to the hood (ZoneUtil.getHoodId: 11500 -> 11000, 12500 -> 12000, ...). 0 until
    // then; once.
    uint32_t popWinDestination() {
        const uint32_t z = winDestination_;
        winDestination_ = 0;
        return z;
    }
    int currentZoneNum() const { return zoneNum_; }
    // DA office floor elevators (DistributedElevatorFloor): once boarded, where our seat is
    // (ElevatorPoints[slot] in the elevator, facing out); then, when it leaves, the next
    // floor's zone (DistributedStage.setStageZone). Each is returned once.
    bool popSeat(float* x, float* y, float* z, float* h) {
        if (!seatPending_) return false;
        seatPending_ = false;
        *x = seat_[0], *y = seat_[1], *z = seat_[2], *h = seat_[3];
        return true;
    }
    bool aboardElevator() const { return boardedElevator_ != 0; }
    uint32_t popNextFloorZone() {
        const uint32_t z = nextFloorZone_;
        nextFloorZone_ = 0;
        return z;
    }
    // Autotest: the floor-space position of a floor elevator's doorway; false if none.
    bool elevatorPosition(float* x, float* y, float* z, float* h) const;
    // Autotest: world position of the nearest cog that can fight / of a blocker; false if none.
    bool nearestCog(float x, float y, float z, float* cx, float* cy, float* cz) const;
    // Closed doors, buttons, lifts...: Player::setDynamic while the level is active.
    const DynamicSolids* solids() const { return active_ ? &dynamic_ : nullptr; }
    // Stomper squish this frame (DistributedLevel.b_setOuch(.., 'Squish')): the damage was
    // sent; main.cpp plays Toon.enterSquish and stuns. Returns 0 when nothing happened.
    // A goon zapped us this frame (enterBattle: b_setOuch(strength)); 0 when nothing happened.
    int popOuch() {
        const int d = ouchDamage_;
        ouchDamage_ = 0;
        return d;
    }
    int popSquish(float* floorZ) {
        const int d = squishDamage_;
        squishDamage_ = 0;
        *floorZ = squishZ_;
        return d;
    }
    // Toon.stunToon: no ouch while the blink runs (about 4 s).
    void setStunned(bool stunned) { stunned_ = stunned; }
    // Autotest: world position of the first spec entity of a type (generated or not).
    bool specPosition(const char* type, float* x, float* y, float* z) const {
        const int i = active_ ? spec_.next(type, -1) : -1;
        if (i < 0) return false;
        const LevelEntity& e = spec_.entity(i);
        *x = e.world[9], *y = e.world[10], *z = e.world[11];
        return true;
    }
    // A floor: the piece (room 2i / hallway 2i+1) the toon stands in, and the piece count.
    int currentPiece() const { return floor_ ? piece_ : -1; }
    int numPieces() const { return floor_ ? numPieces_ : 0; }
    // Autotest: the floor-space centre of a piece's floor; false if none.
    bool pieceCenter(int piece, float* x, float* y, float* z) const;
    // Autotest: the spawn of the level's boss cog (CogData 'boss': 1, the foreman).
    bool foremanPosition(float* x, float* y, float* z) const {
        for (int i = active_ ? spec_.next("cogSpec", -1) : -1; i >= 0; i = spec_.next("cogSpec", i)) {
            const LevelEntity& e = spec_.entity(i);
            if (!spec_.integer(e, "boss", 0)) continue;
            *x = e.world[9], *y = e.world[10], *z = e.world[11];
            return true;
        }
        return false;
    }
    // Autotest: the live pose of the nearest walking goon; false if none.
    bool nearestGoon(float x, float y, float z, float* gx, float* gy, float* gz, float* gh) const {
        float best = 1e30f;
        for (const Goon& g : goons_) {
            if (!g.valid || g.mode != GoonMode::Walk) continue;
            const float d = (g.x - x) * (g.x - x) + (g.y - y) * (g.y - y) + (g.z - z) * (g.z - z);
            if (d < best) best = d, *gx = g.x, *gy = g.y, *gz = g.z, *gh = g.h;
        }
        return best < 1e30f;
    }
    // Autotest: world position of the nearest distributed entity of a class; false if none.
    bool nearestEntity(uint16_t classId, float x, float y, float z, float* ex, float* ey, float* ez) const;
    // Autotest: the nearest closed door ahead, as a point in front of it; false if none.
    bool nearestDoor(float x, float y, float z, float* dx, float* dy, float* dz, float* dh) const;

    // ---- level games (world/level_games.*) ----
    LevelGames& games() { return games_; }
    const LevelGames& games() const { return games_; }
    bool nearestEntityTransform(uint16_t classId, float x, float y, float z, float outWorld[12]) const;
    // ---- end level games ----

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
        bool inBattle = false;                 // a member of a battle: world/battle.* owns it
        int room = -1;                         // its level (rooms_ index)
    };
    static constexpr int kMaxRooms = LevelSpec::kMaxRooms;
    // One DistributedLevel: the factory, or a room of a floor (in setRoomDoIds order).
    struct Room {
        uint32_t doId = 0;
        int piece = 0;            // its index among the floor's pieces (roomNum; rooms are even)
        Zone zone;                // its .tz, drawn and streamed in its own space
        float placement[12] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0}; // room -> floor (rows)
        uint32_t levelZoneId = 0;
        int numZones = 0;
        uint16_t zoneNums[128] = {}; // spec zone entity ids, sorted (zone ids map 1:1)
        uint32_t zoneIds[128] = {};  // setZoneIds
        uint32_t startMs = 0;        // setStartTimestamp: the time base of its movers
        bool zonesSent = false;
    };
    bool loadRoom(int room, const char* name, const float placement[12]);
    bool loadFloor(const net::Facility& f);
    void tryBeginFloor(net::Client& client);
    void activate(net::Client& client);
    void loadSpinModels();
    void begin(Room& room, const net::LevelObj& l);
    int roomOf(uint32_t levelDoId) const {
        for (int i = 0; i < numRooms_; ++i)
            if (rooms_[i].doId == levelDoId) return i;
        return -1;
    }
    // The spec entity of a distributed level entity, or nullptr when it is not one of ours.
    const LevelEntity* specOf(uint32_t levelDoId, uint32_t entId) const {
        const int r = roomOf(levelDoId);
        return r < 0 ? nullptr : spec_.find(r, entId);
    }
    // Shown and solid: every room of the factory; on a floor, the rooms camEnterRoom unstashes.
    bool shown(int room) const {
        if (!floor_) return true;
        const int p = rooms_[room].piece;
        return p >= showMin_ && p <= showMax_;
    }
    void sendFloorInterest(net::Client& client);
    void updatePiece(float x, float y, float z);
    void sendVisibility(net::Client& client, uint16_t cellZone, const uint16_t* zones, int count);
    static void cellChanged(void* self, uint16_t cellZone, const uint16_t* zones, int count);
    uint32_t zoneIdOf(int zoneNum) const; // the factory's zone ids
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
    bool entryLoading_ = false;
    AssetBatch assetBatch_;
    void loadProps();
    static DoorPose doorPose(const net::LevelEntityObj& d, uint32_t nowMs);
    void updateDoors(net::Client& client, float x, float y, float z, uint32_t nowMs);
    void drawDoors(render::Renderer& renderer) const;
    // DistributedButton (CogDoor_Button parts, button.box).
    void updateButtons(net::Client& client, float x, float y, float z);
    void drawButtons(render::Renderer& renderer) const;
    // DistributedStomper (square_stomper / LB_square_stomper head + shaft, stomper.box).
    struct StomperShape {
        render::Model head, shaft;
        float lo[3] = {-1, -1.274f, -1}, hi[3] = {1, 0, 1}; // head bounds (model space)
        float shaftOffset[3] = {0, -0.994f, 0};
    };
    StomperShape stompers_[2];
    struct StomperPose {
        float raise = 0;         // model Y = -range * raise (0 = down / crushing)
        bool vertical = true;
        float headScale[3] = {1, 1, 1}, shaftScale[3] = {1, 1, 1};
        int style = 0;           // cogStyle: 0 square_stomper, 1 LB_square_stomper
    };
    StomperPose stomperPose(const LevelEntity& e, const net::LevelEntityObj& d, uint32_t nowMs) const;
    void updateStompers(net::Client& client, float x, float y, float z, uint32_t nowMs);
    void drawStompers(render::Renderer& renderer) const;
    float entityPrev_[net::kMaxLevelEntities] = {}; // per client slot: last raise / phase
    int entityCycle_[net::kMaxLevelEntities] = {};
    uint8_t entityState_[net::kMaxLevelEntities] = {};
    uint32_t winDestination_ = 0;
    bool won_ = false;
    int squishDamage_ = 0;
    float squishZ_ = 0;
    bool stunned_ = false;
    float toonX_ = 0, toonY_ = 0, toonZ_ = 0;
    // Sounds (bake_level_props.py LEVEL_SFX), played at the entity like SoundInterval(node=).
    enum Sfx { kSfxStompSmall, kSfxStompMed, kSfxStompLarge, kSfxDoorSlide, kSfxDoorFinal, kSfxDoorUnlock,
               kSfxArms, kSfxSwitchOn, kSfxSwitchCount, kSfxSwitchOff, kSfxLift, kSfxBarrelGrab,
               kSfxGoonCollapse, kSfxGoonRecover, kSfxGoonAttack, kSfxCount };
    audio::SfxId sfx_[kSfxCount] = {};
    void playAt(int sfx, const float pos[3], float volume) const;
    // DistributedLift (Elevator floor model; lift.box: floor, ceiling, front/back/left/right).
    enum LiftBox { kLiftFloor, kLiftCeiling, kLiftFront, kLiftBack, kLiftLeft, kLiftRight, kLiftBoxes };
    render::Model liftModel_;
    float liftBoxes_[kLiftBoxes][6] = {};
    bool liftPose(const LevelEntity& e, const net::LevelEntityObj& d, uint32_t nowMs, float world[12],
                  bool* moving) const;
    void updateLifts(net::Client& client, float x, float y, float z, uint32_t nowMs);
    void drawLifts(render::Renderer& renderer) const;
    float liftPrev_[net::kMaxLevelEntities][3] = {};
    uint32_t onLift_ = 0;
    audio::VoiceHandle liftVoice_[net::kMaxLevelEntities] = {};
    // DistributedGagBarrel / BeanBarrel / HealBarrel (gagTank + label, barrel.box).
    render::Model barrelModel_, barrelJar_, barrelIcecream_;
    render::Model gagIcons_[7][7];
    float barrelBox_[6] = {-3, -2.65f, 0, 3, 2.6f, 8.27f};
    void updateBarrels(net::Client& client, float x, float y, float z, uint32_t nowMs);
    void drawBarrels(render::Renderer& renderer) const;
    uint32_t barrelRequestMs_ = 0;
    // Client-only movers from the spec (no network object): GearEntity, PlatformEntity /
    // PaintMixer, ConveyorBelt, PropSpinner.
    render::Model gearModels_[2];         // factory, mint
    float gearBox_[2][6] = {{-1, -1, -0.304f, 1, 1, 0.0015f}, {-1, -1, 0.004f, 1, 1, 0.31f}};
    render::Model mixerTop_, mixerShaft_, treadModel_;
    float mixerFloor_[6] = {}, mixerWalls_[2][6] = {}, mixerShaftOrigin_[3] = {0, 0, -3.52f};
    int numMixerWalls_ = 0;
    float treadBox_[6] = {-5, -5, 0, 5, 5, 0.431f};
    static constexpr int kMaxSpins = 32;
    render::Model spinModels_[kMaxSpins];
    uint16_t spinRoom_[kMaxSpins] = {}, spinEnt_[kMaxSpins] = {};
    int numSpins_ = 0;
    const render::Model* spinModel(const LevelEntity& e) const {
        for (int k = 0; k < numSpins_; ++k)
            if (spinRoom_[k] == e.room && spinEnt_[k] == e.entId) return &spinModels_[k];
        return nullptr;
    }
    // Seconds since the entity's level started (setStartTimestamp; movers run from it).
    float levelTime(uint32_t nowMs, const LevelEntity& e) const {
        return int32_t(nowMs - rooms_[e.room].startMs) / 1000.0f;
    }
    void gearMatrix(const LevelEntity& e, float t, C3D_Mtx* m) const;
    bool platformOffset(const LevelEntity& e, float t, float out[3]) const;
    void updateMovers(float x, float y, float z, uint32_t nowMs);
    void drawMovers(render::Renderer& renderer) const;
    float moverPrev_[64][3] = {};
    uint32_t moverPrevMs_ = 0;
    // DistributedGoon (Cog_Goonie per hat + walk/collapse/recovery clips, radar cone, goon.box).
    enum GoonClip { kGoonWalk, kGoonCollapse, kGoonRecovery, kGoonClips };
    enum class GoonMode : uint8_t { Off, Walk, Battle, Stunned, Recovery };
    struct Goon {
        uint32_t doId = 0;
        GoonMode mode = GoonMode::Off;
        uint32_t modeMs = 0;      // local time the mode began
        float walkT = 0;          // walk-track time at modeMs (the pause time when paused)
        uint8_t lastMovie = 0xFF;
        uint32_t lastMovieMs = 0;
        float x = 0, y = 0, z = 0, h = 0; // last pose (world)
        bool valid = false;
        bool onHead = false;      // our toon is in its head sphere (the stun is on entering only)
    };
    Goon goons_[net::kMaxLevelEntities];
    render::Model goonModels_[2];   // pg, sg
    render::Anim goonClips_[2][kGoonClips];
    render::Model goonCone_;
    float goonRadar_[3] = {0, 0.546f, 3.048f};
    bool goonPose(const LevelEntity& e, float walkT, float* x, float* y, float* z, float* h) const;
    float goonTrackTime(const Goon& g, uint32_t nowMs) const;
    void updateGoons(net::Client& client, float x, float y, float z, uint32_t nowMs);
    void drawGoons(render::Renderer& renderer) const;
    int ouchDamage_ = 0; // a goon zap this frame (stun only, no squish)
    // DistributedElevatorFloor (LB_ElevatorScaled frame + doors, stage_elevator.box).
    render::Model elevatorFrame_, elevatorLeft_, elevatorRight_;
    static constexpr int kMaxElevatorBoxes = 20;
    float elevatorBoxes_[kMaxElevatorBoxes][6] = {};
    char elevatorBoxKind_[kMaxElevatorBoxes] = {}; // 'w'all, 'd'oor (while closed), 'f'loor
    int numElevatorBoxes_ = 0;
    const LevelEntity* elevatorMarker(const net::LevelElevator& e) const;
    float elevatorDoors(const net::LevelElevator& e, uint32_t nowMs) const; // 0 open .. 1 closed
    void updateElevators(net::Client& client, float x, float y, float z, uint32_t nowMs);
    void drawElevators(render::Renderer& renderer) const;
    uint32_t elevatorRequestMs_ = 0;
    uint32_t boardedElevator_ = 0;
    bool seatPending_ = false;
    float seat_[4] = {};
    uint32_t nextFloorZone_ = 0;
    render::Model buttonBase_, buttonTop_;
    float buttonBox_[6] = {-0.5f, -0.5f, 0.0f, 0.5f, 0.5f, 0.487f};
    uint32_t onButton_ = 0; // the switch we stand on (requestInteract sent)

    LevelSpec spec_;
    Room rooms_[kMaxRooms];
    int numRooms_ = 0;
    uint32_t elevatorUnresolvedMs_ = 0; // diagnostic log rate
    bool active_ = false;
    bool spawned_ = false;
    uint16_t winHood_ = 0;          // the facility (or factory) id whose hood a win teleports to
    // A floor (floor_): the facility, its pieces (rooms and hallways), door frames and the
    // floor collision of the .tlf, and the piece the toon is in.
    bool floor_ = false;
    uint32_t facilityDoId_ = 0, facilityZoneId_ = 0;
    static constexpr int kMaxPieces = 2 * kMaxRooms;
    struct Piece {
        float placement[12];
        float lo[3], hi[3];       // floor-space bounds
        int room = -1;            // rooms_ index, or -1 for a hallway
        int roomId = -1;          // the layout's room id (setRoomId), -1 for a hallway
        int model = -1;           // hallway: connectorModels_ index
    };
    Piece pieces_[kMaxPieces];
    int numPieces_ = 0;
    render::Model connectorModels_[4];
    char connectorNames_[4][48] = {};
    int numConnectors_ = 0;
    struct DoorFrame {
        float placement[12];
        int piece;
    };
    static constexpr int kMaxDoorFrames = 32;
    DoorFrame doorFrames_[kMaxDoorFrames];
    int numDoorFrames_ = 0;
    render::Model doorFrameModel_;
    CollisionWorld floorCollision_;
    struct FloorTri {
        uint16_t piece;
        float v[9];
    };
    FloorTri* floorTris_ = nullptr; // per piece (initFloorCollisions' mintFloorColl<roomNum>)
    int numFloorTris_ = 0;
    int piece_ = 0;                 // toonEnterRoom / camEnterRoom
    int showMin_ = 0, showMax_ = 0;
    bool interestSent_ = false;
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
    // This frame's moving / interactive solids (oriented boxes in an entity's local space).
    struct Solid {
        float world[12];
        float lo[3], hi[3];  // box; for a disc: radius hi[0] about local Y, band lo[1]..hi[1]
        float motion[3];  // world motion since last frame (carry)
        bool wall, floor;
        bool disc;        // a vertical gear: circle in local XZ, axis local Y
    };
    static constexpr int kMaxSolids = 96;
    Solid solids_[kMaxSolids];
    int numSolids_ = 0;
    DynamicSolids dynamic_;
    void addSolid(const float world[12], const float lo[3], const float hi[3], bool wall, bool floor);
    void addDisc(const float world[12], float radius, float y0, float y1);
    static bool floorThunk(void* self, float x, float y, float z, float* top);
    static void pushThunk(void* self, float* x, float* y, float z, float radius);
    static bool carryThunk(void* self, float x, float y, float z, float* dx, float* dy, float* dz);
    render::Model doorModels_[kDoorParts];
    float doorBox_[6] = {-6.6f, -0.35f, 0.0f, 6.8f, 0.4f, 14.2f};
    bool propsLoaded_ = false;
    uint32_t doorRequestMs_ = 0;
    const net::Client* doorClient_ = nullptr;
    uint32_t doorNowMs_ = 0;
    uint32_t doorNowMsPrev_ = 0;
    // ---- level games (world/level_games.*) ----
    friend class LevelGames;
    LevelGames games_;
    // ---- end level games ----
};

} // namespace tt::world
