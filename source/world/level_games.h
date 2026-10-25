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

// Laser hazard tile check: symbols 12 (symbolHiddenBomb) and 13 (symbolBomb) trigger alarm & damage.
inline bool isLaserHazard(uint8_t symbol) {
    return symbol == 12 || symbol == 13;
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

// Maze section rotation determinism from RNG seed (entId * roomDoId).
void shuffleMazeRotations(uint32_t seed, int outRotations[4]);

// Mole schedule generation:
struct MolePopup {
    float start;
    uint8_t hole;
    float upTime;
    float stayTime;
    float downTime;
    uint8_t type; // 0 = normal mole, 1 = cog bomb
};

int calcMoleSchedule(uint32_t seed, float gameDuration, int numMoles,
                     MolePopup* outSchedule, int maxSchedule);

// Mole elevation (0.0 underground, 1.0 fully raised).
inline float calcMoleElevation(float t, float start, float upTime, float stayTime, float downTime) {
    if (t < start) return 0.0f;
    const float elapsed = t - start;
    if (elapsed < upTime) {
        return upTime > 0.0f ? (elapsed / upTime) : 1.0f;
    }
    if (elapsed < upTime + stayTime) {
        return 1.0f;
    }
    if (elapsed < upTime + stayTime + downTime) {
        const float downElapsed = elapsed - (upTime + stayTime);
        return downTime > 0.0f ? (1.0f - (downElapsed / downTime)) : 0.0f;
    }
    return 0.0f;
}

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
    bool propsLoaded_ = false;

    // Models:
    render::Model crateWood_;
    render::Model crateCbWood_;
    render::Model gridFloor_;
    render::Model moleHole_;
    render::Model moleNorm_;
    render::Model moleCog_;
    render::Model moleHit_;
    render::Model golfHole_;
    render::Model golfBall_;
    render::Model goonCone_;

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
    audio::SfxId sfx_[kSfxCount] = {};

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
        uint32_t lastLaserHumMs = 0;

        // Camera
        float trackX = 0, trackY = 0;
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

        // Mole
        games_logic::MolePopup schedule[64];
        int scheduleCount = 0;
        uint32_t moleStartMs = 0;
        bool scheduled = false;
        bool moleTriggered = false;
        float holeElevation[16] = {};
        uint8_t holeType[16] = {};
        bool holeWhacked[16] = {};
        uint32_t whackedTimeMs[16] = {};

        // Golf
        bool joinedGolf = false;
        int currentSpot = -1;
    };
    SlotState slots_[net::kMaxLevelEntities] = {};

    const net::Client* client_ = nullptr;
    const Level* level_ = nullptr;
    uint32_t nowMs_ = 0;
    float toonX_ = 0, toonY_ = 0, toonZ_ = 0;
};

#endif // __3DS__

} // namespace tt::world
