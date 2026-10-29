#include "world/level.h"
#include "core/profile.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/log.h"
#include "world/floor_format.h"
#include "world/py_random.h"
#include "world/suits.h"

namespace tt::world {

namespace {

constexpr char kDir[] = "sdmc:/3ds/toontown3ds/data/level/";
constexpr uint16_t kZoneBase = 1000;     // bake_level.py ZONE_BASE: TZ zone = spec zone + 1000
constexpr float kWalkSpeed = 3.0f;       // DistributedFactorySuit: self.velocity = 3
constexpr float kTurnTime = 1.0f;        // PathEntity.makePathTrack turnTime
constexpr float kChaseSpeed = 4.0f;      // DistributedFactorySuit.chaseTask
constexpr float kReturnSpeed = 3.0f;     // DistributedFactorySuit.returnTask
constexpr float kChaseTime = 3.0f;       // chaseTask: setReturn after 3 s
constexpr float kAlertRadius = 15.0f;    // initializeBodyCollisions: toonSphere radius 15
constexpr float kAlertMaxDz = 8.0f;      // __handleToonAlert
constexpr float kStrayDistance = 10.0f;  // checkStrayTask
constexpr float kTouchRadius = kToonRadius + 1.0f; // suit body tube + toon sphere (suits.cpp touching)
constexpr float kBelowIgnore = 10.0f;    // __handleToonCollision: cogs 10 above us cannot fight
constexpr uint32_t kDenyMs = 6000;       // no battle within this long: walk on (denyBattle)

constexpr float kDegToRad = 0.01745329252f;

// SellbotFactoryInt (ToontownGlobals): the only factory spec the stock server builds.
const char* levelName(uint16_t factoryId) { return factoryId == 11500 ? "sellbot_factory" : nullptr; }

float headingTo(float fx, float fy, float tx, float ty) {
    // NodePath.headsUp: +Y towards the target, Panda heading (counter-clockwise about +Z).
    return std::atan2(-(tx - fx), ty - fy) / kDegToRad;
}

// LerpBlendHelpers easeIn / easeOut.
float easeIn(float t) { return t * t * (3.0f - t) * 0.5f; }
float easeOut(float t) { return t * (3.0f - t * t) * 0.5f; }
float clamp01(float t) { return t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t; }

// LevelEntity::world (basis rows) -> citro3d column-vector matrix.
void toMtx(const float w[12], C3D_Mtx* m) {
    Mtx_Identity(m);
    for (int r = 0; r < 3; ++r) {
        m->r[r].x = w[0 + r], m->r[r].y = w[3 + r], m->r[r].z = w[6 + r], m->r[r].w = w[9 + r];
    }
}

// Point in an entity's local space (rotation + uniform-ish scale, no shear).
void toLocal(const float w[12], float x, float y, float z, float out[3]) {
    const float d[3] = {x - w[9], y - w[10], z - w[11]};
    for (int r = 0; r < 3; ++r) {
        const float* axis = w + r * 3;
        const float len2 = axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2];
        out[r] = len2 > 1e-12f ? (d[0] * axis[0] + d[1] * axis[1] + d[2] * axis[2]) / len2 : 0.0f;
    }
}

// Entity-local point -> world: p' = x*r0 + y*r1 + z*r2 + r3 (LevelEntity::world layout).
void fromLocal(const float w[12], const float p[3], float out[3]) {
    for (int c = 0; c < 3; ++c) out[c] = p[0] * w[c] + p[1] * w[3 + c] + p[2] * w[6 + c] + w[9 + c];
}

constexpr const char* kDoorFiles[] = {"door_frame", "door_top", "door_bottom", "door_left", "door_right",
                                      "door_lock1_closed", "door_lock1_left", "door_lock1_right",
                                      "door_lock2_closed", "door_lock2_left", "door_lock2_right",
                                      "door_lock3_closed", "door_lock3_left", "door_lock3_right"};
constexpr float kDoorMove = 8.0f;   // DistributedDoorEntity moveDistance
constexpr float kLockSlide = 7.5f;  // DistributedDoorEntityLock slideLeft / slideRight

} // namespace

void Level::loadProps() {
    if (propsLoaded_) return;
    propsLoaded_ = true;
    static_assert(sizeof(kDoorFiles) / sizeof(kDoorFiles[0]) == kDoorParts, "door part table");
    char path[112];
    for (int i = 0; i < kDoorParts; ++i) {
        snprintf(path, sizeof(path), "%sprops/%s.ttm", kDir, kDoorFiles[i]);
        if (!assetBatch_.load(doorModels_[i], path))
            TT_LOG("level: missing %s", path);
    }
    const char* stomperNames[2] = {"stomper", "stomper_lb"};
    for (int k = 0; k < 2; ++k) {
        StomperShape& st = stompers_[k];
        snprintf(path, sizeof(path), "%sprops/%s_head.ttm", kDir, stomperNames[k]);
        assetBatch_.load(st.head, path);
        snprintf(path, sizeof(path), "%sprops/%s_shaft.ttm", kDir, stomperNames[k]);
        assetBatch_.load(st.shaft, path);
        snprintf(path, sizeof(path), "%sprops/%s.box", kDir, stomperNames[k]);
        if (FILE* f = fopen(path, "r")) {
            float b[9];
            if (fscanf(f, "%f %f %f %f %f %f %f %f %f", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5], &b[6], &b[7],
                       &b[8]) == 9) {
                memcpy(st.lo, b, 12), memcpy(st.hi, b + 3, 12), memcpy(st.shaftOffset, b + 6, 12);
            }
            fclose(f);
        }
    }
    static const char* const kSfxNames[kSfxCount] = {
        "CHQ_FACT_stomper_small", "CHQ_FACT_stomper_med", "CHQ_FACT_stomper_large", "CHQ_FACT_door_open_sliding",
        "CHQ_FACT_door_open_final", "CHQ_FACT_door_unlock", "CHQ_FACT_arms_retracting", "CHQ_FACT_switch_pressed",
        "CHQ_FACT_switch_depressed", "CHQ_FACT_switch_popup", "CHQ_FACT_elevator_up_down", "SZ_DD_treasure",
        "CHQ_GOON_hunker_down", "CHQ_GOON_rattle_shake", "CHQ_GOON_tractor_beam_alarmed"};
    for (int k = 0; k < kSfxCount; ++k) {
        snprintf(path, sizeof(path), "sdmc:/3ds/toontown3ds/data/audio/%s.tsnd", kSfxNames[k]);
        sfx_[k] = audio::loadSfx(path);
    }
    const char* gearNames[2] = {"gear_factory", "gear_mint"};
    for (int k = 0; k < 2; ++k) {
        snprintf(path, sizeof(path), "%sprops/%s.ttm", kDir, gearNames[k]);
        assetBatch_.load(gearModels_[k], path);
        snprintf(path, sizeof(path), "%sprops/%s.box", kDir, gearNames[k]);
        if (FILE* f = fopen(path, "r")) {
            float b[6];
            if (fscanf(f, "%f %f %f %f %f %f", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) == 6) memcpy(gearBox_[k], b, 24);
            fclose(f);
        }
    }
    snprintf(path, sizeof(path), "%sprops/paint_mixer_top.ttm", kDir);
    assetBatch_.load(mixerTop_, path);
    snprintf(path, sizeof(path), "%sprops/paint_mixer_shaft.ttm", kDir);
    assetBatch_.load(mixerShaft_, path);
    snprintf(path, sizeof(path), "%sprops/paint_mixer.box", kDir);
    if (FILE* f = fopen(path, "r")) {
        char kind[16];
        float b[6];
        while (fscanf(f, "%15s", kind) == 1) {
            if (!strcmp(kind, "shaft_origin")) {
                if (fscanf(f, "%f %f %f", &b[0], &b[1], &b[2]) == 3) memcpy(mixerShaftOrigin_, b, 12);
            } else if (fscanf(f, "%f %f %f %f %f %f", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) == 6) {
                if (!strcmp(kind, "floor")) memcpy(mixerFloor_, b, 24);
                else if (numMixerWalls_ < 2) memcpy(mixerWalls_[numMixerWalls_++], b, 24);
            }
        }
        fclose(f);
    }
    snprintf(path, sizeof(path), "%sprops/tread.ttm", kDir);
    assetBatch_.load(treadModel_, path);
    snprintf(path, sizeof(path), "%sprops/tread.box", kDir);
    if (FILE* f = fopen(path, "r")) {
        float b[6];
        if (fscanf(f, "%f %f %f %f %f %f", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) == 6) memcpy(treadBox_, b, 24);
        fclose(f);
    }
    static const char* const kGoonClipNames[kGoonClips] = {"walk", "collapse", "recovery"};
    const char* goonTypes[2] = {"pg", "sg"};
    for (int k = 0; k < 2; ++k) {
        snprintf(path, sizeof(path), "%sprops/goon_%s_full.ttm", kDir, goonTypes[k]);
        assetBatch_.load(goonModels_[k], path);
        for (int c = 0; c < kGoonClips; ++c) {
            snprintf(path, sizeof(path), "%sprops/goon_%s_full_%s.tta", kDir, goonTypes[k], kGoonClipNames[c]);
            assetBatch_.load(goonClips_[k][c], path);
        }
    }
    snprintf(path, sizeof(path), "%sprops/goon_cone.ttm", kDir);
    assetBatch_.load(goonCone_, path);
    snprintf(path, sizeof(path), "%sprops/goon.box", kDir);
    if (FILE* f = fopen(path, "r")) {
        float b[3];
        if (fscanf(f, "radar %f %f %f", &b[0], &b[1], &b[2]) == 3) memcpy(goonRadar_, b, 12);
        fclose(f);
    }
    snprintf(path, sizeof(path), "%sprops/barrel.ttm", kDir);
    assetBatch_.load(barrelModel_, path);
    snprintf(path, sizeof(path), "%sprops/barrel_jar.ttm", kDir);
    assetBatch_.load(barrelJar_, path);
    snprintf(path, sizeof(path), "%sprops/barrel_icecream.ttm", kDir);
    assetBatch_.load(barrelIcecream_, path);
    for (int t = 0; t < 7; ++t)
        for (int l = 0; l < 7; ++l) {
            snprintf(path, sizeof(path), "%sprops/gag_icon_%d_%d.ttm", kDir, t, l);
            assetBatch_.load(gagIcons_[t][l], path);
        }
    snprintf(path, sizeof(path), "%sprops/barrel.box", kDir);
    if (FILE* f = fopen(path, "r")) {
        float b[6];
        if (fscanf(f, "%f %f %f %f %f %f", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) == 6) memcpy(barrelBox_, b, sizeof(b));
        fclose(f);
    }
    snprintf(path, sizeof(path), "%sprops/lift.ttm", kDir);
    assetBatch_.load(liftModel_, path);
    snprintf(path, sizeof(path), "%sprops/lift.box", kDir);
    if (FILE* f = fopen(path, "r")) {
        static const char* const kNames[kLiftBoxes] = {"elevator_floor", "ceiling", "wall_front", "wall_back",
                                                       "wall_left", "wall_right"};
        char name[32];
        float b[6];
        while (fscanf(f, "%31s %f %f %f %f %f %f", name, &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) == 7)
            for (int k = 0; k < kLiftBoxes; ++k)
                if (!strcmp(name, kNames[k])) memcpy(liftBoxes_[k], b, sizeof(b));
        fclose(f);
    }
    snprintf(path, sizeof(path), "%sprops/button_base.ttm", kDir);
    assetBatch_.load(buttonBase_, path);
    snprintf(path, sizeof(path), "%sprops/button_top.ttm", kDir);
    assetBatch_.load(buttonTop_, path);
    snprintf(path, sizeof(path), "%sprops/button.box", kDir);
    if (FILE* f = fopen(path, "r")) {
        float b[6];
        if (fscanf(f, "%f %f %f %f %f %f", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) == 6) memcpy(buttonBox_, b, sizeof(b));
        fclose(f);
    }
    snprintf(path, sizeof(path), "%sprops/stage_elevator.ttm", kDir);
    assetBatch_.load(elevatorFrame_, path);
    snprintf(path, sizeof(path), "%sprops/stage_elevator_left.ttm", kDir);
    assetBatch_.load(elevatorLeft_, path);
    snprintf(path, sizeof(path), "%sprops/stage_elevator_right.ttm", kDir);
    assetBatch_.load(elevatorRight_, path);
    snprintf(path, sizeof(path), "%sprops/stage_elevator.box", kDir);
    if (FILE* f = fopen(path, "r")) {
        char kind[16];
        float b[6];
        while (numElevatorBoxes_ < kMaxElevatorBoxes &&
               fscanf(f, "%15s %f %f %f %f %f %f", kind, &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) == 7) {
            memcpy(elevatorBoxes_[numElevatorBoxes_], b, sizeof(b));
            elevatorBoxKind_[numElevatorBoxes_++] = kind[0];
        }
        fclose(f);
    }
    snprintf(path, sizeof(path), "%sprops/door.box", kDir);
    if (FILE* f = fopen(path, "r")) {
        float b[6];
        if (fscanf(f, "%f %f %f %f %f %f", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) == 6) memcpy(doorBox_, b, sizeof(b));
        fclose(f);
    }
    // ---- level games (world/level_games.*) ----
    games_.loadProps();
    // ---- end level games ----
}

// DistributedDoorEntity's FourState tracks (opening 5 s, open, closing 6 s, closed) and the
// lock slides of DistributedDoorEntityLock (locking 3.5 s, locked, unlocking 4 s, unlocked).
Level::DoorPose Level::doorPose(const net::LevelEntityObj& d, uint32_t nowMs) {
    DoorPose p;
    const float t = int32_t(nowMs - d.stateMs) / 1000.0f;
    switch (d.state) {
    case 1: { // opening: outer halves 0.1d..0.5d, inner halves after (openInnerDoors)
        const float u = easeOut(clamp01((t - 0.5f) / 2.0f));
        p.top = kDoorMove * u, p.bottom = -kDoorMove * u;
        p.outer = t < 2.5f;
        const float v = easeOut(clamp01((t - 2.5f) / 2.0f));
        p.left = -kDoorMove * v, p.right = kDoorMove * v;
        p.inner = t < 4.5f;
        break;
    }
    case 2: // open
        p.outer = p.inner = false;
        break;
    case 3: { // closing: inner halves 0.1d..0.5d, then the outer halves
        const float v = easeIn(clamp01((t - 0.6f) / 2.4f));
        p.left = -kDoorMove * (1.0f - v), p.right = kDoorMove * (1.0f - v);
        p.inner = t >= 0.6f;
        const float u = easeIn(clamp01((t - 3.0f) / 2.4f));
        p.top = kDoorMove * (1.0f - u), p.bottom = -kDoorMove * (1.0f - u);
        p.outer = t >= 3.0f;
        break;
    }
    default: // 0 off, 4 closed
        break;
    }
    for (int i = 0; i < 3; ++i) {
        const int ls = (d.locks >> (4 * i)) & 15;
        const float lt = d.locksMs[i] ? int32_t(nowMs - d.locksMs[i]) / 1000.0f : 1e6f;
        if (ls == 1) { // locking: the halves slide in from 7.5 over 16 beats after 2 beats
            const float beat = 3.5f * 0.05f;
            const float u = easeIn(clamp01((lt - 2 * beat) / (16 * beat)));
            p.lock[i] = kLockSlide * (1.0f - u);
            p.lockOpen[i] = lt < 18 * beat;
            p.lockClosed[i] = !p.lockOpen[i];
        } else if (ls == 2) { // locked
            p.lockClosed[i] = true;
        } else if (ls == 3) { // unlocking
            const float beat = 4.0f * 0.05f;
            const float u = easeOut(clamp01((lt - 2 * beat) / (16 * beat)));
            p.lock[i] = kLockSlide * u;
            p.lockClosed[i] = lt < 2 * beat;
            p.lockOpen[i] = !p.lockClosed[i] && lt < 18 * beat;
        }
        // 4 unlocked (and 0 off): no slide shown
    }
    return p;
}

void Level::updateDoors(net::Client& client, float x, float y, float z, uint32_t nowMs) {
    doorClient_ = &client;
    doorNowMs_ = nowMs;
    numSolids_ = 0;
    dynamic_.floor = &Level::floorThunk;
    dynamic_.push = &Level::pushThunk;
    dynamic_.carry = &Level::carryThunk;
    dynamic_.user = this;
    // VisibilityExtender: a door that is not closed extends its zone's visibility with the
    // extender's newZones (the event fires with "not closed", DistributedDoorEntity.changedOnState).
    uint16_t extra[64];
    int numExtra = 0;
    for (int i = 0; i < net::kMaxLevelEntities; ++i) {
        const net::LevelEntityObj& d = client.levelEntitySlot(i);
        if (!d.active || d.classId != dc::DistributedDoorEntity::kClassId) continue;
        const LevelEntity* e = specOf(d.levelDoId, d.entId);
        if (!e || !shown(e->room)) continue;
        const DoorPose pose = doorPose(d, nowMs);
        if (pose.outer || pose.inner) addSolid(e->world, doorBox_, doorBox_ + 3, true, false);
        // The tracks' SoundIntervals: each slide, then the 'final' clank 0.375 d later.
        const float t = int32_t(nowMs - d.stateMs) / 1000.0f;
        const float prev = entityState_[i] == d.state ? entityPrev_[i] : -1.0f;
        auto at = [&](float when) { return prev < when && t >= when; };
        if (d.state == 1) { // opening (d = 5): outer at 0.5, inner at 2.5
            if (at(0.5f) || at(2.5f)) playAt(kSfxDoorSlide, e->world + 9, 0.8f);
            if (at(0.5f + 1.875f) || at(2.5f + 1.875f)) playAt(kSfxDoorFinal, e->world + 9, 0.8f);
        } else if (d.state == 3) { // closing (d = 6): inner at 0.6, outer at 3.0
            if (at(0.6f) || at(3.0f)) playAt(kSfxDoorSlide, e->world + 9, 0.8f);
            if (at(0.6f + 2.25f) || at(3.0f + 2.25f)) playAt(kSfxDoorFinal, e->world + 9, 0.8f);
        }
        for (int k = 0; k < 3; ++k) { // DistributedDoorEntityLock: unlock clunk + arms
            const int ls = (d.locks >> (4 * k)) & 15;
            const float lt = d.locksMs[k] ? int32_t(nowMs - d.locksMs[k]) / 1000.0f : 1e6f;
            const float lp = lt - (nowMs - doorNowMsPrev_) / 1000.0f;
            if ((ls == 1 || ls == 3) && lp < 2 * (ls == 1 ? 0.175f : 0.2f) && lt >= 2 * (ls == 1 ? 0.175f : 0.2f)) {
                if (ls == 3) playAt(kSfxDoorUnlock, e->world + 9, 0.8f);
                playAt(kSfxArms, e->world + 9, 0.8f);
            }
        }
        entityState_[i] = d.state;
        entityPrev_[i] = t;
        if (!floor_ && d.state != 4 && d.state != 0) {
            for (int k = spec_.next("visibilityExtender", -1); k >= 0; k = spec_.next("visibilityExtender", k)) {
                const LevelEntity& v = spec_.entity(k);
                if (spec_.integer(v, "event", -1) != int32_t(d.entId) || v.zoneNum != zoneNum_) continue;
                float zones[16];
                const int n = spec_.getFloats(v, "newZones", zones, 16);
                for (int j = 0; j < n && j < 16 && numExtra < 64; ++j)
                    extra[numExtra++] = uint16_t(kZoneBase + int(zones[j]));
            }
        }
        // enterTrigger: bumping a closed door's leaves asks the AI to open it.
        if ((d.state == 4 || d.state == 0) && nowMs - doorRequestMs_ > 1500) {
            float l[3];
            toLocal(e->world, x, y, z, l);
            const float reach = kToonRadius + 0.6f;
            if (l[0] > doorBox_[0] && l[0] < doorBox_[3] && l[1] > doorBox_[1] - reach && l[1] < doorBox_[4] + reach &&
                l[2] > doorBox_[2] - 1.0f && l[2] < doorBox_[5]) {
                client.sendLevelDoorOpen(d.doId);
                doorRequestMs_ = nowMs;
                TT_LOG("level: requestOpen door %u (ent %u)", unsigned(d.doId), unsigned(d.entId));
            }
        }
    }
    if (!floor_) rooms_[0].zone.setExtraVisible(extra, numExtra);
    doorNowMsPrev_ = nowMs;
}

void Level::addSolid(const float world[12], const float lo[3], const float hi[3], bool wall, bool floor) {
    if (numSolids_ >= kMaxSolids) return;
    Solid& b = solids_[numSolids_++];
    memcpy(b.world, world, sizeof(b.world));
    memcpy(b.lo, lo, sizeof(b.lo));
    memcpy(b.hi, hi, sizeof(b.hi));
    b.motion[0] = b.motion[1] = b.motion[2] = 0.0f;
    b.wall = wall, b.floor = floor;
    b.disc = false;
}

void Level::addDisc(const float world[12], float radius, float y0, float y1) {
    if (numSolids_ >= kMaxSolids) return;
    Solid& b = solids_[numSolids_++];
    memcpy(b.world, world, sizeof(b.world));
    b.lo[0] = -radius, b.lo[1] = y0, b.lo[2] = -radius;
    b.hi[0] = radius, b.hi[1] = y1, b.hi[2] = radius;
    b.motion[0] = b.motion[1] = b.motion[2] = 0.0f;
    b.wall = b.floor = true;
    b.disc = true;
}

bool Level::floorThunk(void* self, float x, float y, float z, float* top) {
    const Level& l = *static_cast<const Level*>(self);
    bool found = false;
    for (int i = 0; i < l.numSolids_; ++i) {
        const Solid& b = l.solids_[i];
        if (!b.floor) continue;
        float p[3];
        toLocal(b.world, x, y, z, p);
        if (p[0] < b.lo[0] || p[0] > b.hi[0] || p[1] < b.lo[1] || p[1] > b.hi[1]) continue;
        // A disc's top under x is on its circle; a box's is its top face.
        const float r = b.hi[0];
        const float local[3] = {p[0], p[1], b.disc ? std::sqrt(std::max(0.0f, r * r - p[0] * p[0])) : b.hi[2]};
        float w[3];
        fromLocal(b.world, local, w);
        if (w[2] > z + kFloorReach || w[2] < z - 20.0f) continue; // above the step-up reach / far below
        if (!found || w[2] > *top) *top = w[2], found = true;
    }
    return found;
}

void Level::pushThunk(void* self, float* x, float* y, float z, float radius) {
    const Level& l = *static_cast<const Level*>(self);
    for (int i = 0; i < l.numSolids_; ++i) {
        const Solid& b = l.solids_[i];
        if (!b.wall) continue;
        float p[3];
        toLocal(b.world, *x, *y, z, p);
        if (b.disc) {
            // Inside a vertical gear below its rim: out of its thickness band along local Y.
            const float r = b.hi[0];
            if (p[0] * p[0] + p[2] * p[2] > r * r) continue;
            if (p[2] > std::sqrt(std::max(0.0f, r * r - p[0] * p[0])) - kFloorReach * 0.25f) continue;
        } else if (p[2] > b.hi[2] - kFloorReach * 0.25f || p[2] + 2.0f * radius < b.lo[2]) {
            continue; // only what the toon cannot step onto and that reaches its body
        }
        const float* ax = b.world;
        const float* ay = b.world + 3;
        const float sx = std::sqrt(ax[0] * ax[0] + ax[1] * ax[1] + ax[2] * ax[2]);
        const float sy = std::sqrt(ay[0] * ay[0] + ay[1] * ay[1] + ay[2] * ay[2]);
        if (sx < 1e-6f || sy < 1e-6f) continue;
        const float rx = radius / sx, ry = radius / sy; // the toon radius in local units
        const float lx0 = b.lo[0] - rx, lx1 = b.hi[0] + rx, ly0 = b.lo[1] - ry, ly1 = b.hi[1] + ry;
        if (p[0] <= lx0 || p[0] >= lx1 || p[1] <= ly0 || p[1] >= ly1) continue;
        // Out along the shallowest side.
        const float pen[4] = {(p[0] - lx0) * sx, (lx1 - p[0]) * sx, (p[1] - ly0) * sy, (ly1 - p[1]) * sy};
        int side = b.disc ? (pen[2] < pen[3] ? 2 : 3) : 0;
        for (int k = 1; k < 4 && !b.disc; ++k)
            if (pen[k] < pen[side]) side = k;
        const float* axis = side < 2 ? ax : ay;
        const float len = side < 2 ? sx : sy;
        const float d = (side == 0 || side == 2 ? -pen[side] : pen[side]) / len;
        *x += axis[0] / len * d, *y += axis[1] / len * d;
    }
}

bool Level::carryThunk(void* self, float x, float y, float z, float* dx, float* dy, float* dz) {
    const Level& l = *static_cast<const Level*>(self);
    for (int i = 0; i < l.numSolids_; ++i) {
        const Solid& b = l.solids_[i];
        if (!b.floor || (b.motion[0] == 0 && b.motion[1] == 0 && b.motion[2] == 0)) continue;
        float p[3];
        toLocal(b.world, x - b.motion[0], y - b.motion[1], z - b.motion[2], p);
        if (p[0] < b.lo[0] || p[0] > b.hi[0] || p[1] < b.lo[1] || p[1] > b.hi[1]) continue;
        if (std::fabs(p[2] - b.hi[2]) > 2.0f) continue; // standing on its top (a fast lift moves ~1.2/frame)
        *dx = b.motion[0], *dy = b.motion[1], *dz = b.motion[2];
        return true;
    }
    return false;
}

void Level::updateButtons(net::Client& client, float x, float y, float z) {
    uint32_t touching = 0;
    for (int i = 0; i < net::kMaxLevelEntities; ++i) {
        const net::LevelEntityObj& d = client.levelEntitySlot(i);
        if (!d.active || d.classId != dc::DistributedButton::kClassId) continue;
        const LevelEntity* e = specOf(d.levelDoId, d.entId);
        if (!e || !shown(e->room)) continue;
        addSolid(e->world, buttonBox_, buttonBox_ + 3, false, true); // its top is a floor
        // DistributedButton.setupSwitch: an intangible sphere (r 0.5 at z 0.5) the toon's body
        // sphere (r kToonRadius at its middle) enters.
        const float local[3] = {0.0f, 0.0f, 0.5f};
        float c[3];
        LevelSpec::toWorld(*e, local, c);
        const float scale = std::sqrt(e->world[0] * e->world[0] + e->world[1] * e->world[1] + e->world[2] * e->world[2]);
        const float dx = x - c[0], dy = y - c[1], dz = z + kToonRadius - c[2];
        const float reach = 0.5f * scale + kToonRadius;
        if (dx * dx + dy * dy + dz * dz < reach * reach) touching = d.doId;
        // switchOnTrack / switchOffTrack sounds when the AI changes the switch state.
        const uint8_t state = !strcmp(d.stateName, "playing") ? 2 : !strcmp(d.stateName, "attract") ? 1 : 0;
        if (entityState_[i] != 0 && state != entityState_[i]) {
            if (state == 2) playAt(kSfxSwitchOn, e->world + 9, 0.9f);
            else if (state == 1) playAt(kSfxSwitchOff, e->world + 9, 1.0f);
        }
        entityState_[i] = state ? state : 1;
    }
    if (touching != onButton_) {
        // DistributedSwitch.enterTrigger / exitTrigger.
        if (onButton_) client.sendLevelInteract(onButton_, false);
        if (touching) client.sendLevelInteract(touching, true);
        TT_LOG("level: switch %u -> %u", unsigned(onButton_), unsigned(touching));
        onButton_ = touching;
    }
}

void Level::drawButtons(render::Renderer& renderer) const {
    if (!doorClient_ || !buttonBase_.loaded() || !buttonTop_.loaded()) return;
    for (int i = 0; i < net::kMaxLevelEntities; ++i) {
        const net::LevelEntityObj& d = doorClient_->levelEntitySlot(i);
        if (!d.active || d.classId != dc::DistributedButton::kClassId) continue;
        const LevelEntity* e = specOf(d.levelDoId, d.entId);
        if (!e || !shown(e->room)) continue;
        C3D_Mtx m;
        toMtx(e->world, &m);
        renderer.drawModel(const_cast<render::Model&>(buttonBase_), m);
        // switchOnTrack: down 0.2 over 0.8 s (easeInOut), green over the second half;
        // switchOffTrack ("attract"): back up over 1.0 s, its colour over the second half.
        float color[4] = {1, 1, 1, 1};
        spec_.getFloats(*e, "color", color, 4);
        const float t = int32_t(doorNowMs_ - d.stateMs) / 1000.0f;
        const bool on = !strcmp(d.stateName, "playing");
        const bool off = !strcmp(d.stateName, "attract");
        float down = 0, green = 0;
        if (on) {
            const float u = clamp01(t / 0.8f);
            down = u * u * (3 - 2 * u);
            green = easeOut(clamp01((t - 0.4f) / 0.4f));
        } else if (off) {
            const float u = clamp01(t / 1.0f);
            down = 1.0f - u * u * (3 - 2 * u);
            green = 1.0f - easeIn(clamp01((t - 0.5f) / 0.5f));
        }
        const float tint[4] = {color[0] * (1 - green), color[1] * (1 - green) + green, color[2] * (1 - green), color[3]};
        renderer.setBaseTint(tint[0], tint[1], tint[2], tint[3]);
        Mtx_Translate(&m, 0, 0, -0.2f * down, true);
        renderer.drawModel(const_cast<render::Model&>(buttonTop_), m);
        renderer.setBaseTint(1, 1, 1, 1);
    }
}

void Level::playAt(int sfx, const float pos[3], float volume) const {
    if (sfx < 0 || sfx >= kSfxCount || sfx_[sfx] == audio::kInvalidSfx) return;
    // Positional like SoundInterval(node=...): full within 20 units, then 1/d.
    const float dx = pos[0] - toonX_, dy = pos[1] - toonY_, dz = pos[2] - toonZ_;
    const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
    const float gain = d < 20.0f ? 1.0f : 20.0f / d;
    if (gain * volume < 0.03f) return;
    audio::playSfx(sfx_[sfx], volume * gain);
}

// DistributedStomper.getMotionIval: the model's Y as a fraction of -range ("raise").
Level::StomperPose Level::stomperPose(const LevelEntity& e, const net::LevelEntityObj& d, uint32_t nowMs) const {
    StomperPose p;
    const char* style = spec_.string(e, "style");
    p.vertical = !style || strcmp(style, "horizontal") != 0;
    p.style = spec_.integer(e, "cogStyle", 0) == 1 ? 1 : 0;
    float v[3];
    if (spec_.getFloats(e, "headScale", v, 3) >= 3) memcpy(p.headScale, v, 12);
    if (spec_.getFloats(e, "shaftScale", v, 3) >= 3) memcpy(p.shaftScale, v, 12);
    const int motion = spec_.integer(e, "motion", 0);
    const float period = spec_.number(e, "period", 2.0f);
    if (motion == 5) { // MotionSwitched: STOMP falls in 0.35 s, RISE climbs in 0.5 s (easeInOut)
        const float t = int32_t(nowMs - d.movieMs) / 1000.0f;
        if (d.movie == 4) {
            const float u = clamp01(t / 0.35f);
            p.raise = 1.0f - u * u * (3 - 2 * u);
        } else if (d.movie == 5) {
            const float u = clamp01(t / 0.5f);
            p.raise = u * u * (3 - 2 * u);
        } else {
            p.raise = 1.0f; // loadModel: setPos(0, -range, 0)
        }
        return p;
    }
    if (d.movie != 0 || period <= 0.0f) return p; // only STOMPER_START loops
    // ival.setT(frameTime - level.startTime + period * phaseShift), looping.
    const float t = levelTime(nowMs, e) + period * spec_.number(e, "phaseShift", 0.0f);
    float u = std::fmod(t / period, 1.0f);
    if (u < 0) u += 1.0f;
    switch (motion) {
    case 0: // MotionLinear: up 1/2, hold 1/4, down 1/4
        p.raise = u < 0.5f ? u / 0.5f : u < 0.75f ? 1.0f : 1.0f - (u - 0.75f) / 0.25f;
        break;
    case 1: // MotionSinus
        p.raise = 0.5f - 0.5f * std::cos(2.0f * 3.14159265f * u);
        break;
    case 3: // MotionSlowFast: stick 0.2, rise to 0.95, slam back
        p.raise = u < 0.2f ? 0.0f : u < 0.95f ? (u - 0.2f) / 0.75f : 1.0f - (u - 0.95f) / 0.05f;
        break;
    case 4: { // MotionCrush: stick 0.2, up 0.45 easeInOut, pause 0.2, down 0.15 easeInOut
        auto smooth = [](float x) { return x * x * (3 - 2 * x); };
        p.raise = u < 0.2f ? 0.0f : u < 0.65f ? smooth((u - 0.2f) / 0.45f) : u < 0.85f ? 1.0f
                                                                            : 1.0f - smooth((u - 0.85f) / 0.15f);
        break;
    }
    default: // MotionHalfSinus
        p.raise = std::sin(u * 3.14159265f);
        break;
    }
    return p;
}

void Level::updateStompers(net::Client& client, float x, float y, float z, uint32_t nowMs) {
    for (int i = 0; i < net::kMaxLevelEntities; ++i) {
        const net::LevelEntityObj& d = client.levelEntitySlot(i);
        if (!d.active || d.classId != dc::DistributedStomper::kClassId) continue;
        const LevelEntity* e = specOf(d.levelDoId, d.entId);
        if (!e || !shown(e->room)) continue;
        const StomperPose p = stomperPose(*e, d, nowMs);
        const StomperShape& st = stompers_[p.style];
        const float range = spec_.number(*e, "range", 6.0f);
        const float lift = range * p.raise;
        // The head as a solid box in entity space (a wall, and a floor on top).
        float lo[3], hi[3];
        if (p.vertical) { // rotateNode P = -90: model -Y is up, model Z is entity Y
            lo[0] = st.lo[0] * p.headScale[0], hi[0] = st.hi[0] * p.headScale[0];
            lo[1] = st.lo[2] * p.headScale[2], hi[1] = st.hi[2] * p.headScale[2];
            lo[2] = lift - st.hi[1] * p.headScale[1], hi[2] = lift - st.lo[1] * p.headScale[1];
        } else { // horizontal: the head slides along -Y
            lo[0] = st.lo[0] * p.headScale[0], hi[0] = st.hi[0] * p.headScale[0];
            lo[1] = st.lo[1] * p.headScale[1] - lift, hi[1] = st.hi[1] * p.headScale[1] - lift;
            lo[2] = st.lo[2] * p.headScale[2], hi[2] = st.hi[2] * p.headScale[2];
        }
        const int before = numSolids_;
        addSolid(e->world, lo, hi, true, true);
        if (numSolids_ > before && p.vertical) solids_[before].motion[2] = lift - entityPrev_[i]; // carry riders
        entityPrev_[i] = lift;
        // The end of each cycle: the stomp (sound, checkSquashedToon).
        const float period = spec_.number(*e, "period", 2.0f);
        int cycle = 0;
        if (d.movie == 0 && period > 0.0f && spec_.integer(*e, "motion", 0) != 5) {
            const float t = levelTime(nowMs, *e) + period * spec_.number(*e, "phaseShift", 0.0f);
            cycle = int(std::floor(t / period));
        } else if (spec_.integer(*e, "motion", 0) == 5) {
            cycle = d.movie == 4 && int32_t(nowMs - d.movieMs) >= 0 ? int(d.movieMs | 1) : 0;
        }
        if (cycle != entityCycle_[i]) {
            const bool first = entityCycle_[i] == 0;
            entityCycle_[i] = cycle;
            if (!first && cycle != 0) {
                if (spec_.integer(*e, "soundOn", 0)) {
                    const int path = spec_.integer(*e, "soundPath", 0);
                    playAt(kSfxStompSmall + (path < 0 ? 0 : path > 2 ? 2 : path), e->world + 9, 0.45f);
                }
                if (p.vertical && !stunned_) {
                    float l[3];
                    toLocal(e->world, x, y, z, l);
                    if (std::fabs(l[0]) < p.headScale[0] && std::fabs(l[1]) < p.headScale[2] && l[2] < 5.0f &&
                        l[2] > -0.5f) {
                        squishDamage_ = spec_.integer(*e, "damage", 1);
                        squishZ_ = e->world[11] + 0.025f;
                        client.sendLevelOuch(rooms_[e->room].doId, uint8_t(squishDamage_));
                        stunned_ = true;
                        TT_LOG("level: squished by stomper %u (%d laff)", unsigned(d.entId), squishDamage_);
                    }
                }
            }
        }
    }
}

void Level::drawStompers(render::Renderer& renderer) const {
    if (!doorClient_) return;
    for (int i = 0; i < net::kMaxLevelEntities; ++i) {
        const net::LevelEntityObj& d = doorClient_->levelEntitySlot(i);
        if (!d.active || d.classId != dc::DistributedStomper::kClassId) continue;
        const LevelEntity* e = specOf(d.levelDoId, d.entId);
        if (!e || !shown(e->room)) continue;
        const StomperPose p = stomperPose(*e, d, doorNowMs_);
        StomperShape& st = const_cast<StomperShape&>(stompers_[p.style]);
        C3D_Mtx m;
        toMtx(e->world, &m);
        if (p.vertical) Mtx_RotateX(&m, -90.0f * kDegToRad, true); // rotateNode.setP(-90)
        Mtx_Translate(&m, 0, -spec_.number(*e, "range", 6.0f) * p.raise, 0, true);
        Mtx_Scale(&m, p.headScale[0], p.headScale[1], p.headScale[2]);
        if (st.head.loaded()) renderer.drawModel(st.head, m);
        Mtx_Translate(&m, st.shaftOffset[0], st.shaftOffset[1], st.shaftOffset[2], true);
        Mtx_Scale(&m, p.shaftScale[0], p.shaftScale[1], p.shaftScale[2]);
        if (st.shaft.loaded()) renderer.drawModel(st.shaft, m);
    }
}

// DistributedLift.enterMoving: LerpPos(platform, duration, endPos, startPos, easeInOut) that
// ends at the arrival timestamp. The platform sits under the entity, scaled by modelScale.
bool Level::liftPose(const LevelEntity& e, const net::LevelEntityObj& d, uint32_t nowMs, float world[12],
                     bool* moving) const {
    float start[3] = {0, 0, 0}, end[3] = {0, 0, 0}, scale[3] = {1, 1, 1};
    spec_.getFloats(e, "startPos", start, 3);
    spec_.getFloats(e, "endPos", end, 3);
    if (spec_.getFloats(e, "modelScale", scale, 3) == 1) scale[1] = scale[2] = scale[0];
    const float duration = spec_.number(e, "duration", 1.0f);
    const float* from = d.fromState == 0 ? start : end; // LiftConstants.Down = 0
    const float* to = d.state == 0 ? start : end;
    const float left = int32_t(d.arrivalMs - nowMs) / 1000.0f;
    const float u = duration > 0.0f ? clamp01(1.0f - left / duration) : 1.0f;
    const float k = u * u * (3 - 2 * u);
    *moving = u < 1.0f;
    float p[3];
    for (int c = 0; c < 3; ++c) p[c] = from[c] + (to[c] - from[c]) * k;
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) world[r * 3 + c] = e.world[r * 3 + c] * scale[r];
    LevelSpec::toWorld(e, p, world + 9);
    return true;
}

void Level::updateLifts(net::Client& client, float x, float y, float z, uint32_t nowMs) {
    uint32_t riding = 0;
    for (int i = 0; i < net::kMaxLevelEntities; ++i) {
        const net::LevelEntityObj& d = client.levelEntitySlot(i);
        const LevelEntity* e = d.active && d.classId == dc::DistributedLift::kClassId ? specOf(d.levelDoId, d.entId)
                                                                                      : nullptr;
        if (!e || !shown(e->room)) {
            if (liftVoice_[i]) { audio::stopVoice(liftVoice_[i]); liftVoice_[i] = 0; }
            continue;
        }
        float w[12];
        bool moving;
        liftPose(*e, d, nowMs, w, &moving);
        // The floor carries riders; the walls keep them in (and out) like the board collisions.
        const int floorIndex = numSolids_;
        float floorLo[3], floorHi[3];
        memcpy(floorLo, liftBoxes_[kLiftFloor], 12);
        memcpy(floorHi, liftBoxes_[kLiftFloor] + 3, 12);
        addSolid(w, floorLo, floorHi, false, true);
        if (numSolids_ > floorIndex) {
            for (int c = 0; c < 3; ++c) solids_[floorIndex].motion[c] = w[9 + c] - liftPrev_[i][c];
            if (liftPrev_[i][0] == 0 && liftPrev_[i][1] == 0 && liftPrev_[i][2] == 0)
                solids_[floorIndex].motion[0] = solids_[floorIndex].motion[1] = solids_[floorIndex].motion[2] = 0;
        }
        memcpy(liftPrev_[i], w + 9, 12);
        // At rest in a state its board sides (startBoardSides / endBoardSides) are open; moving,
        // every wall is up (startMoving unstashes, doneMoving stashes the arrival side).
        static const char* const kSide[4] = {"front", "back", "left", "right"};
        for (int k = 0; k < 4; ++k) {
            bool open = false;
            if (!moving) {
                const char* sides = spec_.string(*e, d.state == 0 ? "startBoardSides" : "endBoardSides");
                open = sides && strstr(sides, kSide[k]); // comma-joined (tlv.py)
            }
            if (!open) addSolid(w, liftBoxes_[kLiftFront + k], liftBoxes_[kLiftFront + k] + 3, true, false);
        }
        // Guards (level collision moved here by bake_level.py, boxes in level-root space): the far
        // end's while resting, both while moving.
        float box[6];
        for (int g = 0; g < 2; ++g) {
            const bool active = moving || (g == 0 ? d.state != 0 : d.state == 0);
            if (active && spec_.getFloats(*e, g == 0 ? "startGuardBox" : "endGuardBox", box, 6) == 6)
                addSolid(rooms_[e->room].placement, box, box + 3, true, false);
        }
        // setAvatarEnter / setAvatarLeave: standing on the lift floor.
        float l[3];
        toLocal(w, x, y, z, l);
        // (Hysteresis: on within 1 of the floor, off only past 3, so riding never flaps.)
        const float reach = onLift_ == d.doId ? 3.0f : 1.0f;
        if (l[0] > floorLo[0] && l[0] < floorHi[0] && l[1] > floorLo[1] && l[1] < floorHi[1] && l[2] > -reach &&
            l[2] < reach)
            riding = d.doId;
        // CHQ_FACT_elevator_up_down loops while it moves.
        if (moving && !liftVoice_[i] && sfx_[kSfxLift] != audio::kInvalidSfx) {
            liftVoice_[i] = audio::playSfx(sfx_[kSfxLift], 0.8f, 0.0f, true);
        } else if (!moving && liftVoice_[i]) {
            audio::stopVoice(liftVoice_[i]);
            liftVoice_[i] = 0;
        }
    }
    if (riding != onLift_) {
        if (onLift_) client.sendLevelLiftRider(onLift_, false);
        if (riding) client.sendLevelLiftRider(riding, true);
        TT_LOG("level: lift %u -> %u", unsigned(onLift_), unsigned(riding));
        onLift_ = riding;
    }
}

void Level::drawLifts(render::Renderer& renderer) const {
    if (!doorClient_ || !liftModel_.loaded()) return;
    for (int i = 0; i < net::kMaxLevelEntities; ++i) {
        const net::LevelEntityObj& d = doorClient_->levelEntitySlot(i);
        if (!d.active || d.classId != dc::DistributedLift::kClassId) continue;
        const LevelEntity* e = specOf(d.levelDoId, d.entId);
        if (!e || !shown(e->room)) continue;
        float w[12];
        bool moving;
        liftPose(*e, d, doorNowMs_, w, &moving);
        C3D_Mtx m;
        toMtx(w, &m);
        renderer.drawModel(const_cast<render::Model&>(liftModel_), m);
    }
}

namespace {

// ElevatorConstants.ElevatorData[ELEVATOR_STAGE / ELEVATOR_COUNTRY_CLUB] and ElevatorPoints.
struct ElevatorType {
    float doorTime;    // openTime / closeTime
    float doorWidth;   // width: a closed door's X offset
    float boardRadius; // collRadius: DistributedElevatorFSM.setupElevator's sphere at (0, 5, 0)
};
constexpr ElevatorType kStageElevator = {4.0f, 6.5f, 9.5f};
constexpr ElevatorType kClubElevator = {2.0f, 5.875f, 4.0f};
const ElevatorType& elevatorType(const net::LevelElevator& e) { return e.club ? kClubElevator : kStageElevator; }
constexpr float kElevatorPoints[8][3] = {{-1.5f, 5, 0.1f}, {1.5f, 5, 0.1f}, {-2.5f, 3, 0.1f}, {2.5f, 3, 0.1f},
                                         {-3.5f, 5, 0.1f}, {3.5f, 5, 0.1f}, {-4, 3, 0.1f},     {4, 3, 0.1f}};

} // namespace

// DistributedElevatorFloor.set2Latch: the model is parented to its elevatorMarker entity.
const LevelEntity* Level::elevatorMarker(const net::LevelElevator& e) const {
    if (!doorClient_ || !e.latch) return nullptr;
    for (int i = 0; i < net::kMaxLevelEntities; ++i) {
        const net::LevelEntityObj& d = doorClient_->levelEntitySlot(i);
        if (d.active && d.doId == e.latch) return specOf(d.levelDoId, d.entId);
    }
    return nullptr;
}

// ElevatorUtils.getOpenInterval / getCloseInterval (easeOut over openTime / closeTime).
float Level::elevatorDoors(const net::LevelElevator& e, uint32_t nowMs) const {
    const float t = clamp01(int32_t(nowMs - e.stateMs) / 1000.0f / elevatorType(e).doorTime);
    if (!strcmp(e.state, "Opening")) return 1.0f - easeOut(t);
    if (!strcmp(e.state, "Closing")) return easeOut(t);
    if (!strcmp(e.state, "WaitEmpty") || !strcmp(e.state, "WaitCountdown") || !strcmp(e.state, "AllAboard"))
        return 0.0f;
    return 1.0f; // Closed / Off
}

void Level::updateElevators(net::Client& client, float x, float y, float z, uint32_t nowMs) {
    const uint32_t me = client.avatarId();
    for (int i = 0; i < net::kMaxLevelElevators; ++i) {
        const net::LevelElevator& e = client.levelElevatorSlot(i);
        const LevelEntity* m = e.active ? elevatorMarker(e) : nullptr;
        if (e.active && !m && nowMs - elevatorUnresolvedMs_ > 5000) {
            // Every 5 s while it lasts: why the elevator's marker does not resolve.
            elevatorUnresolvedMs_ = nowMs;
            int markers = 0;
            for (int k = 0; k < net::kMaxLevelEntities; ++k) {
                const net::LevelEntityObj& d = client.levelEntitySlot(k);
                if (!d.active || d.classId != dc::DistributedElevatorMarker::kClassId) continue;
                ++markers;
                TT_LOG("level: marker %u level %u ent %u room %d%s", unsigned(d.doId), unsigned(d.levelDoId),
                       unsigned(d.entId), roomOf(d.levelDoId), d.doId == e.latch ? " (latch)" : "");
            }
            TT_LOG("level: elevator %u latch %u unresolved, %d marker(s)", unsigned(e.doId), unsigned(e.latch),
                   markers);
        }
        if (!m || !shown(m->room)) continue;
        const bool closed = elevatorDoors(e, nowMs) > 0.99f;
        for (int k = 0; k < numElevatorBoxes_; ++k) {
            const char kind = elevatorBoxKind_[k];
            if (kind == 'd' && !closed) continue;
            addSolid(m->world, elevatorBoxes_[k], elevatorBoxes_[k] + 3, kind != 'f', kind == 'f');
        }
        int seatIndex = -1;
        for (int k = 0; k < 8; ++k)
            if (e.slots[k] == me && me) seatIndex = k;
        if (seatIndex >= 0 && boardedElevator_ != e.doId) {
            // fillSlot: the toon takes its seat (ElevatorPoints[index]) facing out of the doors.
            boardedElevator_ = e.doId;
            LevelSpec::toWorld(*m, kElevatorPoints[seatIndex], seat_);
            seat_[3] = LevelSpec::worldHeading(*m) + 180.0f;
            seatPending_ = true;
            TT_LOG("level: aboard floor elevator %u, seat %d", unsigned(e.doId), seatIndex);
        } else if (seatIndex < 0 && boardedElevator_ == e.doId) {
            boardedElevator_ = 0;
        }
        // The board sphere (collRadius at (0, 5, 0)) while the doors are open and it takes riders.
        const bool boarding = !e.locked && (!strcmp(e.state, "WaitEmpty") || !strcmp(e.state, "WaitCountdown"));
        if (seatIndex < 0 && boarding && nowMs - elevatorRequestMs_ > 3000) {
            const float local[3] = {0, 5, 0};
            float c[3];
            LevelSpec::toWorld(*m, local, c);
            const float dx = x - c[0], dy = y - c[1], dz = z - c[2];
            const float r = elevatorType(e).boardRadius;
            if (dx * dx + dy * dy + dz * dz < r * r &&
                client.sendLevelElevatorBoard(e.doId))
                elevatorRequestMs_ = nowMs;
        }
    }
}

void Level::drawElevators(render::Renderer& renderer) const {
    if (!doorClient_ || !elevatorFrame_.loaded()) return;
    for (int i = 0; i < net::kMaxLevelElevators; ++i) {
        const net::LevelElevator& e = doorClient_->levelElevatorSlot(i);
        const LevelEntity* m = e.active ? elevatorMarker(e) : nullptr;
        if (!m || !shown(m->room)) continue;
        C3D_Mtx root, door;
        toMtx(m->world, &root);
        renderer.drawModel(const_cast<render::Model&>(elevatorFrame_), root);
        // getLeftClosePoint (width, 0, 0) / getRightClosePoint (-width, 0, 0); open at the origin.
        const float shut = elevatorDoors(e, doorNowMs_) * elevatorType(e).doorWidth;
        door = root;
        Mtx_Translate(&door, shut, 0, 0, true);
        if (elevatorLeft_.loaded()) renderer.drawModel(const_cast<render::Model&>(elevatorLeft_), door);
        door = root;
        Mtx_Translate(&door, -shut, 0, 0, true);
        if (elevatorRight_.loaded()) renderer.drawModel(const_cast<render::Model&>(elevatorRight_), door);
    }
}

bool Level::elevatorPosition(float* x, float* y, float* z, float* h) const {
    for (int i = 0; active_ && doorClient_ && i < net::kMaxLevelElevators; ++i) {
        const net::LevelElevator& e = doorClient_->levelElevatorSlot(i);
        const LevelEntity* m = e.active ? elevatorMarker(e) : nullptr;
        if (!m) continue;
        const float local[3] = {0, 2, 0}; // just inside the doors
        float w[3];
        LevelSpec::toWorld(*m, local, w);
        *x = w[0], *y = w[1], *z = w[2], *h = LevelSpec::worldHeading(*m);
        return true;
    }
    return false;
}

namespace {

constexpr float kBarrelScale = 0.5f;   // DistributedBarrelBase.barrelScale
constexpr float kBarrelSphere = 3.2f;  // ... sphereRadius (in barrel space)

bool isBarrel(uint16_t classId) {
    return classId == dc::DistributedGagBarrel::kClassId || classId == dc::DistributedBeanBarrel::kClassId ||
           classId == dc::DistributedHealBarrel::kClassId;
}

} // namespace

void Level::updateBarrels(net::Client& client, float x, float y, float z, uint32_t nowMs) {
    for (int i = 0; i < net::kMaxLevelEntities; ++i) {
        const net::LevelEntityObj& d = client.levelEntitySlot(i);
        if (!d.active || !isBarrel(d.classId)) continue;
        const LevelEntity* e = specOf(d.levelDoId, d.entId);
        if (!e || !shown(e->room)) continue;
        float lo[3], hi[3];
        for (int c = 0; c < 3; ++c) lo[c] = barrelBox_[c] * kBarrelScale, hi[c] = barrelBox_[3 + c] * kBarrelScale;
        addSolid(e->world, lo, hi, true, true);
        if (d.grabbedByUs || nowMs - barrelRequestMs_ < 1000) continue; // one grab per toon
        // handleEnterSphere: the intangible sphere (r 3.2 x barrelScale) meets GravityWalker's event
        // sphere (centre r - 0.1, radius r x 1.04). Panda raises the event before its pusher
        // resolves the frame's motion, so a toon walking in overlaps by about one frame of
        // movement (forward 16/s at 30 fps); we test after resolving, hence the margin.
        const float scale = std::sqrt(e->world[0] * e->world[0] + e->world[1] * e->world[1] + e->world[2] * e->world[2]);
        const float dx = x - e->world[9], dy = y - e->world[10], dz = z + kToonRadius - 0.1f - e->world[11];
        const float reach = kBarrelSphere * kBarrelScale * scale + kToonRadius * 1.04f + 16.0f / 30.0f;
        if (dx * dx + dy * dy + dz * dz < reach * reach) {
            client.sendLevelBarrelGrab(d.doId);
            barrelRequestMs_ = nowMs;
            TT_LOG("level: requestGrab barrel %u (ent %u)", unsigned(d.doId), unsigned(d.entId));
        }
        if (d.grabMs && entityState_[i] != 9) { // setGrab arrived: the grab sound
            playAt(kSfxBarrelGrab, e->world + 9, 1.0f);
            entityState_[i] = 9;
        }
    }
}

void Level::drawBarrels(render::Renderer& renderer) const {
    if (!doorClient_ || !barrelModel_.loaded()) return;
    for (int i = 0; i < net::kMaxLevelEntities; ++i) {
        const net::LevelEntityObj& d = doorClient_->levelEntitySlot(i);
        if (!d.active || !isBarrel(d.classId)) continue;
        const LevelEntity* e = specOf(d.levelDoId, d.entId);
        if (!e || !shown(e->room)) continue;
        // setGrab: scale to 1.1x and back over 0.4 s; greyed for the toon who grabbed it.
        float s = kBarrelScale;
        if (d.grabMs) {
            const float t = int32_t(doorNowMs_ - d.grabMs) / 1000.0f;
            auto smooth = [](float u) { return u * u * (3 - 2 * u); };
            if (t < 0.2f) s *= 1.0f + 0.1f * smooth(t / 0.2f);
            else if (t < 0.4f) s *= 1.1f - 0.1f * smooth((t - 0.2f) / 0.2f);
        }
        const float grey = d.grabbedByUs ? 0.5f : 1.0f;
        C3D_Mtx m;
        toMtx(e->world, &m);
        Mtx_Scale(&m, s, s, s);
        renderer.setBaseTint(grey, grey, grey, 1.0f);
        renderer.drawModel(const_cast<render::Model&>(barrelModel_), m);
        // The label on gagNode (0, -2.62, 4) in barrel space, colour-scaled (0.7, 0.7, 0.6).
        Mtx_Translate(&m, 0.0f, -2.62f, 4.0f, true);
        renderer.setBaseTint(0.7f * grey, 0.7f * grey, 0.6f * grey, 1.0f);
        const render::Model* label = nullptr;
        float labelScale = 1.0f, labelZ = 0.0f;
        if (d.classId == dc::DistributedGagBarrel::kClassId) {
            // BarrelBase.getGagTrack / getGagLevel: random.Random(entId * level.doId), fresh each.
            const uint64_t seed = uint64_t(e->entId) * rooms_[e->room].doId;
            int track = spec_.integer(*e, "gagTrack", 0);
            const char* trackName = spec_.string(*e, "gagTrack");
            if (trackName && !strcmp(trackName, "random")) {
                static const int kTracks[9] = {0, 1, 2, 3, 4, 4, 5, 5, 6};
                PyRandom rng(seed);
                track = kTracks[rng.randbelow(9)];
            }
            int level = spec_.integer(*e, "gagLevel", 0);
            const int levelMax = spec_.integer(*e, "gagLevelMax", 0);
            if (levelMax > level) {
                PyRandom rng(seed);
                level = rng.randrange(level, levelMax + 1);
            }
            if (track >= 0 && track < 7 && level >= 0 && level < 7) label = &gagIcons_[track][level];
            labelScale = 13.0f;
        } else if (d.classId == dc::DistributedBeanBarrel::kClassId) {
            label = &barrelJar_;
            labelScale = 3.0f;
        } else {
            label = &barrelIcecream_;
            labelScale = 0.6f;
            labelZ = -0.1f - labelScale;
        }
        if (label && label->loaded()) {
            Mtx_Translate(&m, 0.0f, -0.1f, labelZ, true);
            Mtx_Scale(&m, labelScale, labelScale, labelScale);
            renderer.drawModel(const_cast<render::Model&>(*label), m);
        }
        renderer.setBaseTint(1, 1, 1, 1);
    }
}

namespace {

// 3x4 entity-local transform composition: out = w * (rotation r, translation t), all in the
// LevelEntity::world row layout.
void compose(const float w[12], const float r[9], const float t[3], float out[12]) {
    float o[12];
    for (int row = 0; row < 3; ++row)
        for (int c = 0; c < 3; ++c)
            o[row * 3 + c] = r[row * 3 + 0] * w[c] + r[row * 3 + 1] * w[3 + c] + r[row * 3 + 2] * w[6 + c];
    fromLocal(w, t, o + 9);
    memcpy(out, o, sizeof(o));
}

// LerpBlendHelpers by name (PlatformEntity motion).
float blend(const char* mode, float t) {
    if (!mode) return t;
    if (!strcmp(mode, "easeIn")) return easeIn(t);
    if (!strcmp(mode, "easeOut")) return easeOut(t);
    if (!strcmp(mode, "easeInOut")) return t * t * (3 - 2 * t);
    return t;
}

} // namespace

// GearEntity: gearParent (P -90 when vertical) x the spinning model (H rotation) x gearScale,
// the vertical model first raised 0.15 (model.setZ(0.15) before the scale).
void Level::gearMatrix(const LevelEntity& e, float t, C3D_Mtx* m) const {
    const float dps = spec_.number(e, "degreesPerSec", 0.0f);
    const float scale = spec_.number(e, "gearScale", 1.0f);
    const char* orientation = spec_.string(e, "orientation");
    const bool vertical = !orientation || strcmp(orientation, "horizontal") != 0;
    // rotateIval.setT(frameTime - level.startTime + ivalDur * phaseShift) over 360 / |dps| s.
    const float angle = dps * t + (dps < 0 ? -360.0f : 360.0f) * spec_.number(e, "phaseShift", 0.0f);
    toMtx(e.world, m);
    if (vertical) Mtx_RotateX(m, -90.0f * kDegToRad, true);
    Mtx_RotateZ(m, std::fmod(angle, 360.0f) * kDegToRad, true);
    Mtx_Scale(m, scale, scale, scale);
    if (vertical) Mtx_Translate(m, 0, 0, 0.15f, true);
}

// PlatformEntity.start: wait 1/2, out 1/2 of the move, wait, back; looping from the level start.
bool Level::platformOffset(const LevelEntity& e, float t, float out[3]) const {
    float offset[3] = {0, 0, 0};
    spec_.getFloats(e, "offset", offset, 3);
    const float period = spec_.number(e, "period", 0.0f);
    if (period <= 0.0f) {
        out[0] = out[1] = out[2] = 0;
        return false;
    }
    const float wait = period * spec_.number(e, "waitPercent", 0.0f), move = period - wait;
    float u = std::fmod(t + period * spec_.number(e, "phaseShift", 0.0f), period);
    if (u < 0) u += period;
    const char* motion = spec_.string(e, "motion");
    float k;
    if (u < wait * 0.5f) k = 0;
    else if (u < (wait + move) * 0.5f) k = blend(motion, clamp01((u - wait * 0.5f) / (move * 0.5f)));
    else if (u < wait + move * 0.5f) k = 1;
    else k = 1.0f - blend(motion, clamp01((u - wait - move * 0.5f) / (move * 0.5f)));
    for (int c = 0; c < 3; ++c) out[c] = offset[c] * k;
    return true;
}

void Level::updateMovers(float x, float y, float z, uint32_t nowMs) {
    (void)x, (void)y, (void)z;
    int slot = 0;
    for (int i = 0; i < spec_.numEntities(); ++i) {
        const LevelEntity& e = spec_.entity(i);
        if ((e.flags & kLevelSynthetic) || !shown(e.room)) continue;
        const float t = levelTime(nowMs, e);
        if (!strcmp(e.type, "gear")) {
            const char* type = spec_.string(e, "modelType");
            const int k = type && !strcmp(type, "mint") ? 1 : 0;
            const char* orientation = spec_.string(e, "orientation");
            const float scale = spec_.number(e, "gearScale", 1.0f);
            if (!orientation || strcmp(orientation, "horizontal") != 0) {
                // A vertical gear: a disc about entity Y (model Z after the -90 pitch).
                const float radius = std::max(gearBox_[k][3], -gearBox_[k][0]) * scale;
                addDisc(e.world, radius, (gearBox_[k][2] + 0.15f) * scale, (gearBox_[k][5] + 0.15f) * scale);
            } else {
                float lo[3] = {gearBox_[k][0] * scale, gearBox_[k][1] * scale, gearBox_[k][2] * scale};
                float hi[3] = {gearBox_[k][3] * scale, gearBox_[k][4] * scale, gearBox_[k][5] * scale};
                addSolid(e.world, lo, hi, true, true);
            }
        } else if (!strcmp(e.type, "paintMixer")) {
            float off[3];
            platformOffset(e, t, off);
            float scale[3] = {1, 1, 1};
            if (spec_.getFloats(e, "modelScale", scale, 3) == 1) scale[1] = scale[2] = scale[0];
            const float r[9] = {scale[0], 0, 0, 0, scale[1], 0, 0, 0, scale[2]};
            float w[12];
            compose(e.world, r, off, w);
            const int first = numSolids_;
            addSolid(w, mixerFloor_, mixerFloor_ + 3, false, true);
            if (numSolids_ > first && slot < 64) { // carry riders (MovingPlatform)
                for (int c = 0; c < 3; ++c) solids_[first].motion[c] = moverPrev_[slot][c] ? w[9 + c] - moverPrev_[slot][c] : 0;
                memcpy(moverPrev_[slot], w + 9, 12);
            }
            ++slot;
            const float sz = spec_.number(e, "shaftScale", 1.0f);
            for (int k = 0; k < numMixerWalls_; ++k) {
                float lo[3], hi[3];
                memcpy(lo, mixerWalls_[k], 12);
                memcpy(hi, mixerWalls_[k] + 3, 12);
                if (hi[2] <= mixerShaftOrigin_[2] + 0.1f) { // the shaft: stretched about its origin
                    lo[2] = mixerShaftOrigin_[2] + (lo[2] - mixerShaftOrigin_[2]) * sz;
                    hi[2] = mixerShaftOrigin_[2] + (hi[2] - mixerShaftOrigin_[2]) * sz;
                }
                addSolid(w, lo, hi, true, true);
            }
        } else if (!strcmp(e.type, "conveyorBelt")) {
            // The treads as one floor moving along +Y at speed (MovingPlatform per tread upstream).
            const float width = spec_.number(e, "widthScale", 1.0f);
            const float length = spec_.number(e, "length", 10.0f);
            const float tread = spec_.number(e, "treadLength", 10.0f);
            const float speed = spec_.number(e, "speed", 1.0f);
            const float lo[3] = {treadBox_[0] * width, -tread * 0.5f, treadBox_[2]};
            const float hi[3] = {treadBox_[3] * width, length - tread * 0.5f, treadBox_[5]};
            const int first = numSolids_;
            addSolid(e.world, lo, hi, false, true);
            if (numSolids_ > first) {
                const float dt = moverPrevMs_ ? (nowMs - moverPrevMs_) / 1000.0f : 0.0f;
                const float d[3] = {0, speed * (dt > 0.1f ? 0.1f : dt), 0};
                float w0[3], w1[3];
                const float origin[3] = {0, 0, 0};
                fromLocal(e.world, origin, w0);
                fromLocal(e.world, d, w1);
                for (int c = 0; c < 3; ++c) solids_[first].motion[c] = w1[c] - w0[c];
            }
        }
    }
    moverPrevMs_ = nowMs;
}

void Level::drawMovers(render::Renderer& renderer) const {
    C3D_Mtx m;
    for (int i = 0; i < spec_.numEntities(); ++i) {
        const LevelEntity& e = spec_.entity(i);
        if (!shown(e.room)) continue;
        const float t = levelTime(doorNowMs_, e);
        if (!strcmp(e.type, "gear")) {
            const char* type = spec_.string(e, "modelType");
            render::Model& model = const_cast<render::Model&>(gearModels_[type && !strcmp(type, "mint") ? 1 : 0]);
            if (!model.loaded()) continue;
            gearMatrix(e, t, &m);
            renderer.drawModel(model, m);
        } else if (!strcmp(e.type, "paintMixer")) {
            float off[3];
            platformOffset(e, t, off);
            float scale[3] = {1, 1, 1};
            if (spec_.getFloats(e, "modelScale", scale, 3) == 1) scale[1] = scale[2] = scale[0];
            toMtx(e.world, &m);
            Mtx_Translate(&m, off[0], off[1], off[2], true);
            Mtx_Scale(&m, scale[0], scale[1], scale[2]);
            if (mixerTop_.loaded()) renderer.drawModel(const_cast<render::Model&>(mixerTop_), m);
            // PaintMixer.start: shaft.setSz(shaftScale) on PaintMixerBase1.
            Mtx_Translate(&m, mixerShaftOrigin_[0], mixerShaftOrigin_[1], mixerShaftOrigin_[2], true);
            Mtx_Scale(&m, 1, 1, spec_.number(e, "shaftScale", 1.0f));
            if (mixerShaft_.loaded()) renderer.drawModel(const_cast<render::Model&>(mixerShaft_), m);
        } else if (!strcmp(e.type, "conveyorBelt") && treadModel_.loaded()) {
            // ConveyorBelt.start: numTreads = length / treadLength + 3 treads cycling from
            // -treadLength; the clip planes at Y 0 and Y length hide the overhang (here: treads
            // whose middle is outside the belt are not drawn).
            const float length = spec_.number(e, "length", 10.0f);
            const float tread = spec_.number(e, "treadLength", 10.0f);
            const float speed = spec_.number(e, "speed", 1.0f);
            const float width = spec_.number(e, "widthScale", 1.0f);
            const int n = int(length / tread) + 3;
            const float span = n * tread;
            for (int k = 0; k < n; ++k) {
                float yk = std::fmod(k * tread + speed * t, span);
                if (yk < 0) yk += span;
                yk -= tread;
                if (yk < -tread * 0.5f || yk > length - tread * 0.5f) continue;
                toMtx(e.world, &m);
                Mtx_Translate(&m, 0, yk, 0, true);
                Mtx_Scale(&m, width, 1, 1);
                renderer.drawModel(const_cast<render::Model&>(treadModel_), m);
            }
        } else if (!strcmp(e.type, "spinProp") && spinModel(e) && spinModel(e)->loaded()) {
            // PropSpinner: rate turns a minute; axis X spins P, Y spins H, Z spins R.
            const char* axis = spec_.string(e, "axis");
            const float a = spec_.number(e, "rate", 0.0f) * 360.0f * t / 60.0f * kDegToRad;
            toMtx(e.world, &m);
            if (axis && axis[0] == 'X') Mtx_RotateX(&m, a, true);
            else if (axis && axis[0] == 'Y') Mtx_RotateZ(&m, a, true);
            else Mtx_RotateY(&m, a, true);
            renderer.drawModel(const_cast<render::Model&>(*spinModel(e)), m);
        }
    }
}

namespace {

constexpr float kGoonTurnTime = 3.0f;     // GoonGlobals.T_TURN
constexpr float kGoonAnimWalkRate = 2.8f; // GoonGlobals.ANIM_WALK_RATE
constexpr float kGoonStunTime = 4.0f;     // DistributedGoon.__startRecoverTask stunTime
constexpr float kGoonHeadHeight = 3.0f;   // Goon.createHead headHeight (head sphere at +0.8)

// A one-shot clip held on its last frame.
void sampleOnce(const render::Anim& anim, float t, u32* a, u32* b, float* blend) {
    const u32 n = anim.numFrames();
    float f = t * anim.fps();
    if (f < 0) f = 0;
    if (f > float(n - 1)) f = float(n - 1);
    *a = u32(f);
    *b = *a + 1 < n ? *a + 1 : n - 1;
    *blend = f - float(*a);
}

} // namespace

// PathEntity.makePathTrack(goon, velocity, turnTime=T_TURN): per leg a 3 s LerpHpr to the leg's
// heading (from the previous one, numerically) and a LerpPos at the goon's velocity, looping.
bool Level::goonPose(const LevelEntity& e, float walkT, float* x, float* y, float* z, float* h) const {
    const LevelEntity* path = spec_.find(e.room, e.parentEntId);
    if (!path) return false;
    float pts[48];
    const int n = spec_.getFloats(*path, "points", pts, 48) / 3;
    const float v = spec_.number(e, "velocity", 4.0f);
    if (n < 2 || v <= 0) {
        *x = e.world[9], *y = e.world[10], *z = e.world[11], *h = LevelSpec::worldHeading(e);
        return true;
    }
    auto theta = [&](int i) { // node.headsUp(next) then getH() % 360
        const float* a = pts + 3 * i;
        const float* b = pts + 3 * ((i + 1) % n);
        float d = std::atan2(-(b[0] - a[0]), b[1] - a[1]) / kDegToRad;
        return std::fmod(d + 360.0f, 360.0f);
    };
    float total = 0;
    for (int i = 0; i < n; ++i) {
        const float* a = pts + 3 * i;
        const float* b = pts + 3 * ((i + 1) % n);
        total += kGoonTurnTime + std::sqrt((b[0] - a[0]) * (b[0] - a[0]) + (b[1] - a[1]) * (b[1] - a[1]) +
                                          (b[2] - a[2]) * (b[2] - a[2])) / v;
    }
    float t = std::fmod(walkT, total);
    if (t < 0) t += total;
    float local[3] = {pts[0], pts[1], pts[2]};
    float heading = theta(n - 1);
    for (int i = 0; i < n; ++i) {
        const float* a = pts + 3 * i;
        const float* b = pts + 3 * ((i + 1) % n);
        const float from = theta((i + n - 1) % n), to = theta(i);
        if (t < kGoonTurnTime) {
            memcpy(local, a, 12);
            heading = from + (to - from) * (t / kGoonTurnTime);
            break;
        }
        t -= kGoonTurnTime;
        const float dur = std::sqrt((b[0] - a[0]) * (b[0] - a[0]) + (b[1] - a[1]) * (b[1] - a[1]) +
                                    (b[2] - a[2]) * (b[2] - a[2])) / v;
        if (t < dur || i == n - 1) {
            const float u = dur > 0 ? std::min(t / dur, 1.0f) : 1.0f;
            for (int c = 0; c < 3; ++c) local[c] = a[c] + (b[c] - a[c]) * u;
            heading = to;
            break;
        }
        t -= dur;
    }
    float w[3];
    LevelSpec::toWorld(*path, local, w);
    *x = w[0], *y = w[1], *z = w[2];
    *h = heading + LevelSpec::worldHeading(*path);
    return true;
}

float Level::goonTrackTime(const Goon& g, uint32_t nowMs) const {
    // Walking: the track runs on from walkT; every other mode holds the paused time.
    return g.mode == GoonMode::Walk ? g.walkT + int32_t(nowMs - g.modeMs) / 1000.0f : g.walkT;
}

void Level::updateGoons(net::Client& client, float x, float y, float z, uint32_t nowMs) {
    for (int i = 0; i < net::kMaxLevelEntities; ++i) {
        const net::LevelEntityObj& d = client.levelEntitySlot(i);
        Goon& g = goons_[i];
        const LevelEntity* e = d.active && d.classId == dc::DistributedGoon::kClassId ? specOf(d.levelDoId, d.entId)
                                                                                      : nullptr;
        if (!e) {
            g = Goon{};
            continue;
        }
        if (!shown(e->room)) continue;
        if (g.doId != d.doId) {
            g = Goon{};
            g.doId = d.doId;
        }
        // DistributedGoon.setMovie (and its local predictions below).
        if (d.movie != g.lastMovie || d.movieMs != g.lastMovieMs) {
            g.lastMovie = d.movie, g.lastMovieMs = d.movieMs;
            switch (d.movie) {
            case 2: // GOON_MOVIE_BATTLE
                if (g.mode != GoonMode::Battle) {
                    g.walkT = goonTrackTime(g, nowMs);
                    g.mode = GoonMode::Battle, g.modeMs = d.movieMs;
                }
                break;
            case 1: // GOON_MOVIE_STUNNED
                if (g.mode != GoonMode::Stunned) {
                    g.walkT = goonTrackTime(g, nowMs);
                    g.mode = GoonMode::Stunned, g.modeMs = d.movieMs;
                    playAt(kSfxGoonCollapse, e->world + 9, 1.0f);
                }
                break;
            case 3: // GOON_MOVIE_RECOVERY: recover, then walk on from pauseTime
                g.mode = GoonMode::Recovery, g.modeMs = d.movieMs, g.walkT = d.pauseTime;
                playAt(kSfxGoonRecover, e->world + 9, 1.0f);
                break;
            default: // GOON_MOVIE_WALK / SYNC: walk from pauseTime + ts
                g.mode = GoonMode::Walk, g.modeMs = d.movieMs, g.walkT = d.pauseTime;
                break;
            }
        }
        if (g.mode == GoonMode::Recovery && goonClips_[0][kGoonRecovery].loaded() &&
            int32_t(nowMs - g.modeMs) / 1000.0f > goonClips_[0][kGoonRecovery].duration()) {
            g.mode = GoonMode::Walk, g.modeMs = nowMs; // recoveryDone: Walk at pauseTime
        }
        if (d.crushed) g.mode = GoonMode::Off; // crushed by a stomper: gone
        float gx, gy, gz, gh;
        if (!goonPose(*e, goonTrackTime(g, nowMs), &gx, &gy, &gz, &gh)) continue;
        g.x = gx, g.y = gy, g.z = gz, g.h = gh, g.valid = true;
        if (g.mode == GoonMode::Off) continue;
        const float scale = spec_.number(*e, "scale", 1.0f);
        // Its body (cSphere r 1 on the head) is solid, and its head top a floor to land on.
        const float body[12] = {1, 0, 0, 0, 1, 0, 0, 0, 1, gx, gy, gz};
        const float lo[3] = {-scale, -scale, 0}, hi[3] = {scale, scale, (kGoonHeadHeight + 0.8f) * scale};
        addSolid(body, lo, hi, true, true);
        const float dx = x - gx, dy = y - gy, dz = z - gz;
        // __handleStun: landing on its head (the toonSphere at headHeight + 0.8).
        const float top = (kGoonHeadHeight + 0.8f) * scale;
        const bool onHead = dx * dx + dy * dy < (scale + 0.5f) * (scale + 0.5f) && dz > top - 0.6f && dz < top + 1.0f;
        const bool entered = onHead && !g.onHead;
        g.onHead = onHead;
        if (g.mode == GoonMode::Walk || g.mode == GoonMode::Battle) {
            const float radius = spec_.number(*e, "attackRadius", 15.0f);
            if (entered && dx * dx + dy * dy + dz * dz < radius * radius) {
                g.walkT = goonTrackTime(g, nowMs);
                g.mode = GoonMode::Stunned, g.modeMs = nowMs;
                client.sendLevelGoonStunned(d.doId, g.walkT);
                playAt(kSfxGoonCollapse, e->world + 9, 1.0f);
                TT_LOG("level: stunned goon %u", unsigned(d.entId));
                continue;
            }
        }
        // handleToonDetect: we walked into its radar cone (hFov wide, attackRadius long, the beam
        // reaching 3.5 below the eye); the goon zaps us for 'strength'.
        if (g.mode == GoonMode::Walk && !stunned_) {
            const float radius = spec_.number(*e, "attackRadius", 15.0f);
            const float halfFov = spec_.number(*e, "hFov", 70.0f) * 0.5f;
            const float hr = gh * kDegToRad;
            const float fx = -std::sin(hr), fy = std::cos(hr);
            const float dist = std::sqrt(dx * dx + dy * dy);
            const float eyeZ = goonRadar_[2] * scale;
            if (dist < radius && dist > 0.01f && (dx * fx + dy * fy) / dist > std::cos(halfFov * kDegToRad) &&
                dz < eyeZ + 1.0f && dz > -3.5f) {
                g.walkT = goonTrackTime(g, nowMs);
                g.mode = GoonMode::Battle, g.modeMs = nowMs;
                client.sendLevelGoonBattle(d.doId, g.walkT);
                playAt(kSfxGoonAttack, e->world + 9, 0.4f);
                // enterBattle(local): level.b_setOuch(strength) (setOuch + stunToon).
                ouchDamage_ = spec_.integer(*e, "strength", 1);
                client.sendLevelOuch(rooms_[e->room].doId, uint8_t(ouchDamage_));
                stunned_ = true;
                TT_LOG("level: zapped by goon %u (%d laff)", unsigned(d.entId), ouchDamage_);
            }
        }
    }
}

void Level::drawGoons(render::Renderer& renderer) const {
    for (int i = 0; i < net::kMaxLevelEntities; ++i) {
        const Goon& g = goons_[i];
        if (!g.valid || g.mode == GoonMode::Off || !doorClient_) continue;
        const net::LevelEntityObj& d = doorClient_->levelEntitySlot(i);
        const LevelEntity* e = d.active ? specOf(d.levelDoId, d.entId) : nullptr;
        if (!e || !shown(e->room)) continue;
        const char* type = spec_.string(*e, "goonType");
        const int k = type && !strcmp(type, "sg") ? 1 : 0;
        render::Model& model = const_cast<render::Model&>(goonModels_[k]);
        if (!model.loaded()) continue;
        const float scale = spec_.number(*e, "scale", 1.0f);
        C3D_Mtx m;
        Mtx_Identity(&m);
        Mtx_Translate(&m, g.x, g.y, g.z, true);
        Mtx_RotateZ(&m, g.h * kDegToRad, true);
        Mtx_Scale(&m, scale, scale, scale);
        const float t = int32_t(doorNowMs_ - g.modeMs) / 1000.0f;
        const render::Anim* clip = &goonClips_[k][kGoonWalk];
        u32 a = 0, b = 0;
        float blend = 0;
        if (g.mode == GoonMode::Stunned) { // collapse, held (pose 'collapse', 48)
            clip = &goonClips_[k][kGoonCollapse];
            if (clip->loaded()) sampleOnce(*clip, t, &a, &b, &blend);
        } else if (g.mode == GoonMode::Recovery) {
            clip = &goonClips_[k][kGoonRecovery];
            if (clip->loaded()) sampleOnce(*clip, t, &a, &b, &blend);
        } else if (clip->loaded()) {
            // setPlayRate(velocity / (ANIM_WALK_RATE * scale), 'walk'); paused while attacking.
            const float rate = spec_.number(*e, "velocity", 4.0f) / (kGoonAnimWalkRate * scale);
            render::sampleClip(*clip, goonTrackTime(g, doorNowMs_) * rate, &a, &b, &blend);
        }
        if (clip->loaded()) renderer.drawMorph(model, *clip, a, b, blend, m);
        else renderer.drawModel(model, m);
        // The radar beam (Goon.scaleRadar), hidden while stunned; it flashes while attacking.
        if (goonCone_.loaded() && g.mode != GoonMode::Stunned && g.mode != GoonMode::Recovery) {
            const float radius = spec_.number(*e, "attackRadius", 15.0f);
            const float halfFov = spec_.number(*e, "hFov", 70.0f) * 0.5f;
            const float kw = std::tan(halfFov * kDegToRad) * radius / 10.5f;
            const float kl = std::sqrt(radius * radius + 9.0f) / 25.0f;
            C3D_Mtx r = m;
            Mtx_Translate(&r, goonRadar_[0], goonRadar_[1], goonRadar_[2], true);
            Mtx_RotateZ(&r, 3.14159265f, true); // the eye's frame faces the goon's front (actorGeom H 180)
            // beam.setHpr(0, halfFov, 0) and setScale(kw, kl, kw) / scale; radar.setSz(-3.5 / p.z).
            const float pz = -6.0f * std::sin(halfFov * kDegToRad) * kl / scale - 1.8f * std::cos(halfFov * kDegToRad) * kw / scale;
            Mtx_Scale(&r, 1, 1, pz < -1e-3f ? -3.5f / pz : 1);
            Mtx_RotateX(&r, halfFov * kDegToRad, true);
            Mtx_Scale(&r, kw / scale, kl / scale, kw / scale);
            // makeAttackTrack: the eye flashes red / blue every 0.2 s for 0.8 s (on the beam here).
            const bool attacking = g.mode == GoonMode::Battle && t < 0.8f;
            const bool red = (int(t / 0.2f) & 1) == 0;
            if (attacking) renderer.setBaseTint(red ? 1.0f : 0.2f, 0.2f, red ? 0.2f : 1.0f, 0.2f);
            else renderer.setBaseTint(1, 1, 1, 0.2f);
            renderer.drawModel(const_cast<render::Model&>(goonCone_), r);
            renderer.setBaseTint(1, 1, 1, 1);
        }
    }
}

bool Level::nearestEntity(uint16_t classId, float x, float y, float z, float* ex, float* ey, float* ez) const {
    if (!active_ || !doorClient_) return false;
    float best = 1e30f;
    for (int i = 0; i < net::kMaxLevelEntities; ++i) {
        const net::LevelEntityObj& d = doorClient_->levelEntitySlot(i);
        if (!d.active || d.classId != classId) continue;
        const LevelEntity* e = specOf(d.levelDoId, d.entId); // any room (autotest: go where it is)
        if (!e) continue;
        const float dx = e->world[9] - x, dy = e->world[10] - y, dz = e->world[11] - z;
        const float dist = dx * dx + dy * dy + dz * dz;
        if (dist < best) best = dist, *ex = e->world[9], *ey = e->world[10], *ez = e->world[11];
    }
    return best < 1e30f;
}

bool Level::nearestDoor(float x, float y, float z, float* dx, float* dy, float* dz, float* dh) const {
    if (!active_ || !doorClient_) return false;
    float best = 1e30f;
    for (int i = 0; i < net::kMaxLevelEntities; ++i) {
        const net::LevelEntityObj& d = doorClient_->levelEntitySlot(i);
        if (!d.active || d.classId != dc::DistributedDoorEntity::kClassId) continue;
        if (d.state != 4 && d.state != 0) continue;
        const LevelEntity* e = specOf(d.levelDoId, d.entId);
        if (!e) continue;
        const float ex = e->world[9], ey = e->world[10], ez = e->world[11];
        const float dist = (ex - x) * (ex - x) + (ey - y) * (ey - y) + (ez - z) * (ez - z);
        if (dist >= best) continue;
        best = dist;
        // Stand on our side of the door, facing it.
        float l[3];
        toLocal(e->world, x, y, z, l);
        const float side = l[1] < 0 ? -1.0f : 1.0f;
        const float* ay = e->world + 3;
        const float len = std::sqrt(ay[0] * ay[0] + ay[1] * ay[1]);
        const float nx = len > 1e-6f ? ay[0] / len : 0.0f, ny = len > 1e-6f ? ay[1] / len : 1.0f;
        *dx = ex + nx * side * 3.0f, *dy = ey + ny * side * 3.0f, *dz = ez;
        *dh = headingTo(*dx, *dy, ex, ey);
    }
    return best < 1e30f;
}

void Level::drawDoors(render::Renderer& renderer) const {
    if (!doorClient_) return;
    for (int i = 0; i < net::kMaxLevelEntities; ++i) {
        const net::LevelEntityObj& d = doorClient_->levelEntitySlot(i);
        if (!d.active || d.classId != dc::DistributedDoorEntity::kClassId) continue;
        const LevelEntity* e = specOf(d.levelDoId, d.entId);
        if (!e || !shown(e->room)) continue;
        const DoorPose p = doorPose(d, doorNowMs_);
        C3D_Mtx root, m;
        toMtx(e->world, &root);
        float color[4] = {1, 1, 1, 1};
        spec_.getFloats(*e, "color", color, 4);
        renderer.setBaseTint(color[0], color[1], color[2], color[3]);
        auto part = [&](int index, float ox, float oy, float oz) {
            render::Model& model = const_cast<render::Model&>(doorModels_[index]);
            if (!model.loaded()) return;
            m = root;
            Mtx_Translate(&m, ox, oy, oz, true);
            renderer.drawModel(model, m);
        };
        part(kDoorFrame, 0, 0, 0);
        if (p.outer) {
            part(kDoorTop, 0, 0, p.top);
            part(kDoorBottom, 0, 0, p.bottom);
        }
        if (p.inner) {
            part(kDoorLeft, p.left, 0, 0);
            part(kDoorRight, p.right, 0, 0);
        }
        for (int k = 0; k < 3; ++k) {
            const int base = kDoorLock0 + k * 3;
            if (p.lockClosed[k]) part(base, 0, 0, 0);
            if (p.lockOpen[k]) {
                part(base + 1, -p.lock[k], 0, 0);
                part(base + 2, p.lock[k], 0, 0);
            }
        }
        renderer.setBaseTint(1, 1, 1, 1);
    }
}

namespace {

// bake_facility_floors.py TLF1 (one facility floor).
using FloorHeader = floorfmt::Header;
using FloorPiece = floorfmt::Piece;
using FloorDoor = floorfmt::Door;
using FloorTriRecord = floorfmt::FloorTri;

constexpr uint32_t kUberZone = 2; // OTPGlobals.UberZone (DistributedMint.gotAllRooms' zoneList)

const char* floorKindName(net::FacilityKind k) {
    return k == net::FacilityKind::Mint ? "mint" : k == net::FacilityKind::Stage ? "stage" : "club";
}

} // namespace

bool Level::loadRoom(int room, const char* name, const float placement[12]) {
    char path[112];
    snprintf(path, sizeof(path), "%s%s.tlv", kDir, name);
    if (spec_.append(path, placement) != room) {
        TT_LOG("level: missing or bad %s", path);
        return false;
    }
    Room& r = rooms_[room];
    if (placement) memcpy(r.placement, placement, sizeof(r.placement));
    snprintf(path, sizeof(path), "%s%s.tz", kDir, name);
    if (!r.zone.load(path)) {
        TT_LOG("level: %s: %s", path, r.zone.error());
        return false;
    }
    r.numZones = 0;
    for (int i = spec_.next("zone", -1); i >= 0 && r.numZones < 128; i = spec_.next("zone", i))
        if (spec_.entity(i).room == room) r.zoneNums[r.numZones++] = uint16_t(spec_.entity(i).entId);
    std::sort(r.zoneNums, r.zoneNums + r.numZones); // LevelMgr.privAssignZoneIds: sorted zone nums
    TT_LOG("level: room %d %s: %d zones, %d chunks", room, name, r.numZones, r.zone.numChunks());
    return true;
}

// DistributedMint/Stage/CountryClub.gotAllRooms: the rooms placed along the floor layout with
// their hallways and door frames (all baked into the floor's .tlf).
bool Level::loadFloor(const net::Facility& fac, const LoadedBlob& source) {
    const u64 loadStart = svcGetSystemTick();
    char file[112], path[112];
    snprintf(file, sizeof(file), "%sfloors/%s_%u_%u_%u.tlf", kDir, floorKindName(fac.kind), unsigned(fac.facilityId),
             unsigned(fac.kind == net::FacilityKind::Mint ? 0 : fac.layoutIndex), unsigned(fac.floorNum));
    const auto* bytes = static_cast<const uint8_t*>(source.data);
    auto read = [&](uint32_t offset, void* out, size_t size) {
        if (!bytes || uint64_t(offset) + size > source.size) return false;
        memcpy(out, bytes + offset, size);
        return true;
    };
    FloorHeader h{};
    read(0, &h, sizeof(h));
    const floorfmt::Expected expected{uint16_t(fac.kind), fac.facilityId,
                                     uint16_t(fac.kind == net::FacilityKind::Mint ? 0 : fac.layoutIndex),
                                     fac.floorNum, fac.numRooms};
    const char* validationError = floorfmt::validate(source.data, source.size, expected);
    bool ok = validationError == nullptr;
    if (!ok) TT_LOG("level: %s: %s", file, validationError);
    numPieces_ = 0;
    // These tables are small and bounded (31 pieces / 32 door frames today). Reading each
    // record with readAt used to turn them into dozens of seeks and SD transactions during
    // the elevator fade. Keep the raw records on the stack and preserve the processing order.
    FloorPiece floorPieces[kMaxPieces];
    if (ok && h.numPieces)
        ok = read(h.piecesOffset, floorPieces, h.numPieces * sizeof(FloorPiece));
    for (int i = 0; ok && i < h.numPieces; ++i) {
        FloorPiece& fp = floorPieces[i];
        fp.name[47] = 0;
        Piece& p = pieces_[numPieces_++];
        memcpy(p.placement, fp.mat, sizeof(p.placement));
        memcpy(p.lo, fp.lo, 12), memcpy(p.hi, fp.hi, 12);
        p.room = p.model = -1;
        p.roomId = fp.roomId;
        if (!fp.isHallway) {
            const int room = i / 2;
            p.room = room;
            rooms_[room].piece = i;
            rooms_[room].doId = fac.roomDoIds[room];
            numRooms_ = room + 1;
            ok = loadRoom(room, fp.name, fp.mat);
            continue;
        }
        for (int k = 0; k < numConnectors_ && p.model < 0; ++k)
            if (!strcmp(connectorNames_[k], fp.name)) p.model = k;
        if (p.model < 0 && numConnectors_ < 4) {
            p.model = numConnectors_++;
            snprintf(connectorNames_[p.model], sizeof(connectorNames_[p.model]), "%s", fp.name);
            snprintf(path, sizeof(path), "%sprops/%s.ttm", kDir, fp.name);
            if (!assetBatch_.load(connectorModels_[p.model], path))
                TT_LOG("level: missing %s", path);
        }
    }
    numDoorFrames_ = 0;
    FloorDoor floorDoors[kMaxDoorFrames];
    if (ok && h.numDoorFrames)
        ok = read(h.doorFramesOffset, floorDoors, h.numDoorFrames * sizeof(FloorDoor));
    for (int i = 0; ok && i < h.numDoorFrames && numDoorFrames_ < kMaxDoorFrames; ++i) {
        const FloorDoor& d = floorDoors[i];
        DoorFrame& df = doorFrames_[numDoorFrames_++];
        memcpy(df.placement, d.mat, sizeof(df.placement));
        df.piece = d.roomNum;
    }
    if (ok && h.numFloorTris) {
        floorTris_ = static_cast<FloorTri*>(malloc(h.numFloorTris * sizeof(FloorTri)));
        ok = floorTris_ != nullptr;
        // In blocks: one seek + read per record (up to ~850 a floor) cost an SD round trip each.
        FloorTriRecord block[64];
        for (uint32_t i = 0; ok && i < h.numFloorTris;) {
            const uint32_t n = std::min<uint32_t>(64, h.numFloorTris - i);
            ok = read(h.floorTrisOffset + i * sizeof(FloorTriRecord), block, n * sizeof(FloorTriRecord));
            for (uint32_t k = 0; ok && k < n; ++k, ++i) {
                floorTris_[i].piece = block[k].roomNum;
                memcpy(floorTris_[i].v, block[k].v, sizeof(block[k].v));
                numFloorTris_ = int(i + 1);
            }
        }
    }
    if (ok)
        ok = floorCollision_.loadMemory(bytes + h.collisionOffset, h.collisionSize, file);
    if (!ok) return false;
    const unsigned long loadMs = static_cast<unsigned long>((svcGetSystemTick() - loadStart) /
                                                            (SYSCLOCK_ARM11 / 1000));
    TT_LOG("level: floor %s: %d rooms, %d pieces, %d door frames, %d floor tris, %lu ms", file, numRooms_,
           numPieces_, numDoorFrames_, numFloorTris_, loadMs);
    snprintf(path, sizeof(path), "%sprops/door_frame_0.ttm", kDir);
    if (numDoorFrames_ && !doorFrameModel_.loaded())
        assetBatch_.load(doorFrameModel_, path);
    return true;
}

void Level::requestFloor(const net::Facility& fac) {
    if (!fac.numRooms) return;
    char file[112];
    snprintf(file, sizeof(file), "%sfloors/%s_%u_%u_%u.tlf", kDir, floorKindName(fac.kind),
             unsigned(fac.facilityId), unsigned(fac.kind == net::FacilityKind::Mint ? 0 : fac.layoutIndex),
             unsigned(fac.floorNum));
    if (!strcmp(floorLoadPath_, file)) return;
    if (floorLoadTicket_) AssetLoader::cancel(floorLoadTicket_);
    floorLoadTicket_ = kNoTicket;
    floorLoadPath_[0] = 0;
    // Current files are far below this. Keep corrupt/network-selected metadata from
    // consuming an unbounded share of the Old 3DS regular heap.
    floorLoadTicket_ = AssetLoader::requestBlob(file, 2 * 1024 * 1024);
    if (floorLoadTicket_) {
        snprintf(floorLoadPath_, sizeof(floorLoadPath_), "%s", file);
    }
}

void Level::loadSpinModels() {
    // PropSpinner models of every level (bake_level.py spin_props: <level>_spin_<i>.ttm).
    char path[112];
    numSpins_ = 0;
    for (int i = spec_.next("spinProp", -1); i >= 0 && numSpins_ < kMaxSpins; i = spec_.next("spinProp", i)) {
        const LevelEntity& e = spec_.entity(i);
        const char* file = spec_.string(e, "file");
        if (!file) continue;
        snprintf(path, sizeof(path), "%s%s", kDir, file);
        if (!assetBatch_.load(spinModels_[numSpins_], path))
            continue;
        spinRoom_[numSpins_] = e.room;
        spinEnt_[numSpins_] = uint16_t(e.entId);
        numSpins_++;
    }
}

void Level::activate(net::Client& client) {
    entryLoading_ = true;
    active_ = true;
    spawned_ = false;
    won_ = false;
    zoneNum_ = -1;
    lockZoneId_ = sentLock_ = 0;
    interestSent_ = false;
    boardedElevator_ = 0;
    seatPending_ = false;
    piece_ = 0;
    showMin_ = 0, showMax_ = 1; // camEnterRoom(0)
    for (Cog& c : cogs_) c = Cog{};
    loadProps();
    loadSpinModels();
    client.setLevelBattlePlacer(&Level::battleCellThunk, this);
}

void Level::begin(Room& room, const net::LevelObj& l) {
    room.levelZoneId = l.levelZoneId;
    for (int i = 0; i < room.numZones; ++i) room.zoneIds[i] = i < l.numZones ? l.zoneIds[i] : 0;
    if (l.numZones && l.numZones != room.numZones)
        TT_LOG("level: server sent %d zone ids for %d spec zones", int(l.numZones), room.numZones);
    room.startMs = l.startMs;
    if (&room != &rooms_[0]) return;
    entranceId_ = l.entranceId;
    toonIndex_ = 0;
    numToons_ = l.numPlayers > 0 ? l.numPlayers : 1;
    for (int i = 0; i < l.numPlayers; ++i)
        if (client_ && l.playerIds[i] == client_->avatarId()) toonIndex_ = i;
}

// gotAllRooms: once the floor object and every one of its rooms are here.
void Level::tryBeginFloor(net::Client& client) {
    const net::Facility* fac = client.facility();
    if (active_ || !fac || fac->numRooms == 0) return;
    requestFloor(*fac);
    if (!floorLoadTicket_) return;
    const LoadStatus floorStatus = AssetLoader::poll(floorLoadTicket_);
    if (floorStatus == LoadStatus::Pending) return;
    if (floorStatus != LoadStatus::Ready) {
        TT_LOG("level: floor container load failed");
        AssetLoader::cancel(floorLoadTicket_);
        floorLoadTicket_ = kNoTicket;
        return;
    }
    for (int i = 0; i < fac->numRooms; ++i)
        if (!client.levelObj(fac->roomDoIds[i])) return;
    LoadedBlob blob;
    if (!AssetLoader::takeBlob(floorLoadTicket_, blob)) return;
    floorLoadTicket_ = kNoTicket;
    const bool floorOk = loadFloor(*fac, blob);
    free(blob.data);
    if (!floorOk) {
        shutdown();
        return;
    }
    for (int i = 0; i < numRooms_; ++i) {
        const net::LevelObj* l = client.levelObj(rooms_[i].doId);
        if (l->levelId != pieces_[rooms_[i].piece].roomId) // the layout must match the AI's (MintLayout)
            TT_LOG("level: room %d is %u on the server, %d in the layout", i, unsigned(l->levelId),
                   int(pieces_[rooms_[i].piece].roomId));
        begin(rooms_[i], *l);
    }
    floor_ = true;
    facilityDoId_ = fac->doId;
    facilityZoneId_ = fac->zoneId;
    winHood_ = fac->facilityId;
    activate(client);
    // Every room's zones are in view at once (no per-room visibility; DistributedMintRoom).
    for (int r = 0; r < numRooms_; ++r) {
        uint16_t zones[128];
        for (int k = 0; k < rooms_[r].numZones; ++k) zones[k] = uint16_t(kZoneBase + rooms_[r].zoneNums[k]);
        rooms_[r].zone.setExtraVisible(zones, rooms_[r].numZones);
    }
    TT_LOG("level: facility %u floor %u: %d rooms", unsigned(fac->facilityId), unsigned(fac->floorNum), numRooms_);
}

uint32_t Level::zoneIdOf(int zoneNum) const {
    const Room& r = rooms_[0];
    for (int i = 0; i < r.numZones; ++i)
        if (r.zoneNums[i] == zoneNum) return r.zoneIds[i];
    return 0;
}

void Level::handleEvent(const net::Event& ev, net::Client& client) {
    using net::EventType;
    client_ = &client;
    if (ev.type == EventType::LevelUp || ev.type == EventType::LevelState) {
        const net::LevelObj* l = client.levelObj(ev.doId);
        if (!l) return;
        if (!l->isRoom()) { // DistributedFactory: one level
            if (!active_) {
                const char* name = levelName(l->levelId);
                if (!name) return;
                rooms_[0].doId = l->doId;
                numRooms_ = 1;
                if (!loadRoom(0, name, nullptr)) {
                    shutdown();
                    return;
                }
                floor_ = false;
                winHood_ = l->levelId;
                activate(client);
            }
        } else if (!active_) {
            tryBeginFloor(client);
        }
        const int room = roomOf(l->doId);
        if (!active_ || room < 0) return;
        begin(rooms_[room], *l);
        if (l->defeated && !won_) {
            won_ = true;
            winDestination_ = winHood_ - winHood_ % 1000; // ZoneUtil.getHoodId
            TT_LOG("level: %u defeated -> zone %u", unsigned(winHood_), unsigned(winDestination_));
        }
    } else if (ev.type == EventType::FacilityUp) {
        if (const net::Facility* fac = client.facility()) requestFloor(*fac);
        tryBeginFloor(client);
    } else if (ev.type == EventType::FacilityNextFloor) {
        // setStageZone / setCountryClubZone: the elevator left with us; on to the next floor.
        nextFloorZone_ = uint32_t(ev.arg);
        TT_LOG("level: next floor in zone %u", unsigned(nextFloorZone_));
    } else if (ev.type == EventType::LevelGone) {
        if (ownsLevel(ev.doId)) shutdown();
    } else if (ev.type == EventType::FacilityGone) {
        if (floor_ && ev.doId == facilityDoId_) shutdown();
    }
}

void Level::shutdown() {
    entryLoading_ = false;
    if (floorLoadTicket_) AssetLoader::cancel(floorLoadTicket_);
    floorLoadTicket_ = kNoTicket;
    floorLoadPath_[0] = 0;
    assetBatch_.clear();
    if (client_) client_->setLevelBattlePlacer(nullptr, nullptr);
    doorClient_ = nullptr;
    for (Room& r : rooms_) {
        r.zone.unload();
        const float identity[12] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
        memcpy(r.placement, identity, sizeof(identity));
        r.doId = r.levelZoneId = r.startMs = 0;
        r.numZones = r.piece = 0;
    }
    numRooms_ = 0;
    spec_.unload();
    for (int k = 0; k < numSpins_; ++k) spinModels_[k].unload();
    numSpins_ = 0;
    for (int k = 0; k < numConnectors_; ++k) connectorModels_[k].unload();
    numConnectors_ = 0;
    numPieces_ = numDoorFrames_ = 0;
    floorCollision_.unload();
    free(floorTris_);
    floorTris_ = nullptr;
    numFloorTris_ = 0;
    floor_ = false;
    facilityDoId_ = facilityZoneId_ = 0;
    active_ = spawned_ = false;
    for (Cog& c : cogs_) c = Cog{};
    // ---- level games (world/level_games.*) ----
    games_.shutdown();
    // ---- end level games ----
}

bool Level::spawnPose(float* x, float* y, float* z, float* h) {
    if (!active_ || spawned_) return false;
    spawned_ = true;
    *x = *y = *z = *h = 0.0f; // no entrance point: render's origin (DistributedStage.placeToon)
    bool placed = false;
    for (int i = spec_.next("entrancePoint", -1); i >= 0 && !placed; i = spec_.next("entrancePoint", i)) {
        const LevelEntity& e = spec_.entity(i);
        if (e.room != 0 || spec_.integer(e, "entranceId", -1) != entranceId_) continue;
        // EntrancePoint.placeToon: placer at Y = -radius under a rotator turned by
        // -theta * (numToons - 1) / 2 + toonIndex * theta.
        const float radius = spec_.number(e, "radius", 15.0f), theta = spec_.number(e, "theta", 20.0f);
        const float rot = -theta * (numToons_ - 1) * 0.5f + toonIndex_ * theta;
        const float r = rot * kDegToRad;
        const float local[3] = {radius * std::sin(r), -radius * std::cos(r), 0.0f};
        float w[3];
        LevelSpec::toWorld(e, local, w);
        *x = w[0], *y = w[1], *z = w[2];
        *h = LevelSpec::worldHeading(e) + rot;
        placed = true;
    }
    if (!placed) TT_LOG("level: no entrance point %d", int(entranceId_));
    if (floor_) {
        updatePiece(*x, *y, *z);
        for (int i = 0; i < numRooms_; ++i) {
            float l[3];
            toLocal(rooms_[i].placement, *x, *y, *z, l);
            rooms_[i].zone.update(Vec3{l[0], l[1], l[2]});
        }
    } else {
        rooms_[0].zone.update(Vec3{*x, *y, *z}, &Level::cellChanged, this);
    }
    TT_LOG("level: entrance %d at %.1f,%.1f,%.1f h %.0f", int(entranceId_), double(*x), double(*y), double(*z),
           double(*h));
    return true;
}

void Level::cellChanged(void* self, uint16_t cellZone, const uint16_t* zones, int count) {
    Level& l = *static_cast<Level*>(self);
    if (cellZone >= kZoneBase) l.zoneNum_ = cellZone - kZoneBase;
    l.numPendingVis_ = 0;
    for (int i = 0; i < count && l.numPendingVis_ < 64; ++i)
        if (zones[i] >= kZoneBase) l.pendingVis_[l.numPendingVis_++] = l.zoneIdOf(zones[i] - kZoneBase);
    l.visDirty_ = true;
}

void Level::sendVisibility(net::Client& client, uint16_t, const uint16_t*, int) {
    // DistributedLevel.setVisibility: [levelZone, uberZone] + the visible zones.
    uint32_t ids[66];
    int n = 0;
    if (const uint32_t uber = zoneIdOf(0)) ids[n++] = uber;
    for (int i = 0; i < numPendingVis_ && n < 64; ++i)
        if (pendingVis_[i]) ids[n++] = pendingVis_[i];
    if (lockZoneId_) ids[n++] = lockZoneId_; // a battle keeps its zone in view (lockLevelViz)
    if (client.setLevelVisibility(rooms_[0].levelZoneId, ids, n)) {
        visDirty_ = false;
        sentLock_ = lockZoneId_;
    }
}

// DistributedMint.gotAllRooms: sendSetZoneMsg(zoneId, [UberZone, zoneId] + every room's zones).
void Level::sendFloorInterest(net::Client& client) {
    uint32_t ids[64];
    int n = 0;
    ids[n++] = kUberZone;
    for (int r = 0; r < numRooms_; ++r) {
        if (!rooms_[r].numZones || !rooms_[r].zoneIds[0]) return; // setZoneIds not here yet
        for (int k = 0; k < rooms_[r].numZones && n < 64; ++k) ids[n++] = rooms_[r].zoneIds[k];
    }
    interestSent_ = client.setLevelVisibility(facilityZoneId_, ids, n);
}

// toonEnterRoom / camEnterRoom: the piece whose floor (mintFloorColl<roomNum>) the toon stands on;
// its neighbours stay shown (a hallway shows the two rooms either side of it, a room one piece).
// Hallway floors reach a little into the rooms (about 0.35 above them at the doorways), so the
// floor nearest the feet decides, like the floor collision the toon actually touches.
void Level::updatePiece(float x, float y, float z) {
    int best = -1;
    float bestDz = 1e30f;
    for (int i = 0; i < numFloorTris_; ++i) {
        const float* v = floorTris_[i].v;
        auto side = [&](const float* a, const float* b) { return (b[0] - a[0]) * (y - a[1]) - (b[1] - a[1]) * (x - a[0]); };
        const float d0 = side(v, v + 3), d1 = side(v + 3, v + 6), d2 = side(v + 6, v);
        if (!((d0 >= 0 && d1 >= 0 && d2 >= 0) || (d0 <= 0 && d1 <= 0 && d2 <= 0))) continue;
        const float e1[3] = {v[3] - v[0], v[4] - v[1], v[5] - v[2]};
        const float e2[3] = {v[6] - v[0], v[7] - v[1], v[8] - v[2]};
        const float n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
        if (std::fabs(n[2]) < 1e-6f) continue;
        const float fz = v[2] - (n[0] * (x - v[0]) + n[1] * (y - v[1])) / n[2];
        if (fz > z + kFloorReach || std::fabs(z - fz) >= bestDz) continue;
        bestDz = std::fabs(z - fz);
        best = floorTris_[i].piece;
    }
    if (best >= 0 && best != piece_) {
        piece_ = best;
        TT_LOG("level: in piece %d (%s)", piece_, pieces_[piece_].room >= 0 ? "room" : "hallway");
    }
    showMin_ = piece_ % 2 ? piece_ - 2 : piece_ - 1;
    showMax_ = piece_ % 2 ? piece_ + 2 : piece_ + 1;
}

bool Level::pieceCenter(int piece, float* x, float* y, float* z) const {
    float sum[3] = {0, 0, 0};
    int n = 0;
    for (int i = 0; i < numFloorTris_; ++i) {
        if (floorTris_[i].piece != piece) continue;
        for (int c = 0; c < 3; ++c) sum[c] += (floorTris_[i].v[c] + floorTris_[i].v[3 + c] + floorTris_[i].v[6 + c]) / 3;
        n++;
    }
    if (!n) return false;
    // The centroid of the triangle nearest the mean (on the floor even for an L-shaped hallway)
    // that no other piece's floor covers (hallway floors reach into the rooms).
    const float mean[3] = {sum[0] / n, sum[1] / n, sum[2] / n};
    auto covered = [&](const float c[3]) {
        for (int i = 0; i < numFloorTris_; ++i) {
            if (floorTris_[i].piece == piece) continue;
            const float* v = floorTris_[i].v;
            auto side = [&](const float* a, const float* b) {
                return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]);
            };
            const float d0 = side(v, v + 3), d1 = side(v + 3, v + 6), d2 = side(v + 6, v);
            const bool inside = (d0 >= 0 && d1 >= 0 && d2 >= 0) || (d0 <= 0 && d1 <= 0 && d2 <= 0);
            if (inside && std::fabs((v[2] + v[5] + v[8]) / 3 - c[2]) < 2.0f) return true;
        }
        return false;
    };
    float best = 1e30f;
    for (int i = 0; i < numFloorTris_; ++i) {
        if (floorTris_[i].piece != piece) continue;
        const float* v = floorTris_[i].v;
        const float c[3] = {(v[0] + v[3] + v[6]) / 3, (v[1] + v[4] + v[7]) / 3, (v[2] + v[5] + v[8]) / 3};
        const float d = (c[0] - mean[0]) * (c[0] - mean[0]) + (c[1] - mean[1]) * (c[1] - mean[1]);
        if (d < best && !covered(c)) best = d, *x = c[0], *y = c[1], *z = c[2];
    }
    if (best == 1e30f) *x = mean[0], *y = mean[1], *z = mean[2];
    return true;
}

bool Level::battleCell(uint32_t levelDoId, uint32_t cellId, float* x, float* y, float* z, float* h) const {
    const int room = active_ ? roomOf(levelDoId) : -1;
    if (room < 0) return false;
    const LevelEntity* cell = spec_.findSynthetic(room, "battleCell", cellId);
    if (!cell) return false;
    *x = cell->world[9], *y = cell->world[10], *z = cell->world[11];
    *h = LevelSpec::worldHeading(*cell);
    return true;
}

Level::Cog* Level::cog(uint32_t doId) {
    for (Cog& c : cogs_)
        if (c.used && c.doId == doId) return &c;
    return nullptr;
}

void Level::setupCog(Cog& c, const net::LevelSuit& s) {
    c = Cog{};
    c.used = true;
    c.doId = s.doId;
    c.room = roomOf(s.levelDoId);
    c.spec = spec_.findSynthetic(c.room, s.reserve ? "reserveCogSpec" : "cogSpec", s.cogId);
    c.chaseSerial = s.chaseSerial;
    c.returnSerial = s.returnSerial;
    if (!c.spec) {
        TT_LOG("level: cog %u has no spec %u", unsigned(s.doId), unsigned(s.cogId));
        return;
    }
    c.ox = c.x = c.spec->world[9], c.oy = c.y = c.spec->world[10], c.oz = c.z = c.spec->world[11];
    c.oh = c.h = LevelSpec::worldHeading(*c.spec);
    const char* behavior = spec_.string(*c.spec, "behavior");
    c.chase = behavior && !strcmp(behavior, "chase");
    float pts[48];
    const int n = spec_.getFloats(*c.spec, "pathWorld", pts, 48) / 3;
    c.numPath = std::min(n, 16);
    for (int i = 0; i < c.numPath; ++i)
        for (int k = 0; k < 3; ++k) c.path[i][k] = pts[i * 3 + k];
    c.pathLength = 0;
    for (int i = 0; i < c.numPath; ++i) {
        const float* a = c.path[i];
        const float* b = c.path[(i + 1) % c.numPath];
        c.pathLength += kTurnTime + std::hypot(b[0] - a[0], b[1] - a[1], b[2] - a[2]) / kWalkSpeed;
    }
    if (c.numPath >= 2) {
        c.x = c.path[0][0], c.y = c.path[0][1], c.z = c.path[0][2];
    }
}

void Level::walkPath(Cog& c, float dt) {
    // PathEntity.makePathTrack: per leg, a 1 s LerpHpr to face the next point, then a LerpPos
    // at the walk speed; the track loops (enterWalk) from where it was paused.
    if (c.numPath < 2 || c.pathLength <= 0) {
        c.walking = false;
        return;
    }
    c.pathTime = std::fmod(c.pathTime + dt, c.pathLength);
    float t = c.pathTime;
    for (int i = 0; i < c.numPath; ++i) {
        const float* a = c.path[i];
        const float* b = c.path[(i + 1) % c.numPath];
        const float len = std::hypot(b[0] - a[0], b[1] - a[1], b[2] - a[2]);
        const float target = headingTo(a[0], a[1], b[0], b[1]);
        if (t < kTurnTime) {
            float from = i > 0 ? headingTo(c.path[i - 1][0], c.path[i - 1][1], a[0], a[1])
                               : headingTo(c.path[c.numPath - 1][0], c.path[c.numPath - 1][1], a[0], a[1]);
            float d = std::fmod(target - from + 540.0f, 360.0f) - 180.0f; // LerpHpr takes the short way
            c.x = a[0], c.y = a[1], c.z = a[2];
            c.h = from + d * (t / kTurnTime);
            c.walking = false;
            return;
        }
        t -= kTurnTime;
        const float dur = len / kWalkSpeed;
        if (t < dur || i == c.numPath - 1) {
            const float u = dur > 0 ? std::min(t / dur, 1.0f) : 1.0f;
            c.x = a[0] + (b[0] - a[0]) * u, c.y = a[1] + (b[1] - a[1]) * u, c.z = a[2] + (b[2] - a[2]) * u;
            c.h = target;
            c.walking = true;
            return;
        }
        t -= dur;
    }
}

void Level::moveToward(Cog& c, float speed, float dt) {
    const float dx = c.tx - c.x, dy = c.ty - c.y;
    const float dist = std::sqrt(dx * dx + dy * dy);
    c.walking = dist > 0.05f;
    if (!c.walking) return;
    c.h = headingTo(c.x, c.y, c.tx, c.ty);
    const float step = std::min(dist, speed * dt);
    c.x += dx / dist * step, c.y += dy / dist * step;
}

bool Level::parentLocal(const Cog& c, float* x, float* y, float* z, float* h) const {
    // getPos()/getHpr() of the cog relative to its parent entity (d_requestBattle).
    const LevelEntity* parent = c.spec ? spec_.find(c.spec->room, c.spec->parentEntId) : nullptr;
    if (!parent) {
        *x = c.x, *y = c.y, *z = c.z, *h = c.h;
        return false;
    }
    const float* m = parent->world;
    const float d[3] = {c.x - m[9], c.y - m[10], c.z - m[11]};
    float out[3];
    for (int r = 0; r < 3; ++r) {
        const float* axis = m + r * 3;
        const float len2 = axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2];
        out[r] = len2 > 1e-12f ? (d[0] * axis[0] + d[1] * axis[1] + d[2] * axis[2]) / len2 : 0.0f;
    }
    *x = out[0], *y = out[1], *z = out[2];
    *h = c.h - LevelSpec::worldHeading(*parent);
    return true;
}

void Level::requestBattle(Cog& c, uint32_t nowMs, LevelBattleAction* action) {
    float lx, ly, lz, lh;
    parentLocal(c, &lx, &ly, &lz, &lh);
    action->cogDoId = c.doId;
    action->x = lx, action->y = ly, action->z = lz, action->h = lh;
    c.mode = CogMode::WaitBattle;
    c.requestMs = nowMs;
    c.walking = false;
}

void Level::update(net::Client& client, SuitActors& suits, float x, float y, float z, float dt, uint32_t nowMs,
                   bool battleIdle, LevelBattleAction* action) {
    prof::lap(prof::kPlayer);
    client_ = &client;
    toonX_ = x, toonY_ = y, toonZ_ = z;
    *action = LevelBattleAction{};
    if (!active_) return;
    updateDoors(client, x, y, z, nowMs);
    updateButtons(client, x, y, z);
    updateStompers(client, x, y, z, nowMs);
    updateLifts(client, x, y, z, nowMs);
    updateBarrels(client, x, y, z, nowMs);
    updateMovers(x, y, z, nowMs);
    updateGoons(client, x, y, z, nowMs);
    updateElevators(client, x, y, z, nowMs);
    // ---- level games (world/level_games.*) ----
    games_.update(client, *this, x, y, z, dt, nowMs);
    prof::lap(prof::kFacilityUpdate);
    // ---- end level games ----
    if (floor_) {
        updatePiece(x, y, z);
        if (!interestSent_) sendFloorInterest(client);
        for (int r = 0; r < numRooms_; ++r) { // each room streams in its own space
            float l[3];
            toLocal(rooms_[r].placement, x, y, z, l);
            rooms_[r].zone.update(Vec3{l[0], l[1], l[2]});
        }
    } else {
        rooms_[0].zone.update(Vec3{x, y, z}, &Level::cellChanged, this, 200.0f); // rims of gears, lifts, mixers
        if (lockZoneId_ != sentLock_) visDirty_ = true;
        if (visDirty_ && numPendingVis_ > 0) sendVisibility(client, 0, nullptr, 0);
    }

    // Cogs of this level: walk, alert, chase, return; contact requests the battle.
    for (Cog& c : cogs_) {
        if (c.used && !client.findLevelSuit(c.doId)) {
            suits.removeExternal(c.doId);
            c = Cog{};
        }
    }
    bool requested = false;
    for (int i = 0; i < net::kMaxLevelSuits; ++i) {
        const net::LevelSuit& s = client.levelSuitSlot(i);
        if (!s.active || roomOf(s.levelDoId) < 0) continue;
        Cog* c = cog(s.doId);
        if (!c) {
            for (Cog& free : cogs_)
                if (!free.used && !c) c = &free;
            if (!c) continue;
            setupCog(*c, s);
        }
        if (!c->spec) continue;
        // A cog in a battle belongs to world/battle.* (it places, animates and hides it).
        bool fighting = false;
        for (int b = 0; b < net::kMaxBattles && !fighting; ++b) {
            const net::Battle& nb = client.battleSlot(b);
            for (int k = 0; nb.active && k < nb.numSuits; ++k) fighting = fighting || nb.suits[k] == c->doId;
        }
        if (fighting) {
            c->mode = CogMode::WaitBattle;
            c->requestMs = nowMs;
            c->inBattle = true;
            continue;
        }
        if (c->inBattle) { // the battle let it go (the toons ran or lost): back where it stands
            c->inBattle = false;
            float bx, by, bz, bh;
            if (suits.pose(c->doId, &bx, &by, &bz, &bh)) c->x = bx, c->y = by, c->z = bz, c->h = bh;
            c->mode = CogMode::Return;
        }
        if (s.chaseSerial != c->chaseSerial) { // setConfrontToon: enterChase
            c->chaseSerial = s.chaseSerial;
            if (s.confrontToon) {
                c->mode = CogMode::Chase;
                c->chaseClock = 0;
                c->chaseStep = 1.0f; // aim at once
            }
        }
        if (s.returnSerial != c->returnSerial) { // setReturn: enterReturn
            c->returnSerial = s.returnSerial;
            if (c->mode == CogMode::Chase) c->mode = CogMode::Return;
        }
        switch (c->mode) {
        case CogMode::Walk:
            walkPath(*c, dt);
            if (c->chase && !c->alerted) {
                const float dx = x - c->x, dy = y - c->y;
                if (dx * dx + dy * dy < kAlertRadius * kAlertRadius && std::fabs(z - c->z) < kAlertMaxDz) {
                    client.sendLevelSuitAlert(c->doId);
                    c->alerted = true;
                }
            }
            break;
        case CogMode::Chase: {
            c->chaseClock += dt;
            c->chaseStep += dt;
            if (c->chaseClock > kChaseTime) { // chaseTask: setReturn() locally after 3 s
                c->mode = CogMode::Return;
                break;
            }
            if (c->chaseStep >= 1.0f) { // re-aim at the toon's position once a second
                c->chaseStep = 0;
                float tx = x, ty = y, tz = z;
                if (s.confrontToon != client.avatarId()) client.avatarPos(s.confrontToon, &tx, &ty, &tz);
                c->tx = tx, c->ty = ty;
            }
            moveToward(*c, kChaseSpeed, dt);
            const float sx = c->x - c->ox, sy = c->y - c->oy;
            if (sx * sx + sy * sy > kStrayDistance * kStrayDistance && nowMs - c->lastStrayMs > 1000) {
                client.sendLevelSuitStrayed(c->doId);
                c->lastStrayMs = nowMs;
            }
            break;
        }
        case CogMode::Return: {
            // Back to where the path was left (no path: the spawn), then walk on.
            Cog resume = *c;
            walkPath(resume, 0.0f); // the paused path pose
            c->tx = c->numPath >= 2 ? resume.x : c->ox;
            c->ty = c->numPath >= 2 ? resume.y : c->oy;
            moveToward(*c, kReturnSpeed, dt);
            if (!c->walking) {
                c->mode = CogMode::Walk;
                c->alerted = false;
                if (c->numPath < 2) c->h = c->oh; // returnDone: setHpr(spec h)
            }
            break;
        }
        case CogMode::WaitBattle:
            c->walking = false;
            if (nowMs - c->requestMs > kDenyMs) c->mode = CogMode::Walk; // denied or no battle came
            break;
        }
        // __handleToonCollision: the toon touched the walking/chasing cog.
        if (battleIdle && !requested && !s.reserve && c->mode != CogMode::WaitBattle) {
            const float dx = x - c->x, dy = y - c->y;
            if (dx * dx + dy * dy < kTouchRadius * kTouchRadius && c->z - z < kBelowIgnore && z - c->z < 6.0f) {
                requestBattle(*c, nowMs, action);
                requested = true;
            }
        }
        // Drawn while its room is (the rooms camEnterRoom stashes hide their cogs too).
        if (c->room >= 0 && shown(c->room))
            suits.setExternal(c->doId, s.name, s.levelDist, c->x, c->y, c->z, c->h, c->walking, !s.reserve,
                              s.skelecog != 0);
        else
            suits.removeExternal(c->doId);
    }

    // BattleBlocker.startBattle: walking into a blocker joins its battle or fights its cogs.
    if (battleIdle && !requested && nowMs - blockerMs_ > 1000) {
        for (int i = 0; i < net::kMaxLevelBlockers; ++i) {
            const net::LevelBlocker& b = client.levelBlockerSlot(i);
            if (!b.active || !b.blockerActive || b.finished) continue;
            const LevelEntity* e = specOf(b.levelDoId, b.entId);
            if (!e) continue;
            const float r = spec_.number(*e, "radius", 10.0f);
            const float dx = x - e->world[9], dy = y - e->world[10], dz = z - e->world[11];
            if (dx * dx + dy * dy + dz * dz > r * r) continue;
            blockerMs_ = nowMs;
            if (b.battleId && client.findBattle(b.battleId)) {
                action->joinBattle = b.battleId;
            } else {
                for (int k = 0; k < b.numSuits; ++k) {
                    Cog* c = cog(b.suits[k]);
                    if (c && c->spec) {
                        requestBattle(*c, nowMs, action);
                        break;
                    }
                }
            }
            break;
        }
    }
}

bool Level::nearestCog(float x, float y, float z, float* cx, float* cy, float* cz) const {
    float best = 1e30f;
    for (const Cog& c : cogs_) {
        if (!c.used || !c.spec || c.mode == CogMode::WaitBattle) continue;
        const float d = (c.x - x) * (c.x - x) + (c.y - y) * (c.y - y) + (c.z - z) * (c.z - z);
        if (d < best) best = d, *cx = c.x, *cy = c.y, *cz = c.z;
    }
    return best < 1e30f;
    prof::lap(prof::kFacilityUpdate);
}

void Level::draw(render::Renderer& renderer, const C3D_FVec& eye) {
    if (!active_) return;
    prof::lap(prof::kDrawPlace);
    // The spec's farPlaneDistance is 1500; the zone vis lists (the factory) or the shown rooms
    // (a floor) already bound what is drawn.
    if (!floor_) {
        rooms_[0].zone.draw(renderer, Vec3{eye.x, eye.y, eye.z}, 400.0f);
    } else {
        for (int i = showMin_ < 0 ? 0 : showMin_; i <= showMax_ && i < numPieces_; ++i) {
            const Piece& p = pieces_[i];
            if (p.room >= 0) {
                float l[3];
                toLocal(p.placement, eye.x, eye.y, eye.z, l);
                rooms_[p.room].zone.draw(renderer, Vec3{l[0], l[1], l[2]}, 400.0f, p.placement);
            } else if (p.model >= 0 && connectorModels_[p.model].loaded()) {
                C3D_Mtx m;
                toMtx(p.placement, &m);
                renderer.drawModel(connectorModels_[p.model], m);
            }
        }
        for (int i = 0; i < numDoorFrames_ && doorFrameModel_.loaded(); ++i) {
            if (doorFrames_[i].piece < showMin_ || doorFrames_[i].piece > showMax_) continue;
            C3D_Mtx m;
            toMtx(doorFrames_[i].placement, &m);
            renderer.drawModel(doorFrameModel_, m);
        }
    }
    prof::lap(prof::kFacilityRooms);
    drawDoors(renderer);
    drawButtons(renderer);
    drawStompers(renderer);
    drawLifts(renderer);
    drawBarrels(renderer);
    drawMovers(renderer);
    drawGoons(renderer);
    drawElevators(renderer);
    // ---- level games (world/level_games.*) ----
    prof::lap(prof::kFacilityProps);
    games_.draw(renderer, eye);
    prof::lap(prof::kLevelGames);
    // ---- end level games ----
}

// ---- level games (world/level_games.*) ----
bool Level::nearestEntityTransform(uint16_t classId, float x, float y, float z, float outWorld[12]) const {
    if (!active_ || !doorClient_) return false;
    float best = 1e30f;
    for (int i = 0; i < net::kMaxLevelEntities; ++i) {
        const net::LevelEntityObj& d = doorClient_->levelEntitySlot(i);
        if (!d.active || d.classId != classId) continue;
        const LevelEntity* e = specOf(d.levelDoId, d.entId);
        if (!e) continue;
        const float dx = e->world[9] - x, dy = e->world[10] - y, dz = e->world[11] - z;
        const float dist = dx * dx + dy * dy + dz * dz;
        if (dist < best) {
            best = dist;
            memcpy(outWorld, e->world, sizeof(float) * 12);
        }
    }
    return best < 1e30f;
}
// ---- end level games ----

} // namespace tt::world
