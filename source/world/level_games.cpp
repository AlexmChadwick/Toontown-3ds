#include "world/level_games.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "world/py_random.h"

#ifdef __3DS__
#include "core/log.h"
#include "world/laser_symbols.h"
#include "world/level.h"
#endif

namespace tt::world {

namespace games_logic {

void shuffleMazeRotations(uint64_t seed, int outRotations[4]) {
    // DistributedMaze.gotRoom: getRng().shuffle(rotations), then each Maze_Inside_<i> found
    // takes rotations.pop(), the end of the list.
    int rotations[8] = {0, 0, 90, 90, 180, 180, 270, 270};
    PyRandom rng(seed);
    rng.shuffle(rotations, 8);
    outRotations[0] = rotations[7];
    outRotations[1] = rotations[6];
    outRotations[2] = rotations[5];
    outRotations[3] = rotations[4];
}

int calcMoleSchedule(uint64_t seed, double gameDuration, int numMoles, MolePopup* outSchedule, int maxSchedule,
                     double* outEndingTime) {
    // MoleFieldBase.scheduleMoles. getRng() is a fresh random.Random(entId * level.doId) on every
    // call, so the first shuffle and each reshuffle of the used list restart that sequence;
    // randOb, the mole type choice, is one more fresh copy that runs through the whole loop.
    if (!outSchedule || maxSchedule <= 0 || numMoles <= 0 || numMoles > kMaxMoles) return 0;
    uint8_t eligible[kMaxMoles], used[kMaxMoles];
    int numEligible = numMoles, numUsed = 0;
    for (int i = 0; i < numMoles; ++i) eligible[i] = uint8_t(i);
    PyRandom(seed).shuffle(eligible, numEligible);
    PyRandom randOb(seed);
    double curMoveUpTime = 1, curMoveDownTime = 1, curTimeBetweenPopup = 1.5, curStayUpTime = 7, curTime = 3;
    double endingTime = 0;
    int count = 0;
    while (endingTime < gameDuration) {
        if (numEligible == 0) {
            for (int i = 0; i < numUsed; ++i) eligible[i] = used[i];
            numEligible = numUsed;
            numUsed = 0;
            PyRandom(seed).shuffle(eligible, numEligible);
        }
        const uint8_t moleIndex = eligible[0];
        for (int i = 1; i < numEligible; ++i) eligible[i - 1] = eligible[i];
        --numEligible;
        used[numUsed++] = moleIndex;
        static constexpr uint8_t kTypes[4] = {kHillMole, kHillMole, kHillMole, kHillBomb};
        const uint8_t moleType = kTypes[randOb.choiceIndex(4)];
        if (count >= maxSchedule) return 0; // the caller's table is too small for this game
        outSchedule[count++] = MolePopup{float(curTime), moleIndex, float(curMoveUpTime), float(curStayUpTime),
                                         float(curMoveDownTime), moleType};
        curTime += curTimeBetweenPopup;
        // calcNextMoveUpTime clamps to MoveDownTimeMin; both minimums are 0.5.
        curMoveUpTime = std::max(0.5, curMoveUpTime * 0.95);
        curStayUpTime = std::max(3.0, curStayUpTime * 0.95);
        curMoveDownTime = std::max(0.5, curMoveDownTime * 0.95);
        curTimeBetweenPopup = std::max(0.25, curTimeBetweenPopup * 0.95);
        endingTime = curTime + curMoveUpTime + curStayUpTime + curMoveDownTime;
    }
    --count; // self.schedule.pop()
    if (outEndingTime && count > 0) {
        const MolePopup& last = outSchedule[count - 1];
        *outEndingTime = double(last.start) + last.upTime + last.stayTime + last.downTime;
    }
    return count;
}

void moleFlightPick(int32_t ts, int numX, int numY, int* tileX, int* tileY, int* hRot, int* pRot) {
    // random.Random(timestamp): the landing hill, then the H and P spins.
    PyRandom rng(uint64_t(ts < 0 ? -int64_t(ts) : int64_t(ts)));
    *tileX = rng.randint(0, numX - 1);
    *tileY = rng.randint(0, numY - 1);
    *hRot = rng.randrange(1, 8);
    if (rng.choiceIndex(2)) *hRot = -*hRot;
    *pRot = rng.randrange(1, 3);
    if (rng.choiceIndex(2)) *pRot = -*pRot;
}

} // namespace games_logic

#ifdef __3DS__

namespace {

constexpr const char* kPropsDir = "sdmc:/3ds/toontown3ds/data/level/props/";
constexpr const char* kAudioDir = "sdmc:/3ds/toontown3ds/data/audio/";
constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegToRad = kPi / 180.0f;
// GravityWalker's event sphere: centre avatarRadius - 0.1 up, radius avatarRadius * 1.04.
constexpr float kToonEventZ = 1.3f, kToonEventR = 1.456f;

// Math helpers
inline void toMtxLocal(const float w[12], C3D_Mtx* m) {
    Mtx_Identity(m);
    for (int r = 0; r < 3; ++r) {
        m->r[r].x = w[0 + r], m->r[r].y = w[3 + r], m->r[r].z = w[6 + r], m->r[r].w = w[9 + r];
    }
}

inline void toLocalLocal(const float w[12], float x, float y, float z, float out[3]) {
    const float d[3] = {x - w[9], y - w[10], z - w[11]};
    for (int r = 0; r < 3; ++r) {
        const float* axis = w + r * 3;
        const float len2 = axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2];
        out[r] = len2 > 1e-12f ? (d[0] * axis[0] + d[1] * axis[1] + d[2] * axis[2]) / len2 : 0.0f;
    }
}

inline void fromLocalLocal(const float w[12], const float p[3], float out[3]) {
    for (int c = 0; c < 3; ++c) out[c] = p[0] * w[c] + p[1] * w[3 + c] + p[2] * w[6 + c] + w[9 + c];
}

// Vertex-coloured additive geometry for Renderer::drawAdditive, flushed when full (main thread).
struct AddBuilder {
    static constexpr int kVerts = 2048, kIndices = 6144;
    float xyz[kVerts][3];
    u8 rgba[kVerts][4];
    u16 idx[kIndices];
    int nv = 0, ni = 0;
    render::Renderer* renderer = nullptr;
    C3D_Mtx world;
    bool twoSided = true;

    void begin(render::Renderer& r, const C3D_Mtx& m, bool twoSidedDraw) {
        renderer = &r;
        world = m;
        twoSided = twoSidedDraw;
        nv = ni = 0;
    }
    void flush() {
        if (ni) renderer->drawAdditive(world, xyz, rgba, nv, idx, ni, twoSided);
        nv = ni = 0;
    }
    void reserve(int v, int i) {
        if (nv + v > kVerts || ni + i > kIndices) flush();
    }
    u16 vert(float x, float y, float z, const float c[3]) {
        xyz[nv][0] = x, xyz[nv][1] = y, xyz[nv][2] = z;
        for (int k = 0; k < 3; ++k) rgba[nv][k] = u8(std::clamp(c[k], 0.0f, 1.0f) * 255.0f + 0.5f);
        rgba[nv][3] = 255;
        return u16(nv++);
    }
    void tri(u16 a, u16 b, u16 c) {
        idx[ni++] = a, idx[ni++] = b, idx[ni++] = c;
    }
    // A wireframe line (setRenderModeWireframe) as a flat strip `w` wide at height z.
    void line(float ax, float ay, float bx, float by, float z, float w, const float c[3]) {
        float nx = ay - by, ny = bx - ax;
        const float len = std::sqrt(nx * nx + ny * ny);
        if (len < 1e-5f) return;
        nx *= 0.5f * w / len, ny *= 0.5f * w / len;
        reserve(4, 6);
        const u16 i0 = vert(ax + nx, ay + ny, z, c), i1 = vert(ax - nx, ay - ny, z, c);
        const u16 i2 = vert(bx - nx, by - ny, z, c), i3 = vert(bx + nx, by + ny, z, c);
        tri(i0, i1, i2);
        tri(i0, i2, i3);
    }
};
AddBuilder gAdd;

// DistributedLaserField.createGrid for one symbol outline in tile (x, y): the wire (gridColor *
// 0.25) and the beams from the projector (black there, gridColor * 0.04 on the floor).
void addLaserSymbol(const laser::Symbol& sym, float left, float bottom, float tileW, float tileH,
                    const float projector[3], float wireW) {
    constexpr float kZ = 0.05f; // zFloat
    if (sym.count < 2) return;
    const float wire[3] = {sym.color[0] * 0.25f, sym.color[1] * 0.25f, sym.color[2] * 0.25f};
    const float beam[3] = {sym.color[0] * 0.04f, sym.color[1] * 0.04f, sym.color[2] * 0.04f};
    static const float kBlack[3] = {0, 0, 0};
    for (int i = 0; i + 1 < sym.count; ++i) {
        const float* a = laser::kPoints[sym.first + i];
        const float* b = laser::kPoints[sym.first + i + 1];
        gAdd.line(left + a[0] * tileW, bottom + a[1] * tileH, left + b[0] * tileW, bottom + b[1] * tileH, kZ, wireW,
                  wire);
    }
    for (int i = 0; i + 1 < sym.count; ++i) {
        const float* a = laser::kPoints[sym.first + i];
        const float* b = laser::kPoints[sym.first + i + 1];
        gAdd.reserve(3, 3);
        const u16 p = gAdd.vert(projector[0], projector[1], projector[2], kBlack);
        const u16 ia = gAdd.vert(left + a[0] * tileW, bottom + a[1] * tileH, kZ, beam);
        const u16 ib = gAdd.vert(left + b[0] * tileW, bottom + b[1] * tileH, kZ, beam);
        gAdd.tri(p, ia, ib);
    }
}

// TTLocalizer's LaserGame* for setGridGame's name (DistributedLaserField.setGridGame).
const char* laserGameText(const char* game) {
    if (!std::strcmp(game, "MineSweeper")) return "Skull Finder!";
    if (!std::strcmp(game, "Roll")) return "Matching";
    if (!std::strcmp(game, "Avoid")) return "Avoid the Skulls";
    if (!std::strcmp(game, "Drag")) return "Drag three of a color in a row";
    return "Unknown Game";
}

} // namespace

void LevelGames::loadProps() {
    if (propsLoaded_) return;
    propsLoaded_ = true;

    char path[128];

    // Models:
    snprintf(path, sizeof(path), "%scrate_wood.ttm", kPropsDir);
    assetBatch_.load(crateWood_, path);
    snprintf(path, sizeof(path), "%scrate_cbwood.ttm", kPropsDir);
    assetBatch_.load(crateCbWood_, path);

    snprintf(path, sizeof(path), "%sgrid_floor.ttm", kPropsDir);
    assetBatch_.load(gridFloor_, path);

    snprintf(path, sizeof(path), "%smole_hole.ttm", kPropsDir);
    assetBatch_.load(moleHole_, path);
    snprintf(path, sizeof(path), "%smole_norm.ttm", kPropsDir);
    assetBatch_.load(moleNorm_, path);
    snprintf(path, sizeof(path), "%smole_cog.ttm", kPropsDir);
    assetBatch_.load(moleCog_, path);
    snprintf(path, sizeof(path), "%smole_hit.ttm", kPropsDir);
    assetBatch_.load(moleHit_, path);

    snprintf(path, sizeof(path), "%sgolf_green.ttm", kPropsDir);
    assetBatch_.load(golfGreen_, path);
    snprintf(path, sizeof(path), "%sgolf_flag.ttm", kPropsDir);
    assetBatch_.load(golfFlag_, path);
    snprintf(path, sizeof(path), "%ssecurity_camera.ttm", kPropsDir);
    assetBatch_.load(securityCamera_, path);

    // Crate box
    snprintf(path, sizeof(path), "%scrate.box", kPropsDir);
    if (FILE* f = fopen(path, "r")) {
        float b[6];
        if (fscanf(f, "%f %f %f %f %f %f", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) == 6) {
            memcpy(crateBox_, b, sizeof(crateBox_));
        }
        fclose(f);
    }

    // Audio SFX:
    static const char* const kSfxNames[kSfxCount] = {
        "LB_capacitor_discharge_3",
        "LB_sparks_1",
        "LB_camera_shutter_2",
        "LB_laser_beam_hum_2",
        "CHQ_FACT_crate_effort",
        "CHQ_FACT_crate_sliding",
        "Mole_Surprise",
        "Mole_Stomp",
        "MG_Tag_C",
        "CHQ_GOON_tractor_beam_alarmed"
    };

    for (int k = 0; k < kSfxCount; ++k) {
        snprintf(path, sizeof(path), "%s%s.tsnd", kAudioDir, kSfxNames[k]);
        sfx_[k] = audio::loadSfx(path);
    }
}

void LevelGames::shutdown() {
    assetBatch_.clear();
    propsLoaded_ = false;
    // loadProps loads these again on the next facility: free them, or each visit leaks
    // kSfxCount of the kMaxSfx audio slots.
    for (audio::SfxId& id : sfx_) {
        if (id != audio::kInvalidSfx) audio::freeSfx(id);
        id = audio::kInvalidSfx;
    }
    crateWood_.unload();
    crateCbWood_.unload();
    moleHole_.unload();
    moleNorm_.unload();
    moleCog_.unload();
    moleHit_.unload();
    golfGreen_.unload();
    golfFlag_.unload();
    securityCamera_.unload();
    gridFloor_.unload();
    laserCamera_ = false;
    resetMaze();
    for (MoleField& f : moleFields_) f.slot = -1;
    moleFlight_.active = false;
    infoMs_ = 0;
    for (auto& s : slots_) s = SlotState{};
}

void LevelGames::playAt(SfxIndex idx, const float pos[3], float vol) {
    (void)vol;
    if (!propsLoaded_ || idx >= kSfxCount || sfx_[idx] == audio::kInvalidSfx) return;
    const float dx = pos[0] - toonX_, dy = pos[1] - toonY_, dz = pos[2] - toonZ_;
    const float dist2 = dx * dx + dy * dy + dz * dz;
    if (dist2 > 120.0f * 120.0f) return;
    audio::playSfx(sfx_[idx]);
}

void LevelGames::addSolids(Level& level) {
    if (!client_) return;

    for (int i = 0; i < net::kMaxLevelEntities; ++i) {
        const net::LevelEntityObj& d = client_->levelEntitySlot(i);
        if (!d.active || d.classId != dc::DistributedCrate::kClassId) continue;
        const LevelEntity* e = level.specOf(d.levelDoId, d.entId);
        if (!e || !level.shown(e->room)) continue;

        SlotState& slot = slots_[i];
        const float scale = level.spec_.number(*e, "scale", 1.0f);

        // Compute crate's dynamic world transform with current lerp position:
        float crateWorld[12];
        memcpy(crateWorld, e->world, sizeof(crateWorld));
        // Add dynamic offset in entity space
        float localOffset[3] = {slot.curPos[0], slot.curPos[1], slot.curPos[2]};
        float worldPos[3];
        fromLocalLocal(e->world, localOffset, worldPos);
        crateWorld[9] = worldPos[0];
        crateWorld[10] = worldPos[1];
        crateWorld[11] = worldPos[2];

        float lo[3] = {crateBox_[0] * scale, crateBox_[1] * scale, crateBox_[2] * scale};
        float hi[3] = {crateBox_[3] * scale, crateBox_[4] * scale, crateBox_[5] * scale};

        level.addSolid(crateWorld, lo, hi, true, true);

        // If moving, carry toon standing on top:
        if (slot.moveProgress < 1.0f && level.numSolids_ > 0) {
            Level::Solid& last = level.solids_[level.numSolids_ - 1];
            float deltaPos[3] = {slot.curPos[0] - slot.prevPos[0], slot.curPos[1] - slot.prevPos[1], slot.curPos[2] - slot.prevPos[2]};
            last.motion[0] = deltaPos[0];
            last.motion[1] = deltaPos[1];
            last.motion[2] = deltaPos[2];
        }
    }
}

void LevelGames::update(net::Client& client, Level& level, float toonX, float toonY, float toonZ, float dt, uint32_t nowMs) {
    client_ = &client;
    level_ = &level;
    toonX_ = toonX;
    toonY_ = toonY;
    toonZ_ = toonZ;
    nowMs_ = nowMs;

    for (int index = 0; index < net::kMaxLevelEntities; ++index) {
        const auto &object = client.levelEntitySlot(index);
        const auto *entity = object.active ? level.specOf(object.levelDoId, object.entId) : nullptr;
        auto &slot = slots_[index];
        if (entity && (slot.drawEntity != entity || slot.drawRevision != level.spec_.revision())) {
            slot.draw = prepareLevelDraw(level.spec_, *entity);
            slot.drawEntity = entity;
            slot.drawRevision = level.spec_.revision();
        }
    }

    addSolids(level);

    updateLasers(client, level, toonX, toonY, toonZ, nowMs);
    updateCameras(client, level, toonX, toonY, toonZ, dt, nowMs);
    updateCrates(client, level, toonX, toonY, toonZ, dt, nowMs);
    updateMazes(client, level, toonX, toonY, toonZ, nowMs);
    updateMoles(client, level, toonX, toonY, toonZ, dt, nowMs);
    updateGolf(client, level, toonX, toonY, toonZ, nowMs);
}

void LevelGames::updateLasers(net::Client& client, Level& level, float toonX, float toonY, float toonZ, uint32_t nowMs) {
    bool camera = false;
    for (int i = 0; i < net::kMaxLevelEntities; ++i) {
        const net::LevelEntityObj& d = client.levelEntitySlot(i);
        if (!d.active || d.classId != dc::DistributedLaserField::kClassId) continue;
        const LevelEntity* e = level.specOf(d.levelDoId, d.entId);
        if (!e || !level.shown(e->room)) continue;

        SlotState& slot = slots_[i];
        const float scaleX = level.spec_.number(*e, "gridScaleX", 2.0f);
        const float scaleY = level.spec_.number(*e, "gridScaleY", 2.0f);
        // setField carries the grid size too (it calls setGrid when it differs).
        const int len = d.fieldDataLen;
        const uint8_t numX = len >= 2 ? d.fieldData[0] : d.gridNumX;
        const uint8_t numY = len >= 2 ? d.fieldData[1] : d.gridNumY;

        float local[3];
        toLocalLocal(e->world, toonX, toonY, toonZ, local);

        // __detect: within 0.75 of the larger side of the field's centre.
        const float cx = local[0] - scaleX * 0.5f, cy = local[1] - scaleY * 0.5f;
        const float greater = scaleX > scaleY ? scaleX : scaleY;
        const bool inRange = std::sqrt(cx * cx + cy * cy + local[2] * local[2]) < greater * 0.75f;
        if (inRange && !slot.laserInRange) {
            slot.laserInRange = true;
            slot.laserCam = d.activeLF;
            if (d.activeLF) moleInfo(laserGameText(d.gridGame), nowMs); // stage.showInfoText
        } else if (!inRange && slot.laserInRange) {
            slot.laserInRange = false;
            slot.laserCam = false;
        }
        if (slot.laserInRange && slot.laserCam) camera = true;

        int8_t tileX = -1, tileY = -1;
        const bool inside = inRange && games_logic::calcLaserTile(local[0], local[1], scaleX, scaleY, numX, numY,
                                                                  &tileX, &tileY);
        if (inside) {
            if (d.activeLF && nowMs - slot.lastLaserHumMs > 3000) {
                playAt(kSfxLBLaserHum, e->world + 9, 0.4f);
                slot.lastLaserHumMs = nowMs;
            }
            // __toonHit: the tile's function (sendFail on a skull), then hit; setField's
            // __testTile runs the function again when the field changes under the Toon.
            const bool moved = tileX != slot.lastTileX || tileY != slot.lastTileY || !slot.toonInLaser;
            const bool fieldChanged = len != slot.seenLen || std::memcmp(d.fieldData, slot.seenField, len);
            if (moved || fieldChanged) {
                if (games_logic::isLaserHazard(games_logic::laserSymbol(d.fieldData, len, tileX, tileY))) {
                    playAt(kSfxLBSparks, e->world + 9, 0.9f);
                    TT_LOG("level_games: laser field %u hazard on tile (%d, %d)", unsigned(d.doId), int(tileX),
                           int(tileY));
                    client.sendLaserTrapFire(d.doId);
                }
            }
            if (moved) {
                const int8_t oldX = slot.lastTileX;
                const int8_t oldY = slot.lastTileY;
                slot.lastTileX = tileX;
                slot.lastTileY = tileY;
                slot.toonInLaser = true;
                TT_LOG("level_games: laser field %u stepped on tile (%d, %d) (sending hit)", unsigned(d.doId),
                       int(tileX), int(tileY));
                client.sendLaserHit(d.doId, tileX, tileY, oldX, oldY);
            }
        } else if (slot.toonInLaser) {
            slot.toonInLaser = false;
            slot.lastTileX = -1;
            slot.lastTileY = -1;
        }
        const int seen = len < int(sizeof(slot.seenField)) ? len : int(sizeof(slot.seenField));
        std::memcpy(slot.seenField, d.fieldData, seen);
        slot.seenLen = uint8_t(seen);

        // genGrid only while activeLF: the drawn field freezes when the AI turns it off.
        if (d.activeLF) {
            std::memcpy(slot.shownField, slot.seenField, seen);
            slot.shownLen = uint8_t(seen);
            slot.shownToonX = slot.toonInLaser ? slot.lastTileX : -1;
            slot.shownToonY = slot.toonInLaser ? slot.lastTileY : -1;
        }

        // On puzzle solved
        if (d.success == 1 && !slot.successPlayed) {
            playAt(kSfxLBCapacitor, e->world + 9, 0.9f);
            slot.successPlayed = true;
        }
    }
    laserCamera_ = camera;
}

void LevelGames::updateCameras(net::Client& client, Level& level, float toonX, float toonY, float toonZ, float dt, uint32_t nowMs) {
    for (int i = 0; i < net::kMaxLevelEntities; ++i) {
        const net::LevelEntityObj& d = client.levelEntitySlot(i);
        if (!d.active || d.classId != dc::DistributedSecurityCamera::kClassId) continue;
        const LevelEntity* e = level.specOf(d.levelDoId, d.entId);
        if (!e || !level.shown(e->room)) continue;

        SlotState& slot = slots_[i];
        const float radius = level.spec_.number(*e, "radius", 5.0f);
        const float accel = level.spec_.number(*e, "accel", 0.5f);
        const float maxVel = level.spec_.number(*e, "maxVel", 1.0f);
        if (level.spec_.getFloats(*e, "projector", slot.projector, 3) < 3) {
            slot.projector[0] = slot.projector[1] = 0.0f;
            slot.projector[2] = 25.0f;
        }
        slot.hideModel = level.spec_.integer(*e, "hideModel", 0) != 0;

        // setTarget: targetHash % 3 counts through the trackTarget1..3 that are set.
        uint32_t targets[3];
        int numTargets = 0;
        static const char* const kTrack[3] = {"trackTarget1", "trackTarget2", "trackTarget3"};
        for (const char* name : kTrack) {
            const int32_t id = level.spec_.integer(*e, name, 0);
            if (id) targets[numTargets++] = uint32_t(id);
        }
        // __updateTrack does nothing (and draws no beam) until there is a target.
        if (!d.targetSet || numTargets == 0) continue;
        const LevelEntity* t = level.specOf(d.levelDoId, targets[(d.targetHash % 3) % numTargets]);
        if (!t) continue;
        float target[3];
        toLocalLocal(e->world, t->world[9], t->world[10], t->world[11], target);
        slot.camTracking = true;

        const float distX = target[0] - slot.trackX;
        const float distY = target[1] - slot.trackY;
        const float trigDist = std::sqrt(distX * distX + distY * distY);
        const float totalDist = std::abs(distX) + std::abs(distY);
        const float propX = std::abs(distX) / (totalDist + 0.01f);
        const float propY = std::abs(distY) / (totalDist + 0.01f);
        const float dirX = distX != 0.0f ? (distX / std::abs(distX)) : 0.0f;
        const float dirY = distY != 0.0f ? (distY / std::abs(distY)) : 0.0f;

        if (trigDist < radius * 0.5f + 1.0f) {
            float deccel = 1.0f - (dt * 7.0f);
            if (deccel < 0.0f) deccel = 0.0f;
            slot.vX *= deccel;
            slot.vY *= deccel;
        } else {
            slot.vX += dirX * accel * propX;
            slot.vY += dirY * accel * propY;
        }

        slot.vX = std::clamp(slot.vX, -maxVel, maxVel);
        slot.vY = std::clamp(slot.vY, -maxVel, maxVel);
        slot.trackX += slot.vX * dt;
        slot.trackY += slot.vY * dt;

        // getDist: the Toon's distance from the spot in the camera's x / y.
        float localToon[3];
        toLocalLocal(e->world, toonX, toonY, toonZ, localToon);
        const float dx = localToon[0] - slot.trackX;
        const float dy = localToon[1] - slot.trackY;
        slot.toonSpotDist = std::sqrt(dx * dx + dy * dy);

        if (slot.toonSpotDist < radius && (!slot.lastCameraAlarmMs || nowMs - slot.lastCameraAlarmMs > 2000)) {
            slot.lastCameraAlarmMs = nowMs;
            playAt(kSfxCameraAlarm, e->world + 9, 0.9f);
            playAt(kSfxLBCameraShutter, e->world + 9, 0.8f);
            client.sendCameraTrapFire(d.doId);
        }
    }
}

void LevelGames::updateCrates(net::Client& client, Level& level, float toonX, float toonY, float toonZ, float dt, uint32_t nowMs) {
    (void)dt;
    for (int i = 0; i < net::kMaxLevelEntities; ++i) {
        const net::LevelEntityObj& d = client.levelEntitySlot(i);
        if (!d.active || d.classId != dc::DistributedCrate::kClassId) continue;
        const LevelEntity* e = level.specOf(d.levelDoId, d.entId);
        if (!e || !level.shown(e->room)) continue;

        SlotState& slot = slots_[i];
        slot.prevPos[0] = slot.curPos[0];
        slot.prevPos[1] = slot.curPos[1];
        slot.prevPos[2] = slot.curPos[2];

        // Moving lerp (setMoveTo)
        if (d.moving && d.moveStartMs > 0) {
            const float elapsed = int32_t(nowMs - d.moveStartMs) / 1000.0f;
            constexpr float kDuration = 1.5f;
            games_logic::calcCrateLerp(d.movePos, d.movePos + 3, elapsed, kDuration, slot.curPos);
            slot.moveProgress = elapsed / kDuration;

            // Only a setMoveTo that answered this push ends it (an older move has long finished).
            const bool moveIsOurs = int32_t(d.moveStartMs - slot.pushStartMs) >= 0;
            if (elapsed >= kDuration && slot.pushing && moveIsOurs) {
                slot.pushing = false;
                client.sendCrateDone(d.doId);
            }
        }

        // DistributedCrate.setReject: the grid cell behind the crate is blocked; the toon may
        // push again (on another face, or later).
        if (slot.pushing && d.pushRejected) {
            slot.pushing = false;
            TT_LOG("level_games: crate %u push on side %d rejected", unsigned(d.doId), slot.pushSide);
        }

        // Push detection: toon walking into crate
        float localToon[3];
        toLocalLocal(e->world, toonX, toonY, toonZ, localToon);
        const float scale = level.spec_.number(*e, "scale", 1.0f);
        const float w = 2.8f * scale;

        // Is toon near crate face?
        const float dx = localToon[0] - slot.curPos[0];
        const float dy = localToon[1] - slot.curPos[1];
        const float dz = localToon[2] - slot.curPos[2];

        if (dz >= -0.5f && dz <= 5.5f * scale) {
            const int side = games_logic::calcCratePushSide(dx, dy);
            const float distFromCenter = std::sqrt(dx * dx + dy * dy);
            if (side >= 0 && distFromCenter < w + 2.0f && !slot.pushing) {
                if (nowMs - slot.pushStartMs > 1000) {
                    slot.pushStartMs = nowMs;
                    slot.pushSide = side;
                    slot.pushing = true;
                    playAt(kSfxCrateEffort, e->world + 9, 0.8f);
                    TT_LOG("level_games: pushing crate %u on side %d (sending requestPush)", unsigned(d.doId), side);
                    client.sendCratePush(d.doId, static_cast<uint8_t>(side));
                }
            }
        }
    }
}

// ---- DistributedMaze ----
namespace {
// DistributedMaze.RemoveBlocksDict[4] in bake_level.MAZE_BLOCKS indices (0_1, 0_2, 1_3, 2_3). Its
// 2-section entry is a bare string, so choice() returns it and removeHedgeBlocks looks up one
// character at a time: nothing is removed there.
constexpr uint8_t kRemoveBlocks4[3] = {0b1101, 0b1110, 0b1111};
constexpr const char* kMazeBlockNames[4] = {"HedgeBlock_0_1", "HedgeBlock_0_2", "HedgeBlock_1_3", "HedgeBlock_2_3"};
// TTLocalizer
constexpr const char* kEnterHedgeMaze = "Race through the Hedge Maze\n for a laff bonus!";
constexpr const char* kHedgeMazePlaces[4] = {"first", "second", "third", "fourth"};
} // namespace

void LevelGames::loadMaze(int room, const char* level) {
    char path[128];
    std::snprintf(path, sizeof(path), "sdmc:/3ds/toontown3ds/data/level/%s.tmz", level);
    FILE* f = std::fopen(path, "rb");
    if (!f) return; // not a maze room
    resetMaze();
    MazeRoom& m = maze_;
    struct {
        char magic[4];
        uint16_t version;
        uint8_t sections, blocks;
    } h;
    bool ok = std::fread(&h, sizeof(h), 1, f) == 1 && !std::memcmp(h.magic, "TMZ1", 4) && h.version == 1 &&
              h.sections <= 4 && h.blocks <= 4;
    for (int i = 0; ok && i < h.sections; ++i) ok = std::fread(m.section[i], sizeof(m.section[i]), 1, f) == 1;
    for (int i = 0; ok && i < h.blocks; ++i)
        ok = std::fread(m.blockName[i], 16, 1, f) == 1 && std::fread(m.block[i], sizeof(m.block[i]), 1, f) == 1;
    uint16_t hints[2] = {};
    ok = ok && std::fread(m.entrance, sizeof(m.entrance), 1, f) == 1 && std::fread(m.finish, sizeof(m.finish), 1, f) == 1 &&
         std::fread(hints, sizeof(hints), 1, f) == 1;
    m.numHints = ok ? (hints[0] < 16 ? hints[0] : 16) : 0;
    for (int i = 0; ok && i < m.numHints; ++i) ok = std::fread(m.hints[i], sizeof(m.hints[i]), 1, f) == 1;
    std::fclose(f);
    if (!ok) {
        TT_LOG("level_games: bad %s", path);
        m = MazeRoom{};
        return;
    }
    m.room = room;
    m.numSections = h.sections;
    m.numBlocks = h.blocks;
    for (int i = 0; i < m.numSections; ++i) {
        std::snprintf(path, sizeof(path), "sdmc:/3ds/toontown3ds/data/level/%s_maze_%d.ttm", level, i);
        assetBatch_.load(mazeSectionModel_[i], path);
    }
    for (int i = 0; i < m.numBlocks; ++i) {
        m.blockName[i][15] = 0;
        std::snprintf(path, sizeof(path), "sdmc:/3ds/toontown3ds/data/level/%s_hedge_%s.ttm", level, m.blockName[i]);
        assetBatch_.load(mazeBlockModel_[i], path);
    }
    TT_LOG("level_games: room %d %s is a hedge maze: %d sections, %d hedge blocks, %d hints", room, level,
           m.numSections, m.numBlocks, m.numHints);
}

void LevelGames::resetMaze() {
    for (auto& mdl : mazeSectionModel_) mdl.unload();
    for (auto& mdl : mazeBlockModel_) mdl.unload();
    maze_ = MazeRoom{};
}

// DistributedMaze.gotRoom + removeHedgeBlocks for this maze object, and the matching collision.
void LevelGames::applyMaze(Level& level, const net::LevelEntityObj& d) {
    MazeRoom& m = maze_;
    const uint64_t seed = uint64_t(d.entId) * d.doId;
    int rot[4];
    games_logic::shuffleMazeRotations(seed, rot);
    uint8_t removed = 0;
    if (m.numSections == 4) removed = kRemoveBlocks4[PyRandom(seed).choiceIndex(3)];
    uint32_t mask = CollisionWorld::kMazeDefaultVariants & ~(0xFFFFu << 1) & ~(0xFu << 17);
    for (int i = 0; i < 4; ++i) {
        m.rotation[i] = i < m.numSections ? rot[i] : 0;
        mask |= 1u << (1 + 4 * i + m.rotation[i] / 90);
    }
    for (int i = 0; i < m.numBlocks; ++i) {
        int k = 0;
        while (k < 4 && std::strcmp(m.blockName[i], kMazeBlockNames[k])) ++k;
        m.blockShown[i] = k >= 4 || !(removed >> k & 1);
        if (k < 4 && m.blockShown[i]) mask |= 1u << (17 + k);
    }
    level.floorCollision_.setVariantMask(mask);
    m.mazeDoId = d.doId;
    m.startSeq = d.gameStartSeq; // a game already running when we arrived is not ours to start
    m.eventSeq = d.moleEventSeq;
    m.gameStarted = m.finished = m.timedOut = m.entered = m.inHint = false;
    m.timerEndMs = 0;
    TT_LOG("level_games: maze %u: sections at %d / %d / %d / %d, hedge blocks removed 0x%x", unsigned(d.doId),
           m.rotation[0], m.rotation[1], m.rotation[2], m.rotation[3], unsigned(removed));
}

void LevelGames::updateMazes(net::Client& client, Level& level, float toonX, float toonY, float toonZ, uint32_t nowMs) {
    MazeRoom& m = maze_;
    m.inHint = false;
    if (m.room < 0) return;
    for (int i = 0; i < net::kMaxLevelEntities; ++i) {
        const net::LevelEntityObj& d = client.levelEntitySlot(i);
        if (!d.active || d.classId != dc::DistributedMaze::kClassId) continue;
        const LevelEntity* e = level.specOf(d.levelDoId, d.entId);
        if (!e || e->room != m.room) continue;
        if (d.doId != m.mazeDoId) applyMaze(level, d);
        const float* place = level.rooms_[m.room].placement;
        auto inSphere = [&](const float roomPos[3], float r) {
            float w[3];
            fromLocalLocal(place, roomPos, w);
            const float dx = toonX - w[0], dy = toonY - w[1], dz = toonZ + kToonEventZ - w[2];
            const float rr = r + kToonEventR;
            return dx * dx + dy * dy + dz * dz < rr * rr;
        };
        // setGameStart: GameDuration = 35 + numSections * 15 on the timer below the corner.
        if (d.gameStartSeq != m.startSeq && d.gameStartMs) {
            m.startSeq = d.gameStartSeq;
            m.gameStarted = true;
            m.timerEndMs = d.gameStartMs + uint32_t((35 + m.numSections * 15) * 1000);
        }
        if (m.timerEndMs && int32_t(nowMs - m.timerEndMs) >= 0) m.timerEndMs = 0; // timerExpired
        // __handleToonEnterance (radius 8 at ENTRANCE), __handleToonFinish (15 at finish).
        const bool atEntrance = inSphere(m.entrance, 8.0f);
        if (atEntrance && !m.entered && !m.gameStarted) {
            TT_LOG("level_games: toon entered maze %u (sending setClientTriggered)", unsigned(d.doId));
            client.sendMazeTriggered(d.doId);
            moleInfo(kEnterHedgeMaze, nowMs);
        }
        m.entered = atEntrance;
        if (inSphere(m.finish, 15.0f) && !m.finished) {
            m.finished = true;
            TT_LOG("level_games: toon finished maze %u (sending setFinishedMaze)", unsigned(d.doId));
            client.sendMazeFinished(d.doId);
        }
        for (int h = 0; h < m.numHints && !m.inHint; ++h) m.inHint = inSphere(m.hints[h], 7.0f);
        // toonFinished(avId, place, lastToon); lastToon -> setGameOver.
        bool gameOver = d.gameOver && !m.timedOut;
        while (m.eventSeq != d.moleEventSeq) {
            if (uint16_t(d.moleEventSeq - m.eventSeq) > 8) m.eventSeq = uint16_t(d.moleEventSeq - 8);
            const net::LevelEntityObj::MoleEvent& ev = d.moleEvents[m.eventSeq % 8];
            m.eventSeq++;
            const char* name = ev.avId == client.avatarId() ? client.avatarName() : client.avatarName(ev.avId);
            if (!m.timedOut && name) {
                char line[64];
                std::snprintf(line, sizeof(line), "%s \n  finished in %s place!", name,
                              kHedgeMazePlaces[ev.index >= 0 && ev.index < 4 ? ev.index : 3]);
                moleInfo(line, nowMs);
            }
            if (ev.status) gameOver = !m.timedOut;
        }
        if (gameOver) { // setGameOver: a Toon still in the maze takes damage and teleports to the finish
            m.timedOut = true;
            if (!m.finished) {
                client.sendMazeDamage(d.doId);
                fromLocalLocal(place, m.finish, mazeWarp_);
                hasMazeWarp_ = true;
            }
            m.timerEndMs = 0;
        }
    }
}

bool LevelGames::mazePoint(int which, float out[3]) const {
    if (!level_ || maze_.room < 0 || (which == 2 && maze_.numHints == 0)) return false;
    const float* p = which == 0 ? maze_.entrance : which == 1 ? maze_.finish : maze_.hints[0];
    fromLocalLocal(level_->rooms_[maze_.room].placement, p, out);
    return true;
}

// ---- end maze ----

// ---- DistributedMoleField / MoleHill ----
namespace {

constexpr float kMoleDownZ = -2.5f;
constexpr float kMoleScale = 0.75f;
// MoleHill.setHillType's moleHead jitters (LerpPosInterval, 0.05 s each, from the head's place).
constexpr float kWhackJitter[20][3] = {
    {0.18f, 0, 0}, {0, -0.13f, 0}, {0, 0, 0.18f}, {-0.15f, 0, 0}, {0, 0.18f, 0}, {0.11f, 0, 0}, {0, -0.12f, 0},
    {0, 0, 0.18f}, {-0.18f, 0, 0}, {0, 0.13f, 0}, {0.18f, 0, 0}, {0, -0.15f, 0}, {0, 0, 0.18f}, {-0.16f, 0, 0},
    {0, 0.18f, 0}, {0.11f, 0, 0}, {0, -0.18f, 0}, {0, 0, 0.17f}, {-0.18f, 0, 0}, {0, 0, 0}};
constexpr float kCogJitter[10][3] = {{0.28f, 0, 0}, {0, -0.23f, 0}, {0, 0, 0.28f}, {-0.35f, 0, 0}, {0, 0.28f, 0},
                                     {0.31f, 0, 0}, {0, -0.32f, 0}, {0, 0, 0.48f}, {-0.28f, 0, 0}, {0, 0.29f, 0}};

void jitter(const float (*pts)[3], int n, float e, float out[3]) {
    int k = int(e / 0.05f);
    if (k >= n) {
        std::memcpy(out, pts[n - 1], sizeof(float) * 3);
        return;
    }
    const float u = (e - k * 0.05f) / 0.05f;
    static constexpr float kOrigin[3] = {0, 0, 0};
    const float* a = k ? pts[k - 1] : kOrigin;
    for (int c = 0; c < 3; ++c) out[c] = a[c] + (pts[k][c] - a[c]) * u;
}

// TTLocalizer
constexpr const char* kMolesInstruction = "Mole Stomp!\nJump on the red moles!";
constexpr const char* kMolesFinished = "Mole Stomp successful!";
constexpr const char* kMolesPityWin = "Stomp Failed! But the moles left.";
constexpr const char* kMolesRestarted = "Stomp Failed! Restarting...";

} // namespace

LevelGames::MoleField* LevelGames::moleFieldFor(int slot, const net::LevelEntityObj& d, const LevelEntity& e,
                                                const Level& level) {
    MoleField* freeField = nullptr;
    for (MoleField& f : moleFields_) {
        if (f.slot == slot && f.doId == d.doId) return &f;
        if (!freeField && (f.slot < 0 || !client_->levelEntitySlot(f.slot).active ||
                           client_->levelEntitySlot(f.slot).doId != f.doId))
            freeField = &f;
    }
    if (!freeField) return nullptr;
    MoleField& f = *freeField;
    f = MoleField{};
    f.slot = slot;
    f.doId = d.doId;
    f.numX = int(level.spec_.number(e, "numSquaresX", 5));
    f.numY = int(level.spec_.number(e, "numSquaresY", 5));
    f.spacingX = level.spec_.number(e, "spacingX", 5);
    f.spacingY = level.spec_.number(e, "spacingY", 5);
    f.numMoles = f.numX * f.numY;
    if (f.numX <= 0 || f.numY <= 0 || f.numMoles > games_logic::kMaxMoles) {
        TT_LOG("level_games: mole field %u has %d x %d hills (max %d), not shown", unsigned(d.doId), f.numX, f.numY,
               games_logic::kMaxMoles);
        f.numMoles = 0;
    }
    std::memcpy(f.world, e.world, sizeof(f.world));
    f.eventSeq = d.moleEventSeq; // events from before we built the hills are history
    TT_LOG("level_games: mole field %u: %d x %d hills, spacing %.1f x %.1f", unsigned(d.doId), f.numX, f.numY,
           f.spacingX, f.spacingY);
    return &f;
}

void LevelGames::moleInfo(const char* text, uint32_t nowMs) {
    std::snprintf(infoText_, sizeof(infoText_), "%s", text);
    infoMs_ = nowMs ? nowMs : 1;
}

const char* LevelGames::infoText(uint32_t nowMs, float* alpha) const {
    if (!infoMs_ || !infoText_[0]) return nullptr;
    const float e = int32_t(nowMs - infoMs_) / 1000.0f;
    if (e >= 3.6f) return nullptr;
    *alpha = e < 3.1f ? 1.0f : 1.0f - (e - 3.1f) / 0.5f;
    return infoText_;
}

bool LevelGames::gameHud(const char** label, int* timerSeconds, uint32_t nowMs) const {
    auto secondsTo = [nowMs](uint32_t endMs) {
        const int32_t ms = int32_t(endMs - nowMs);
        return endMs && ms > 0 ? int((ms + 999) / 1000) : -1;
    };
    for (const MoleField& f : moleFields_) {
        if (f.slot < 0 || !f.gameStarted || f.won || !client_) continue;
        const net::LevelEntityObj& d = client_->levelEntitySlot(f.slot);
        if (!d.active || d.doId != f.doId) continue;
        const int left = int(d.molesWhackedTarget) - int(d.score);
        if (left < 0) continue;
        std::snprintf(hudLabel_, sizeof(hudLabel_), "Moles Left: %d", left); // TTLocalizer.MolesLeft
        *label = hudLabel_;
        *timerSeconds = secondsTo(f.timerEndMs);
        return true;
    }
    if (maze_.room >= 0 && maze_.gameStarted && maze_.timerEndMs) { // DistributedMaze.loadGui
        *label = "Maze Race!"; // TTLocalizer.mazeLabel
        *timerSeconds = secondsTo(maze_.timerEndMs);
        return true;
    }
    return false;
}

bool LevelGames::moleTarget(bool cog, float out[3]) const {
    for (const MoleField& f : moleFields_) {
        if (f.slot < 0) continue;
        for (int h = 0; h < f.numMoles; ++h) {
            const MoleHill& hill = f.hills[h];
            if (!hill.col || hill.z < -0.5f || hill.type != (cog ? games_logic::kHillBomb : games_logic::kHillMole))
                continue;
            const float hl[3] = {(h % f.numX) * f.spacingX, (h / f.numX) * f.spacingY, 0};
            fromLocalLocal(f.world, hl, out);
            return true;
        }
    }
    return false;
}

// MoleHill.setHillType (models, collision scale) and the whack intervals it starts.
void LevelGames::moleSetType(MoleHill& h, uint8_t type, uint32_t nowMs) {
    using namespace games_logic;
    // A raised hill keeps its kind: a pop never turns a HILL_MOLE into a HILL_BOMB or back.
    if ((h.type == kHillMole && type == kHillBomb) || (h.type == kHillBomb && type == kHillMole)) return;
    h.type = type;
    h.headOff[0] = h.headOff[1] = h.headOff[2] = 0;
    h.headScale = 1;
    if (type == kHillMole) h.colRadius = 3.0f * kMoleScale;
    if (type == kHillBomb || type == kHillCogWhacked) h.colRadius = 1.0f * kMoleScale;
    if (type == kHillCogWhacked) {
        // doMoleDown returns at once (the type is already COGWHACKED): jitter 0.5 s, then down.
        h.ival = kIvalCogWhacked;
        h.ivalMs = nowMs;
        h.fromZ = h.z;
    }
    if (type == kHillWhacked) {
        h.colRadius = 0;
        h.col = false;
        h.z = 0; // finish() of the running pop, then self.mole.setPos(0, 0, 0)
        h.ival = kIvalWhacked;
        h.ivalMs = nowMs;
    }
}

void LevelGames::molePop(MoleHill& h, const games_logic::MolePopup& p, uint32_t startMs) {
    using namespace games_logic;
    if (h.type == kHillWhacked || h.type == kHillCogWhacked) return;
    h.popupNum++;
    h.bombBranch = h.type == kHillBomb; // decided before the sequence's setHillType runs
    moleSetType(h, p.type, startMs);
    h.ival = kIvalPop;
    h.ivalMs = startMs;
    h.fromZ = h.z;
    h.up = p.upTime, h.stay = p.stayTime, h.down = p.downTime;
    h.col = h.bombBranch;
}

void LevelGames::moleEval(MoleHill& h, uint32_t nowMs) {
    using namespace games_logic;
    const float e = int32_t(nowMs - h.ivalMs) / 1000.0f;
    if (e < 0) return;
    switch (h.ival) {
    case kIvalPop:
        if (e < h.up) {
            h.z = h.fromZ + (0 - h.fromZ) * (e / h.up);
        } else if (e < h.up + h.stay) {
            if (!h.bombBranch && h.z != 0) h.col = true; // unstash once up
            h.z = 0;
        } else if (e < h.up + h.stay + h.down) {
            if (!h.bombBranch) h.col = false; // stash before going down
            h.z = kMoleDownZ * ((e - h.up - h.stay) / h.down);
        } else {
            h.z = kMoleDownZ;
            h.col = false;
            h.ival = kIvalNone;
        }
        break;
    case kIvalDown:
        h.col = false;
        if (e < 1.0f) {
            h.z = h.fromZ + (kMoleDownZ - h.fromZ) * e;
        } else {
            h.z = kMoleDownZ;
            h.ival = kIvalNone;
        }
        break;
    case kIvalWhacked:
        if (e < 1.0f) {
            jitter(kWhackJitter, 20, e, h.headOff);
            h.headScale = e < 0.5f ? 1 + 2.5f * (e / 0.5f) : 3.5f - 2.5f * ((e - 0.5f) / 0.5f);
            h.z = 0;
        } else if (e < 1.5f) {
            h.headOff[0] = h.headOff[1] = h.headOff[2] = 0;
            h.headScale = 1;
            h.z = kMoleDownZ * ((e - 1.0f) / 0.5f);
        } else {
            h.z = kMoleDownZ;
            h.ival = kIvalNone;
            moleSetType(h, kHillMole, nowMs);
        }
        break;
    case kIvalCogWhacked:
        h.col = false;
        if (e < 0.5f) {
            jitter(kCogJitter, 10, e, h.headOff);
        } else if (e < 1.0f) {
            h.z = h.fromZ + (kMoleDownZ - h.fromZ) * ((e - 0.5f) / 0.5f);
        } else {
            h.z = kMoleDownZ;
            h.ival = kIvalNone;
            moleSetType(h, kHillBomb, nowMs);
        }
        break;
    default:
        break;
    }
}

// __showToonHitByBomb for the local Toon: up 50 units a second under gravity 32, spinning, to a
// random hill picked by random.Random(timestamp), at the field's world x / y plus that hill's
// offset (not rotated, as upstream).
void LevelGames::moleHitByBomb(MoleField& f, int index, int32_t ts, uint32_t nowMs) {
    (void)index;
    int tileX, tileY, hRot, pRot;
    games_logic::moleFlightPick(ts, f.numX, f.numY, &tileX, &tileY, &hRot, &pRot);
    MoleFlight& fl = moleFlight_;
    fl.active = true;
    fl.startMs = nowMs;
    fl.start[0] = toonX_, fl.start[1] = toonY_, fl.start[2] = f.world[11]; // toon.setZ(self.getZ())
    fl.end[0] = f.world[9] + tileX * f.spacingX;
    fl.end[1] = f.world[10] + tileY * f.spacingY;
    fl.end[2] = fl.start[2];
    fl.hRot = hRot, fl.pRot = pRot;
    std::memcpy(fl.camEye, cameraEye_, sizeof(fl.camEye));
    playAt(kSfxMoleSurprise, fl.start); // soundBomb (AV_pig_howl, soundBomb2, is not in the SD set)
    playAt(kSfxTagC, fl.start);         // soundIUpDown: MG_Tag_C, then the cannon whizz
    TT_LOG("level_games: mole %d threw us to hill (%d, %d), %d H / %d P turns", index, tileX, tileY, hRot, pRot);
}

void LevelGames::updateMoles(net::Client& client, Level& level, float toonX, float toonY, float toonZ, float dt,
                             uint32_t nowMs) {
    using namespace games_logic;
    (void)dt;
    moleCamera_ = false;
    for (int i = 0; i < net::kMaxLevelEntities; ++i) {
        const net::LevelEntityObj& d = client.levelEntitySlot(i);
        if (!d.active || d.classId != dc::DistributedMoleField::kClassId) continue;
        const LevelEntity* e = level.specOf(d.levelDoId, d.entId);
        if (!e || !level.shown(e->room)) continue;
        MoleField* fp = moleFieldFor(i, d, *e, level);
        if (!fp || fp->numMoles == 0) continue;
        MoleField& f = *fp;
        std::memcpy(f.world, e->world, sizeof(f.world));

        // setGameStart (a failed round restarts with another one).
        if (d.gameStartSeq != f.startSeq && d.gameStartMs) {
            f.startSeq = d.gameStartSeq;
            f.count = calcMoleSchedule(uint64_t(d.entId) * d.levelDoId, double(d.gameDuration), f.numMoles,
                                       f.schedule, kMaxMolePopups);
            f.next = 0;
            f.gameStartMs = d.gameStartMs;
            f.gameStarted = true;
            f.activeField = true;
            f.inRange = false;
            f.won = false;
            f.timerEndMs = d.gameStartMs + uint32_t(d.gameDuration) * 1000u;
            for (MoleHill& h : f.hills) h.popupNum = 0;
            if (f.hasRestarted) {
                moleInfo(kMolesRestarted, nowMs);
                client.sendMoleDamage(d.doId);
            } else {
                f.hasRestarted = true;
            }
            TT_LOG("level_games: mole field %u game start: %d popups over %u s, target %u", unsigned(d.doId), f.count,
                   unsigned(d.gameDuration), unsigned(d.molesWhackedTarget));
        }
        // scheduleTask
        if (f.gameStarted) {
            while (f.next < f.count && f.activeField) {
                const MolePopup& p = f.schedule[f.next];
                const uint32_t at = f.gameStartMs + uint32_t(p.start * 1000.0f);
                if (int32_t(nowMs - at) < 0) break;
                if (p.hole < f.numMoles) molePop(f.hills[p.hole], p, at);
                f.next++;
            }
        }
        // updateMole / reportToonHitByBomb
        while (f.eventSeq != d.moleEventSeq) {
            if (uint16_t(d.moleEventSeq - f.eventSeq) > 8) f.eventSeq = uint16_t(d.moleEventSeq - 8);
            const net::LevelEntityObj::MoleEvent& m = d.moleEvents[f.eventSeq % 8];
            f.eventSeq++;
            if (m.index < 0 || m.index >= f.numMoles) continue;
            MoleHill& h = f.hills[m.index];
            if (m.avId == 0) {
                if (m.status == 1 /* WHACKED */ && h.type != kHillCogWhacked) {
                    moleSetType(h, kHillCogWhacked, nowMs);
                    playAt(kSfxMoleStomp, e->world + 9);
                }
            } else if (m.avId != client.avatarId()) {
                if (h.type != kHillWhacked) {
                    moleSetType(h, kHillWhacked, nowMs);
                    playAt(kSfxMoleStomp, e->world + 9);
                }
            }
        }
        for (int h = 0; h < f.numMoles; ++h) moleEval(f.hills[h], nowMs);
        // setScore -> gameWon, setPityWin: every mole down, the frame hidden, the info line.
        if (!f.won && f.gameStarted && (d.pityWin || (d.molesWhackedTarget && d.score >= d.molesWhackedTarget))) {
            f.won = true;
            for (int h = 0; h < f.numMoles; ++h) {
                MoleHill& hill = f.hills[h];
                hill.ival = kIvalNone;
                hill.col = false;
                hill.z = kMoleDownZ;
            }
            f.timerEndMs = 0;
            moleInfo(d.pityWin ? kMolesPityWin : kMolesFinished, nowMs);
            f.activeField = false;
            f.inRange = false;
        }
        if (f.timerEndMs && int32_t(nowMs - f.timerEndMs) >= 0) { // timerExpired: cleanDetect
            f.timerEndMs = 0;
            f.activeField = false;
            f.inRange = false;
        }

        // __detect every 0.1 s: within 0.75 of the field's larger side of its centre.
        if (int32_t(nowMs - f.detectMs) >= 100) {
            f.detectMs = nowMs;
            const float dimX = f.numX * f.spacingX, dimY = f.numY * f.spacingY;
            float c[3];
            const float cl[3] = {dimX * 0.5f, dimY * 0.5f, 0};
            fromLocalLocal(f.world, cl, c);
            const float dx = toonX - c[0], dy = toonY - c[1], dz = toonZ - c[2];
            const bool near = std::sqrt(dx * dx + dy * dy + dz * dz) < (dimY > dimX ? dimY : dimX) * 0.75f;
            if (near && !f.inRange) {
                if (!f.gameStarted) {
                    TT_LOG("level_games: toon entered mole field %u (sending setClientTriggered)", unsigned(d.doId));
                    client.sendMoleTriggered(d.doId);
                }
                f.inRange = true;
                if (f.activeField && !f.hasEntered) {
                    moleInfo(kMolesInstruction, nowMs);
                    f.hasEntered = true;
                }
            } else if (!near && f.inRange) {
                f.inRange = false;
            }
        }
        if (f.inRange && f.activeField) moleCamera_ = true;

        // handleEnterMole: the Toon's event sphere into a raised mole's sphere.
        if (moleFlight_.active) continue;
        for (int h = 0; h < f.numMoles; ++h) {
            MoleHill& hill = f.hills[h];
            if (!hill.col || hill.colRadius <= 0) continue;
            const float hl[3] = {(h % f.numX) * f.spacingX, (h / f.numX) * f.spacingY, hill.z};
            float hw[3];
            fromLocalLocal(f.world, hl, hw);
            const float dx = toonX - hw[0], dy = toonY - hw[1], dz = toonZ + kToonEventZ - hw[2];
            const float r = hill.colRadius + kToonEventR;
            if (dx * dx + dy * dy + dz * dz >= r * r) continue;
            if (!f.gameStarted) client.sendMoleTriggered(d.doId);
            hill.col = false; // stashMoleCollision
            if (hill.type == kHillMole) {
                const int32_t ts = client.networkTime(nowMs);
                moleSetType(hill, kHillWhacked, nowMs);
                TT_LOG("level_games: touched mole %d on field %u (sending whackedBomb)", h, unsigned(d.doId));
                client.sendMoleBomb(d.doId, int8_t(h), hill.popupNum, ts);
                moleHitByBomb(f, h, ts, nowMs);
                break;
            }
            if (hill.type == kHillBomb) {
                moleSetType(hill, kHillCogWhacked, nowMs);
                playAt(kSfxMoleStomp, hw);
                TT_LOG("level_games: stomped Cog mole %d on field %u (sending whackedMole)", h, unsigned(d.doId));
                client.sendMoleWhacked(d.doId, int8_t(h), hill.popupNum);
            }
        }
    }
}
// ---- end mole field ----

void LevelGames::updateGolf(net::Client& client, Level& level, float toonX, float toonY, float toonZ, uint32_t nowMs) {
    for (int i = 0; i < net::kMaxLevelEntities; ++i) {
        const net::LevelEntityObj& d = client.levelEntitySlot(i);
        if (!d.active || d.classId != dc::DistributedGolfGreenGame::kClassId) continue;
        const LevelEntity* e = level.specOf(d.levelDoId, d.entId);
        if (!e || !level.shown(e->room)) continue;

        SlotState& slot = slots_[i];
        float localToon[3];
        toLocalLocal(e->world, toonX, toonY, toonZ + kToonEventZ, localToon);
        const float dist = std::sqrt(localToon[0] * localToon[0] + localToon[1] * localToon[1] +
                                     localToon[2] * localToon[2]);

        // initCollisionGeom: entering the 11.5 sphere at the green sends requestJoin. The board
        // (Bust-a-Cog) is not played here, so walking back out leaves (its Exit button).
        constexpr float kJoinRadius = 11.5f;
        if (dist < kJoinRadius + kToonEventR && !slot.joinedGolf) {
            slot.joinedGolf = true;
            TT_LOG("level_games: entered golf green %u (sending requestJoin)", unsigned(d.doId));
            client.sendGolfJoin(d.doId);
        } else if (dist > kJoinRadius + kToonEventR + 2.0f && slot.joinedGolf) {
            slot.joinedGolf = false;
            TT_LOG("level_games: left golf green %u (sending leaveGame)", unsigned(d.doId));
            client.sendGolfLeave(d.doId);
        }
        // acceptJoin (first time with us in it) and signalDone: countryClub.showInfoText.
        if (d.golfJoinSeq != slot.golfJoinSeq) {
            slot.golfJoinSeq = d.golfJoinSeq;
            bool us = false;
            for (int k = 0; k < d.numJoinedToons; ++k) us |= d.joinedToons[k] == client.avatarId();
            if (us && !slot.golfEntered) {
                slot.golfEntered = true;
                moleInfo("Remove the cog ball!", nowMs); // TTLocalizer.BustACogInstruction
            }
        }
        if (d.golfDoneSeq != slot.golfDoneSeq) {
            slot.golfDoneSeq = d.golfDoneSeq;
            moleInfo(d.golfSuccess ? "Success!" : "Out of Time!", nowMs); // BustACogSuccess / Failure
        }
    }
}

void LevelGames::draw(render::Renderer& renderer, const C3D_FVec& eye) const {
    if (!client_ || !level_) return;
    if (maze_.room >= 0 && level_->shown(maze_.room)) {
        // The hedge maze's sections at their H, and the hedge blocks it kept.
        C3D_Mtx room;
        toMtxLocal(level_->rooms_[maze_.room].placement, &room);
        for (int i = 0; i < maze_.numSections; ++i) {
            if (!mazeSectionModel_[i].loaded()) continue;
            C3D_Mtx sec, m;
            toMtxLocal(maze_.section[i], &sec);
            Mtx_Multiply(&m, &room, &sec);
            Mtx_RotateZ(&m, maze_.rotation[i] * kDegToRad, true);
            renderer.drawModel(const_cast<render::Model&>(mazeSectionModel_[i]), m);
        }
        for (int i = 0; i < maze_.numBlocks; ++i) {
            if (!maze_.blockShown[i] || !mazeBlockModel_[i].loaded()) continue;
            renderer.drawModel(const_cast<render::Model&>(mazeBlockModel_[i]), room);
        }
    }

    for (int i = 0; i < net::kMaxLevelEntities; ++i) {
        const net::LevelEntityObj& d = client_->levelEntitySlot(i);
        if (!d.active) continue;
        const LevelEntity* e = level_->specOf(d.levelDoId, d.entId);
        if (!e || !level_->shown(e->room)) continue;

        const SlotState& slot = slots_[i];

        // 1. Laser fields: createGrid's symbol outlines and their beams from the projector
        // (additive, no depth write). Nothing else: the field has no floor model.
        if (d.classId == dc::DistributedLaserField::kClassId) {
            const int len = slot.shownLen;
            if (len < 2) continue;
            const int numX = slot.shownField[0], numY = slot.shownField[1];
            if (!numX || !numY) continue;
            const float scaleX = slot.draw.hasGridX ? slot.draw.gridX : 2.0f;
            const float scaleY = slot.draw.hasGridY ? slot.draw.gridY : 2.0f;
            const float tileW = scaleX / numX, tileH = scaleY / numY;
            float projector[3] = {0.0f, 0.0f, 25.0f};
            level_->spec_.getFloats(*e, "projector", projector, 3);
            // Panda draws the wire one pixel wide: about 1.5 top-screen pixels at the field.
            const float ex = e->world[9] - eye.x, ey = e->world[10] - eye.y, ez = e->world[11] - eye.z;
            const float wireW = std::clamp(std::sqrt(ex * ex + ey * ey + ez * ez) * 0.0045f, 0.03f, 0.4f);
            C3D_Mtx m;
            std::memcpy(m.m, slot.draw.matrix, sizeof(m.m));
            gAdd.begin(renderer, m, true);
            for (int x = 0; x < numX; ++x)
                for (int y = 0; y < numY; ++y) {
                    const uint8_t sym = games_logic::laserSymbol(slot.shownField, len, x, y);
                    if (!sym || sym >= laser::kNumSymbols) continue;
                    addLaserSymbol(laser::kSymbols[sym], x * tileW, y * tileH, tileW, tileH, projector, wireW);
                }
            if (slot.shownToonX >= 0) // isToonIn: symbolSelect on the Toon's tile
                addLaserSymbol(laser::kSelect, slot.shownToonX * tileW, slot.shownToonY * tileH, tileW, tileH,
                               projector, wireW);
            gAdd.flush();
        }
        // 2. Security cameras: square_stomper at the projector looking at the spot, the beam
        // fan (grey at the projector, black on the floor) and the floor disk (grey, red for 2 s
        // after trapFire), additive with no depth write and one-sided.
        else if (d.classId == dc::DistributedSecurityCamera::kClassId) {
            C3D_Mtx m;
            std::memcpy(m.m, slot.draw.matrix, sizeof(m.m));
            if (!slot.hideModel && securityCamera_.loaded()) {
                C3D_Mtx cm = m;
                if (slot.camTracking) {
                    const float* p = slot.projector;
                    const float fx = slot.trackX - p[0], fy = slot.trackY - p[1], fz = -p[2];
                    Mtx_Translate(&cm, p[0], p[1], p[2], true);
                    Mtx_RotateZ(&cm, std::atan2(-fx, fy), true);                       // lookAt heading
                    Mtx_RotateX(&cm, std::atan2(fz, std::sqrt(fx * fx + fy * fy)), true); // and pitch
                }
                Mtx_Translate(&cm, 0.0f, 1.0f, 0.0f, true);
                renderer.drawModel(const_cast<render::Model&>(securityCamera_), cm);
            }
            if (!slot.camTracking) continue;
            const float radius = level_->spec_.number(*e, "radius", 5.0f);
            const bool alert = slot.lastCameraAlarmMs && nowMs_ - slot.lastCameraAlarmMs < 2000;
            static const float kNorm[3] = {0.2f, 0.2f, 0.2f}, kAlert[3] = {1.0f, 0.0f, 0.0f}, kBlack[3] = {};
            const float* origin = alert ? kAlert : kNorm;
            const float draw = 1.0f / (0.01f + std::pow(slot.toonSpotDist, 0.4f));
            const int beamSegs = std::clamp(int(5.0f + draw * 12.0f), 5, 64);
            const int floorSegs = std::clamp(int(5.0f + draw * 50.0f), 5, 96);
            constexpr float kZ = 0.05f; // zFloat
            gAdd.begin(renderer, m, false);
            gAdd.reserve(beamSegs + 2, beamSegs * 3);
            const u16 apex = gAdd.vert(slot.projector[0], slot.projector[1], slot.projector[2], origin);
            for (int k = 0; k <= beamSegs; ++k) {
                const float a = 2.0f * kPi * float(k % beamSegs) / float(beamSegs);
                gAdd.vert(slot.trackX + radius * std::cos(a), slot.trackY + radius * std::sin(a), kZ, kBlack);
                if (k) gAdd.tri(apex, u16(apex + k), u16(apex + k + 1));
            }
            gAdd.reserve(floorSegs + 2, floorSegs * 3);
            const u16 centre = gAdd.vert(slot.trackX, slot.trackY, kZ, origin);
            for (int k = 0; k <= floorSegs; ++k) {
                const float a = 2.0f * kPi * float(k % floorSegs) / float(floorSegs);
                gAdd.vert(slot.trackX + radius * std::cos(a), slot.trackY + radius * std::sin(a), kZ, origin);
                if (k) gAdd.tri(centre, u16(centre + k), u16(centre + k + 1));
            }
            gAdd.flush();
        }
        // DistributedGrid.loadModel: FloorWear over the crate grid, its long side along x.
        else if (d.classId == dc::DistributedGrid::kClassId) {
            if (!gridFloor_.loaded()) continue;
            const float cell = level_->spec_.number(*e, "cellSize", 3.0f);
            const int cols = level_->spec_.integer(*e, "numCol", 0), rows = level_->spec_.integer(*e, "numRow", 0);
            if (cols <= 0 || rows <= 0) continue;
            const float scale = cell / 6.0f; // texSize
            const bool turn = cols < rows;
            C3D_Mtx m;
            std::memcpy(m.m, slot.draw.matrix, sizeof(m.m));
            Mtx_Translate(&m, cell * cols * 0.5f, cell * rows * 0.5f, 0.025f, true);
            if (turn) Mtx_RotateZ(&m, 90.0f * kDegToRad, true);
            Mtx_RotateX(&m, kPi, true); // setHpr(h, 180, 0)
            Mtx_Scale(&m, scale * (turn ? rows : cols), scale * (turn ? cols : rows), 1.0f);
            renderer.drawModel(const_cast<render::Model&>(gridFloor_), m);
        }
        // 3. Crates
        else if (d.classId == dc::DistributedCrate::kClassId) {
            const bool mintStyle = slot.draw.modelType == 1;
            render::Model& mdl = mintStyle && crateCbWood_.loaded() ? const_cast<render::Model&>(crateCbWood_)
                                                                    : const_cast<render::Model&>(crateWood_);
            if (mdl.loaded()) {
                C3D_Mtx m;
                std::memcpy(m.m, slot.draw.matrix, sizeof(m.m));
                Mtx_Translate(&m, slot.curPos[0], slot.curPos[1], slot.curPos[2], true);
                const float scale = slot.draw.scale;
                Mtx_Scale(&m, scale, scale, scale * (d.crushed ? 0.1f : 1.0f));
                renderer.drawModel(mdl, m);
            }
        }
        // 4. Mole fields: MoleHill's mole_hole and its mole (0.75), which turns to face the Toon
        // (setBillboardAxis(localAvatar)); a whacked one faces the camera.
        else if (d.classId == dc::DistributedMoleField::kClassId) {
            const MoleField* f = nullptr;
            for (const MoleField& mf : moleFields_)
                if (mf.slot == i && mf.doId == d.doId) f = &mf;
            if (!f || !moleHole_.loaded()) continue;
            for (int h = 0; h < f->numMoles; ++h) {
                const MoleHill& hill = f->hills[h];
                C3D_Mtx m;
                std::memcpy(m.m, slot.draw.matrix, sizeof(m.m));
                Mtx_Translate(&m, (h % f->numX) * f->spacingX, (h / f->numX) * f->spacingY, 0.0f, true);
                renderer.drawModel(const_cast<render::Model&>(moleHole_), m);
                if (hill.z <= kMoleDownZ + 0.01f) continue; // under the floor
                const render::Model* head = &moleNorm_;
                if (hill.type == games_logic::kHillWhacked) head = &moleHit_;
                else if (hill.type == games_logic::kHillBomb || hill.type == games_logic::kHillCogWhacked) head = &moleCog_;
                if (!head->loaded()) continue;
                const float hl[3] = {(h % f->numX) * f->spacingX, (h / f->numX) * f->spacingY, hill.z};
                float hw[3];
                fromLocalLocal(f->world, hl, hw);
                const bool faceCamera = hill.type == games_logic::kHillWhacked;
                const float tx = faceCamera ? eye.x : toonX_, ty = faceCamera ? eye.y : toonY_;
                // Billboard about the mole's up axis: its -y (the model's front) toward the target.
                const float heading = std::atan2(-(tx - hw[0]), ty - hw[1]) + kPi;
                C3D_Mtx mm;
                Mtx_Identity(&mm);
                Mtx_Translate(&mm, hw[0], hw[1], hw[2], true);
                Mtx_RotateZ(&mm, heading, true);
                const float s = kMoleScale * hill.headScale;
                Mtx_Translate(&mm, hill.headOff[0] * kMoleScale, hill.headOff[1] * kMoleScale,
                              hill.headOff[2] * kMoleScale, true);
                Mtx_Scale(&mm, s, s, s);
                renderer.drawModel(const_cast<render::Model&>(*head), mm);
            }
        }
        // 5. Golf greens: announceGenerate's bust_a_cog_golf_green (0.24) and its flag (0.5,
        // H -45 at (3, 4, 0)). The green is a ground-bin decal with no depth write.
        else if (d.classId == dc::DistributedGolfGreenGame::kClassId) {
            C3D_Mtx m;
            std::memcpy(m.m, slot.draw.matrix, sizeof(m.m));
            if (golfGreen_.loaded()) {
                C3D_Mtx gm = m;
                Mtx_Translate(&gm, 0.0f, 0.0f, 0.03f, true);
                Mtx_Scale(&gm, 0.24f, 0.24f, 0.24f);
                renderer.drawModel(const_cast<render::Model&>(golfGreen_), gm);
            }
            if (golfFlag_.loaded()) {
                C3D_Mtx fm = m;
                Mtx_Translate(&fm, 3.0f, 4.0f, 0.0f, true);
                Mtx_RotateZ(&fm, -45.0f * kDegToRad, true);
                Mtx_Scale(&fm, 0.5f, 0.5f, 0.5f);
                renderer.drawModel(const_cast<render::Model&>(golfFlag_), fm);
            }
        }
    }
}

bool LevelGames::nearestLaserField(const Level& level, float x, float y, float z, float* outX, float* outY, float* outZ) const {
    return level.nearestEntity(dc::DistributedLaserField::kClassId, x, y, z, outX, outY, outZ);
}

bool LevelGames::nearestCamera(const Level& level, float x, float y, float z, float* outX, float* outY, float* outZ) const {
    return level.nearestEntity(dc::DistributedSecurityCamera::kClassId, x, y, z, outX, outY, outZ);
}

bool LevelGames::nearestCrate(const Level& level, float x, float y, float z, float* outX, float* outY, float* outZ) const {
    return level.nearestEntity(dc::DistributedCrate::kClassId, x, y, z, outX, outY, outZ);
}

bool LevelGames::nearestMaze(const Level& level, float x, float y, float z, float* outX, float* outY, float* outZ) const {
    return level.nearestEntity(dc::DistributedMaze::kClassId, x, y, z, outX, outY, outZ);
}

bool LevelGames::nearestMoleField(const Level& level, float x, float y, float z, float* outX, float* outY, float* outZ) const {
    return level.nearestEntity(dc::DistributedMoleField::kClassId, x, y, z, outX, outY, outZ);
}

bool LevelGames::nearestGolfGreen(const Level& level, float x, float y, float z, float* outX, float* outY, float* outZ) const {
    return level.nearestEntity(dc::DistributedGolfGreenGame::kClassId, x, y, z, outX, outY, outZ);
}

#endif // __3DS__

} // namespace tt::world
