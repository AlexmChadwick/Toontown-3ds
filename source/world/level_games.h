#include "world/level_draw_data.h"
// Facility room games runtime (docs/design/level_games.md):
//   1. Laser fields (DistributedLaserField) & Security cameras (DistributedSecurityCamera)
//   2. Crates (DistributedCrate) on grids (DistributedGrid) & Crushers (DistributedCrusherEntity)
//   3. Mazes (DistributedMaze)
//   4. Mole fields (DistributedMoleField)
//   5. Golf greens (DistributedGolfGreenGame)
// Owned by Level in source/world/level.*.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>

#ifdef __3DS__
#include "audio/audio.h"
#include "net/client.h"
#include "render/model.h"
#include "render/renderer.h"
#include "world/asset_batch.h"
#include "world/bust_a_cog.h"
#include <3ds.h>
#endif

namespace tt::world {

class Level;
struct LevelEntity;

namespace games_logic {

// Laser field tile indexing: localX in [0, scaleX], localY in [0, scaleY] -> tile [0, numX-1], [0, numY-1].
inline bool calcLaserTile(float localX, float localY, float scaleX, float scaleY,
                          uint8_t numX, uint8_t numY, int8_t* outTileX, int8_t* outTileY) {
    if (numX == 0 || numY == 0 || scaleX <= 0.0f || scaleY <= 0.0f) {
        if (outTileX) *outTileX = -1;
        if (outTileY) *outTileY = -1;
        return false;
    }
    if (localX < 0.0f || localX >= scaleX || localY < 0.0f || localY >= scaleY) {
        if (outTileX) *outTileX = -1;
        if (outTileY) *outTileY = -1;
        return false;
    }
    const float tileW = scaleX / float(numX);
    const float tileH = scaleY / float(numY);
    int tx = int(localX / tileW);
    int ty = int(localY / tileH);
    if (tx < 0) tx = 0;
    if (tx >= numX) tx = numX - 1;
    if (ty < 0) ty = 0;
    if (ty >= numY) ty = numY - 1;
    if (outTileX) *outTileX = static_cast<int8_t>(tx);
    if (outTileY) *outTileY = static_cast<int8_t>(ty);
    return true;
}

// Laser hazard tile check: gridSymbols 11 (symbolHiddenBomb) and 12 (symbolSkull) call sendFail.
inline bool isLaserHazard(uint8_t symbol) {
    return symbol == 11 || symbol == 12;
}

// setField's list: [gridNumX, gridNumY, then column-major tiles]. The symbol of tile (x, y), 0 if absent.
inline uint8_t laserSymbol(const uint8_t* data, int len, int x, int y) {
    if (len < 2) return 0;
    const int i = 2 + x * data[1] + y;
    return i < len ? data[i] : 0;
}

// Crate push side detection from contact normal in crate-local space:
// Upstream CrateNormals: 0: (1, 0, 0), 1: (-1, 0, 0), 2: (0, 1, 0), 3: (0, -1, 0).
inline int calcCratePushSide(float normalX, float normalY) {
    const float len = std::sqrt(normalX * normalX + normalY * normalY);
    if (len < 1e-4f) return -1;
    const float nx = normalX / len;
    const float ny = normalY / len;
    if (nx > 0.7071f) return 0;   // pushed against +X face
    if (nx < -0.7071f) return 1;  // pushed against -X face
    if (ny > 0.7071f) return 2;   // pushed against +Y face
    if (ny < -0.7071f) return 3;  // pushed against -Y face
    return -1;
}

// Crate linear position lerp over T_PUSH (1.5 s).
inline void calcCrateLerp(const float start[3], const float end[3], float t, float duration, float out[3]) {
    float u = duration > 0.0f ? t / duration : 1.0f;
    if (u < 0.0f) u = 0.0f;
    if (u > 1.0f) u = 1.0f;
    out[0] = start[0] + (end[0] - start[0]) * u;
    out[1] = start[1] + (end[1] - start[1]) * u;
    out[2] = start[2] + (end[2] - start[2]) * u;
}

// DistributedMaze.gotRoom: the H of Maze_Inside_0..3 from random.Random(entId * doId).
void shuffleMazeRotations(uint64_t seed, int outRotations[4]);

// MoleFieldBase hill types. The names are upstream's: a HILL_MOLE (mole_norm) throws the Toon
// that touches it (whackedBomb), a HILL_BOMB (mole_cog, the red Cog mole) is the one to stomp
// (whackedMole).
enum : uint8_t { kHillMole = 0, kHillBomb = 1, kHillWhacked = 2, kHillCogWhacked = 3, kHillNone = 255 };
constexpr int kMaxMoles = 64;     // numSquaresX * numSquaresY (the stock specs use 6 x 6)
constexpr int kMaxMolePopups = 320; // a 60 s game schedules about 230

struct MolePopup {
    float start;    // game seconds
    uint8_t hole;   // mole index, x fastest
    float upTime, stayTime, downTime;
    uint8_t type;   // kHillMole / kHillBomb
};

// MoleFieldBase.scheduleMoles for seed entId * level.doId; returns the popup count (0 when
// maxSchedule is too small). *outEndingTime: the last popup's end, in game seconds.
int calcMoleSchedule(uint64_t seed, double gameDuration, int numMoles, MolePopup* outSchedule, int maxSchedule,
                     double* outEndingTime = nullptr);

// __showToonHitByBomb's random.Random(timestamp) picks: the landing hill and the spin turns.
void moleFlightPick(int32_t ts, int numX, int numY, int* tileX, int* tileY, int* hRot, int* pRot);

// Golf tee-off spots in green space.
inline void calcGolfSpot(int spotIndex, float outPos[3]) {
    static const float kSpots[4][3] = {
        {3.0f, 13.0f, 0.0f},
        {6.0f, 13.0f, 0.0f},
        {-3.0f, 13.0f, 0.0f},
        {-6.0f, 13.0f, 0.0f}
    };
    const int idx = (spotIndex >= 0 && spotIndex < 4) ? spotIndex : 0;
    outPos[0] = kSpots[idx][0];
    outPos[1] = kSpots[idx][1];
    outPos[2] = kSpots[idx][2];
}

} // namespace games_logic

#ifdef __3DS__

class LevelGames {
public:
    void pumpAssets() {
        assetBatch_.pump();
    }
    bool loadingAssets() const {
        return assetBatch_.loading();
    }
    void loadProps();
    void shutdown();

    // Solids: adds oriented collision boxes for crates into Level::solids_.
    void addSolids(Level& level);

    // Update loop: runs every frame for facility level entities.
    void update(net::Client& client, Level& level, float toonX, float toonY, float toonZ, float dt, uint32_t nowMs);

    // Citro3D rendering pass.
    void draw(render::Renderer& renderer, const C3D_FVec& eye) const;

    // Autotest helpers:
    bool nearestLaserField(const Level& level, float x, float y, float z, float* outX, float* outY, float* outZ) const;
    bool nearestCamera(const Level& level, float x, float y, float z, float* outX, float* outY, float* outZ) const;
    bool nearestCrate(const Level& level, float x, float y, float z, float* outX, float* outY, float* outZ) const;
    bool nearestMaze(const Level& level, float x, float y, float z, float* outX, float* outY, float* outZ) const;
    bool nearestMoleField(const Level& level, float x, float y, float z, float* outX, float* outY, float* outZ) const;
    bool nearestGolfGreen(const Level& level, float x, float y, float z, float* outX, float* outY, float* outZ) const;

private:
    BasicAssetBatch<32> assetBatch_; // room-game props and the Bust-a-Cog board
    bool propsLoaded_ = false;

    // Models:
    render::Model crateWood_;
    render::Model crateCbWood_;
    render::Model moleHole_;
    render::Model moleNorm_;
    render::Model moleCog_;
    render::Model moleHit_;
    render::Model golfGreen_;
    render::Model golfFlag_;
    render::Model securityCamera_; // square_stomper, DistributedSecurityCamera's model
    render::Model gridFloor_;      // FloorWear, DistributedGrid's model

    float crateBox_[6] = {-2.8f, -2.8f, 0.0f, 2.8f, 2.8f, 5.48f};

    // SFX:
    enum SfxIndex {
        kSfxLBCapacitor = 0,
        kSfxLBSparks,
        kSfxLBCameraShutter,
        kSfxLBLaserHum,
        kSfxCrateEffort,
        kSfxCrateSliding,
        kSfxMoleSurprise,
        kSfxMoleStomp,
        kSfxTagC,
        kSfxCameraAlarm,
        kSfxCount
    };
    audio::SfxId sfx_[kSfxCount] = {}; // valid only while propsLoaded_

    void playAt(SfxIndex idx, const float pos[3], float vol = 1.0f);

    void updateLasers(net::Client& client, Level& level, float toonX, float toonY, float toonZ, uint32_t nowMs);
    void updateCameras(net::Client& client, Level& level, float toonX, float toonY, float toonZ, float dt, uint32_t nowMs);
    void updateCrates(net::Client& client, Level& level, float toonX, float toonY, float toonZ, float dt, uint32_t nowMs);
    void updateMazes(net::Client& client, Level& level, float toonX, float toonY, float toonZ, uint32_t nowMs);
    void updateMoles(net::Client& client, Level& level, float toonX, float toonY, float toonZ, float dt, uint32_t nowMs);
    void updateGolf(net::Client& client, Level& level, float toonX, float toonY, float toonZ, uint32_t nowMs);

    struct SlotState {
        const LevelEntity *drawEntity = nullptr;
        uint32_t drawRevision = 0;
        LevelDrawData draw;
        // Laser
        int8_t lastTileX = -1, lastTileY = -1;
        bool toonInLaser = false;
        bool laserInRange = false; // __detect's doToonInRange
        bool laserCam = false;     // ... with the field active then (setIdealCameraPos)
        bool successPlayed = false;
        uint8_t seenField[2 + 8 * 8] = {};
        uint8_t seenLen = 0;
        uint32_t lastLaserHumMs = 0;
        // genGrid only runs while activeLF: the drawn grid stays as it was when the field went off.
        uint8_t shownField[2 + 8 * 8] = {};
        uint8_t shownLen = 0;
        int8_t shownToonX = -1, shownToonY = -1;

        // Camera
        bool camTracking = false; // __updateTrack has run (it needs a target)
        bool hideModel = false;
        float projector[3] = {0.0f, 0.0f, 25.0f};
        float toonSpotDist = 100.0f;
        float trackX = 10.0f, trackY = -5.0f;
        float vX = 0, vY = 0;
        uint32_t lastCameraAlarmMs = 0;

        // Crate
        float curPos[3] = {};
        float prevPos[3] = {};
        float moveProgress = 1.0f;
        bool pushing = false;
        uint32_t pushStartMs = 0;
        int pushSide = -1;

        // Maze
        bool mazeTriggered = false;
        bool mazeFinished = false;

        // Golf
        bool golfInSphere = false; // inside the 11.5 sphere (requestJoin on entry)
        bool golfEntered = false; // hasEntered: the instruction shows once
        uint8_t golfJoinSeq = 0, golfDoneSeq = 0;
        uint8_t bacBoardSeq = 0, bacClearedSeq = 0, bacHelpSeq = 0, bacGagSeq = 0;
    };
    SlotState slots_[net::kMaxLevelEntities] = {};

    // ---- DistributedMoleField / MoleHill ----
    // A hill's running interval (MoleHill.popIval / downIval), evaluated from its start time.
    enum MoleIval : uint8_t { kIvalNone, kIvalPop, kIvalDown, kIvalWhacked, kIvalCogWhacked };
    struct MoleHill {
        uint8_t type = games_logic::kHillNone; // hillType
        uint8_t ival = kIvalNone;
        bool bombBranch = false; // doMolePop's HILL_BOMB sequence: collision for the whole pop
        bool col = false;        // moleColNodePath unstashed
        int16_t popupNum = 0;
        uint32_t ivalMs = 0;
        float up = 0, stay = 0, down = 0;
        float fromZ = -2.5f; // the mole's Z when the interval began
        float z = -2.5f;     // self.mole Z (hill space)
        float headOff[3] = {}, headScale = 1;
        float colRadius = 0.75f; // the 1.0 sphere x moleColNodePath scale x the mole's 0.75
    };
    struct MoleField {
        int slot = -1; // levelEntitySlot index; -1 = free
        uint32_t doId = 0;
        int numX = 0, numY = 0, numMoles = 0;
        float spacingX = 0, spacingY = 0;
        float world[12] = {};
        uint8_t startSeq = 0;
        uint16_t eventSeq = 0;
        bool gameStarted = false, hasRestarted = false, hasEntered = false, activeField = true;
        bool inRange = false, won = false;
        uint32_t gameStartMs = 0, detectMs = 0, timerEndMs = 0;
        int16_t lastScore = -1;
        int count = 0, next = 0;
        games_logic::MolePopup schedule[games_logic::kMaxMolePopups];
        MoleHill hills[games_logic::kMaxMoles];
    };
    static constexpr int kMaxMoleFields = 3;
    MoleField moleFields_[kMaxMoleFields];
    MoleField* moleFieldFor(int slot, const net::LevelEntityObj& d, const LevelEntity& e, const Level& level);
    void molePop(MoleHill& h, const games_logic::MolePopup& p, uint32_t startMs);
    void moleSetType(MoleHill& h, uint8_t type, uint32_t nowMs);
    void moleEval(MoleHill& h, uint32_t nowMs);
    void moleHitByBomb(MoleField& f, int index, int32_t ts, uint32_t nowMs);
    void moleInfo(const char* text, uint32_t nowMs);
    // ---- end mole field ----

    // ---- DistributedMaze (bake_level.maze_parts: <level>.tmz + section / hedge models) ----
    struct MazeRoom {
        int room = -1;  // Level room index; -1 = no maze on this floor
        int numSections = 0, numBlocks = 0;
        float section[4][12] = {}; // section -> room, H 0
        char blockName[4][16] = {};
        float block[4][12] = {};
        float entrance[3] = {}, finish[3] = {}; // room space
        int numHints = 0;
        float hints[16][3] = {};
        int rotation[4] = {};        // degrees, from the maze's random.Random(entId * doId)
        bool blockShown[4] = {true, true, true, true};
        uint32_t mazeDoId = 0;       // the DistributedMaze the layout came from
        uint8_t startSeq = 0;
        uint16_t eventSeq = 0;
        bool gameStarted = false, finished = false, timedOut = false, entered = false, inHint = false;
        uint32_t timerEndMs = 0;
    };
    MazeRoom maze_;
    render::Model mazeSectionModel_[4], mazeBlockModel_[4];
    void applyMaze(Level& level, const net::LevelEntityObj& d);
    // ---- end maze ----

public:
    // Level::loadRoom: a room with <level>.tmz is a hedge maze room.
    void loadMaze(int room, const char* level);
    void resetMaze();
    // The maze's dead-end hint spheres (__handleToonEnterHint): the camera at (0, -22 h / 3, h + 54).
    bool mazeHintCamera() const { return maze_.inHint; }
    // Test-only: the maze's ENTRANCE (0), finish (1) or first dead-end hint (2) in world space.
    bool mazePoint(int which, float out[3]) const;
    // The game's frame2D label (Moles Left: n / Maze Race!) and its ToontownTimer; false = hidden.
    bool gameHud(const char** label, int* timerSeconds, uint32_t nowMs) const;

    // The local Toon thrown by a mole (DistributedMoleField.__showToonHitByBomb): main.cpp plays
    // it while active and leaves the Toon at end.
    struct MoleFlight {
        bool active = false;
        uint32_t startMs = 0;
        float start[3] = {}, end[3] = {};
        int hRot = 0, pRot = 0; // whole turns about H and P over the flight
        float camEye[3] = {};   // the camera, which rides the flight's x / y and looks at the Toon
    };
    static constexpr float kMoleFlightWait = 0.25f, kMoleFlightDur = 3.125f; // Trajectory(0, 0, 50), g 32
    const MoleFlight& moleFlight() const { return moleFlight_; }
    void endMoleFlight() { moleFlight_.active = false; }
    // While the Toon is in a mole field's range (setUpCamera): the ideal camera is
    // (0, -22 * h / 3, h + 12) for clamped Toon height h.
    bool moleCamera() const { return moleCamera_; }
    // While the Toon is near an active laser field (doToonInRange): (0, -20 * h / 3, h + 8).
    bool laserCamera() const { return laserCamera_; }
    // Bust-a-Cog (the golf green's board): the pad turns the aim, A shoots, B is Exit for Now.
    // main.cpp passes the frame's input before update and, while playing, holds the Toon at
    // its stand point (the 'stopped' state) and takes the board camera.
    void setGolfInput(float aim, bool fire, bool exit) { golfAim_ = aim, golfFire_ = fire, golfExit_ = exit; }
    bool golfPlaying() const { return bac_.active(); }
    // Autotest only: report the current board solved (requestBoard(1)) on the next update.
    void autotestSolveBoard() { golfSolve_ = true; }
    // The camera eye / target and the Toon's stand point (toonPoints[index], facing H 180).
    bool golfView(float eye[3], float target[3], float stand[3], float* heading) const;
    void setCameraEye(const float eye[3]) { std::memcpy(cameraEye_, eye, sizeof(cameraEye_)); }
    // Autotest: the world position of a raised mole with its collision on; cog = a HILL_BOMB
    // (the one to stomp), else a HILL_MOLE (it throws the Toon).
    bool moleTarget(bool cog, float out[3]) const;
    // The country club's showInfoText (sign font, 3.1 s then a 0.5 s fade): text and alpha now.
    const char* infoText(uint32_t nowMs, float* alpha) const;
    // Bust-a-Cog's bonusBoard for its 4 s: informGag ("You won a Cake!" over the gag icon, track /
    // level) or helpOthers ("<name> solved a Puzzle!" over the fire ball, track -1).
    const char* bonusBoard(uint32_t nowMs, int* track, int* level) const;

private:
    MoleFlight moleFlight_;
    bool moleCamera_ = false;
    bool laserCamera_ = false;
    BustACog bac_;
    int golfSlot_ = -1;     // the green whose board we play
    float golfAim_ = 0;
    float dt_ = 1.0f / 30.0f;
    bool golfFire_ = false, golfExit_ = false, golfSolve_ = false;
    float cameraEye_[3] = {};
    char infoText_[64] = {};
    mutable char hudLabel_[32] = {};
    // A maze timeout (setGameOver while not finished): main.cpp puts the Toon at the finish.
    bool hasMazeWarp_ = false;
    float mazeWarp_[3] = {};

public:
    bool popMazeWarp(float out[3]) {
        if (!hasMazeWarp_) return false;
        hasMazeWarp_ = false;
        std::memcpy(out, mazeWarp_, sizeof(mazeWarp_));
        return true;
    }

private:
    uint32_t infoMs_ = 0;
    char bonusText_[64] = {};
    int bonusTrack_ = -1, bonusLevel_ = 0;
    uint32_t bonusMs_ = 0;

    const net::Client* client_ = nullptr;
    const Level* level_ = nullptr;
    uint32_t nowMs_ = 0;
    float toonX_ = 0, toonY_ = 0, toonZ_ = 0;
};

#endif // __3DS__

} // namespace tt::world
