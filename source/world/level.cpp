#include "world/level.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/log.h"
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
        if (!doorModels_[i].load(path)) TT_LOG("level: missing %s", path);
    }
    const char* stomperNames[2] = {"stomper", "stomper_lb"};
    for (int k = 0; k < 2; ++k) {
        StomperShape& st = stompers_[k];
        snprintf(path, sizeof(path), "%sprops/%s_head.ttm", kDir, stomperNames[k]);
        st.head.load(path);
        snprintf(path, sizeof(path), "%sprops/%s_shaft.ttm", kDir, stomperNames[k]);
        st.shaft.load(path);
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
        "CHQ_FACT_switch_depressed", "CHQ_FACT_switch_popup", "CHQ_FACT_elevator_up_down", "SZ_DD_treasure"};
    for (int k = 0; k < kSfxCount; ++k) {
        snprintf(path, sizeof(path), "sdmc:/3ds/toontown3ds/data/audio/%s.tsnd", kSfxNames[k]);
        sfx_[k] = audio::loadSfx(path);
    }
    const char* gearNames[2] = {"gear_factory", "gear_mint"};
    for (int k = 0; k < 2; ++k) {
        snprintf(path, sizeof(path), "%sprops/%s.ttm", kDir, gearNames[k]);
        gearModels_[k].load(path);
        snprintf(path, sizeof(path), "%sprops/%s.box", kDir, gearNames[k]);
        if (FILE* f = fopen(path, "r")) {
            float b[6];
            if (fscanf(f, "%f %f %f %f %f %f", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) == 6) memcpy(gearBox_[k], b, 24);
            fclose(f);
        }
    }
    snprintf(path, sizeof(path), "%sprops/paint_mixer_top.ttm", kDir);
    mixerTop_.load(path);
    snprintf(path, sizeof(path), "%sprops/paint_mixer_shaft.ttm", kDir);
    mixerShaft_.load(path);
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
    treadModel_.load(path);
    snprintf(path, sizeof(path), "%sprops/tread.box", kDir);
    if (FILE* f = fopen(path, "r")) {
        float b[6];
        if (fscanf(f, "%f %f %f %f %f %f", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) == 6) memcpy(treadBox_, b, 24);
        fclose(f);
    }
    snprintf(path, sizeof(path), "%sprops/barrel.ttm", kDir);
    barrelModel_.load(path);
    snprintf(path, sizeof(path), "%sprops/barrel_jar.ttm", kDir);
    barrelJar_.load(path);
    snprintf(path, sizeof(path), "%sprops/barrel_icecream.ttm", kDir);
    barrelIcecream_.load(path);
    for (int t = 0; t < 7; ++t)
        for (int l = 0; l < 7; ++l) {
            snprintf(path, sizeof(path), "%sprops/gag_icon_%d_%d.ttm", kDir, t, l);
            gagIcons_[t][l].load(path);
        }
    snprintf(path, sizeof(path), "%sprops/barrel.box", kDir);
    if (FILE* f = fopen(path, "r")) {
        float b[6];
        if (fscanf(f, "%f %f %f %f %f %f", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) == 6) memcpy(barrelBox_, b, sizeof(b));
        fclose(f);
    }
    snprintf(path, sizeof(path), "%sprops/lift.ttm", kDir);
    liftModel_.load(path);
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
    buttonBase_.load(path);
    snprintf(path, sizeof(path), "%sprops/button_top.ttm", kDir);
    buttonTop_.load(path);
    snprintf(path, sizeof(path), "%sprops/button.box", kDir);
    if (FILE* f = fopen(path, "r")) {
        float b[6];
        if (fscanf(f, "%f %f %f %f %f %f", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) == 6) memcpy(buttonBox_, b, sizeof(b));
        fclose(f);
    }
    snprintf(path, sizeof(path), "%sprops/door.box", kDir);
    if (FILE* f = fopen(path, "r")) {
        float b[6];
        if (fscanf(f, "%f %f %f %f %f %f", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) == 6) memcpy(doorBox_, b, sizeof(b));
        fclose(f);
    }
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
        if (!d.active || d.levelDoId != levelDoId_ || d.classId != dc::DistributedDoorEntity::kClassId) continue;
        const LevelEntity* e = spec_.find(d.entId);
        if (!e) continue;
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
        if (d.state != 4 && d.state != 0) {
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
    zone_.setExtraVisible(extra, numExtra);
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
        if (!d.active || d.levelDoId != levelDoId_ || d.classId != dc::DistributedButton::kClassId) continue;
        const LevelEntity* e = spec_.find(d.entId);
        if (!e) continue;
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
        if (!d.active || d.levelDoId != levelDoId_ || d.classId != dc::DistributedButton::kClassId) continue;
        const LevelEntity* e = spec_.find(d.entId);
        if (!e) continue;
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
    const float t = int32_t(nowMs - levelStartMs_) / 1000.0f + period * spec_.number(e, "phaseShift", 0.0f);
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
        if (!d.active || d.levelDoId != levelDoId_ || d.classId != dc::DistributedStomper::kClassId) continue;
        const LevelEntity* e = spec_.find(d.entId);
        if (!e) continue;
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
            const float t = int32_t(nowMs - levelStartMs_) / 1000.0f + period * spec_.number(*e, "phaseShift", 0.0f);
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
                        client.sendLevelOuch(levelDoId_, uint8_t(squishDamage_));
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
        if (!d.active || d.levelDoId != levelDoId_ || d.classId != dc::DistributedStomper::kClassId) continue;
        const LevelEntity* e = spec_.find(d.entId);
        if (!e) continue;
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
    static const float kIdentity[12] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
    for (int i = 0; i < net::kMaxLevelEntities; ++i) {
        const net::LevelEntityObj& d = client.levelEntitySlot(i);
        if (!d.active || d.levelDoId != levelDoId_ || d.classId != dc::DistributedLift::kClassId) {
            if (liftVoice_[i]) { audio::stopVoice(liftVoice_[i]); liftVoice_[i] = 0; }
            continue;
        }
        const LevelEntity* e = spec_.find(d.entId);
        if (!e) continue;
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
        // Guards (level collision moved here by bake_level.py): the far end's while resting,
        // both while moving.
        float box[6];
        for (int g = 0; g < 2; ++g) {
            const bool active = moving || (g == 0 ? d.state != 0 : d.state == 0);
            if (active && spec_.getFloats(*e, g == 0 ? "startGuardBox" : "endGuardBox", box, 6) == 6)
                addSolid(kIdentity, box, box + 3, true, false);
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
        if (!d.active || d.levelDoId != levelDoId_ || d.classId != dc::DistributedLift::kClassId) continue;
        const LevelEntity* e = spec_.find(d.entId);
        if (!e) continue;
        float w[12];
        bool moving;
        liftPose(*e, d, doorNowMs_, w, &moving);
        C3D_Mtx m;
        toMtx(w, &m);
        renderer.drawModel(const_cast<render::Model&>(liftModel_), m);
    }
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
        if (!d.active || d.levelDoId != levelDoId_ || !isBarrel(d.classId)) continue;
        const LevelEntity* e = spec_.find(d.entId);
        if (!e) continue;
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
        if (!d.active || d.levelDoId != levelDoId_ || !isBarrel(d.classId)) continue;
        const LevelEntity* e = spec_.find(d.entId);
        if (!e) continue;
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
            const uint64_t seed = uint64_t(e->entId) * levelDoId_;
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
    const float t = levelTime(nowMs);
    int slot = 0;
    for (int i = 0; i < spec_.numEntities(); ++i) {
        const LevelEntity& e = spec_.entity(i);
        if (e.flags & kLevelSynthetic) continue;
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
    const float t = levelTime(doorNowMs_);
    C3D_Mtx m;
    for (int i = 0; i < spec_.numEntities(); ++i) {
        const LevelEntity& e = spec_.entity(i);
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
        } else if (!strcmp(e.type, "spinProp") && e.entId < uint32_t(kMaxSpins) && spinModels_[e.entId].loaded()) {
            // PropSpinner: rate turns a minute; axis X spins P, Y spins H, Z spins R.
            const char* axis = spec_.string(e, "axis");
            const float a = spec_.number(e, "rate", 0.0f) * 360.0f * t / 60.0f * kDegToRad;
            toMtx(e.world, &m);
            if (axis && axis[0] == 'X') Mtx_RotateX(&m, a, true);
            else if (axis && axis[0] == 'Y') Mtx_RotateZ(&m, a, true);
            else Mtx_RotateY(&m, a, true);
            renderer.drawModel(const_cast<render::Model&>(spinModels_[e.entId]), m);
        }
    }
}

bool Level::nearestEntity(uint16_t classId, float x, float y, float z, float* ex, float* ey, float* ez) const {
    if (!active_ || !doorClient_) return false;
    float best = 1e30f;
    for (int i = 0; i < net::kMaxLevelEntities; ++i) {
        const net::LevelEntityObj& d = doorClient_->levelEntitySlot(i);
        if (!d.active || d.levelDoId != levelDoId_ || d.classId != classId) continue;
        const LevelEntity* e = spec_.find(d.entId);
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
        if (!d.active || d.levelDoId != levelDoId_ || d.classId != dc::DistributedDoorEntity::kClassId) continue;
        if (d.state != 4 && d.state != 0) continue;
        const LevelEntity* e = spec_.find(d.entId);
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
        if (!d.active || d.levelDoId != levelDoId_ || d.classId != dc::DistributedDoorEntity::kClassId) continue;
        const LevelEntity* e = spec_.find(d.entId);
        if (!e) continue;
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

bool Level::load(const char* name) {
    char path[96];
    snprintf(path, sizeof(path), "%s%s.tlv", kDir, name);
    if (!spec_.load(path)) {
        TT_LOG("level: missing or bad %s", path);
        return false;
    }
    snprintf(path, sizeof(path), "%s%s.tz", kDir, name);
    if (!zone_.load(path)) {
        TT_LOG("level: %s: %s", path, zone_.error());
        spec_.unload();
        return false;
    }
    numZones_ = 0;
    for (int i = spec_.next("zone", -1); i >= 0 && numZones_ < 128; i = spec_.next("zone", i))
        zoneNums_[numZones_++] = uint16_t(spec_.entity(i).entId);
    std::sort(zoneNums_, zoneNums_ + numZones_); // LevelMgr.privAssignZoneIds: sorted zone nums
    TT_LOG("level: %s: %d entities, %d zones, %d chunks", name, spec_.numEntities(), numZones_, zone_.numChunks());
    loadProps();
    // PropSpinner models of this level (bake_level.py spin_props).
    for (int k = 0; k < kMaxSpins; ++k) spinModels_[k].unload();
    for (int i = spec_.next("spinProp", -1); i >= 0; i = spec_.next("spinProp", i)) {
        const LevelEntity& e = spec_.entity(i);
        const char* file = spec_.string(e, "file");
        if (!file || e.entId >= uint32_t(kMaxSpins)) continue;
        snprintf(path, sizeof(path), "%s%s", kDir, file);
        spinModels_[e.entId].load(path);
    }
    loaded_ = true;
    return true;
}

void Level::begin(const net::Factory& f) {
    levelDoId_ = f.doId;
    levelZoneId_ = f.levelZoneId;
    for (int i = 0; i < numZones_; ++i) zoneIds_[i] = i < f.numZones ? f.zoneIds[i] : 0;
    if (f.numZones != numZones_)
        TT_LOG("level: server sent %d zone ids for %d spec zones", int(f.numZones), numZones_);
    entranceId_ = f.entranceId;
    levelStartMs_ = f.startMs;
    toonIndex_ = 0;
    numToons_ = f.numPlayers > 0 ? f.numPlayers : 1;
    for (int i = 0; i < f.numPlayers; ++i)
        if (client_ && f.playerIds[i] == client_->avatarId()) toonIndex_ = i;
}

uint32_t Level::zoneIdOf(int zoneNum) const {
    for (int i = 0; i < numZones_; ++i)
        if (zoneNums_[i] == zoneNum) return zoneIds_[i];
    return 0;
}

void Level::handleEvent(const net::Event& ev, net::Client& client) {
    using net::EventType;
    client_ = &client;
    if (ev.type == EventType::FactoryUp || ev.type == EventType::FactoryState) {
        const net::Factory* f = client.factory();
        if (!f) return;
        if (!active_) {
            const char* name = levelName(f->factoryId);
            if (!name || !load(name)) return;
            active_ = true;
            spawned_ = false;
            zoneNum_ = -1;
            lockZoneId_ = sentLock_ = 0;
            for (Cog& c : cogs_) c = Cog{};
            client.setLevelBattlePlacer(&Level::battleCellThunk, this);
        }
        begin(*f);
    } else if (ev.type == EventType::FactoryGone) {
        shutdown();
    }
}

void Level::shutdown() {
    if (client_) client_->setLevelBattlePlacer(nullptr, nullptr);
    doorClient_ = nullptr;
    zone_.unload();
    spec_.unload();
    active_ = loaded_ = spawned_ = false;
    levelDoId_ = levelZoneId_ = 0;
    numZones_ = 0;
    for (Cog& c : cogs_) c = Cog{};
}

bool Level::spawnPose(float* x, float* y, float* z, float* h) {
    if (!active_ || spawned_) return false;
    spawned_ = true;
    for (int i = spec_.next("entrancePoint", -1); i >= 0; i = spec_.next("entrancePoint", i)) {
        const LevelEntity& e = spec_.entity(i);
        if (spec_.integer(e, "entranceId", -1) != entranceId_) continue;
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
        zone_.finishLoading(Vec3{*x, *y, *z}, &Level::cellChanged, this);
        TT_LOG("level: entrance %d at %.1f,%.1f,%.1f h %.0f", int(entranceId_), double(*x), double(*y), double(*z),
               double(*h));
        return true;
    }
    TT_LOG("level: no entrance point %d", int(entranceId_));
    *x = *y = *z = *h = 0.0f;
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
    if (client.setLevelVisibility(levelZoneId_, ids, n)) {
        visDirty_ = false;
        sentLock_ = lockZoneId_;
    }
}

bool Level::battleCell(uint32_t levelDoId, uint32_t cellId, float* x, float* y, float* z, float* h) const {
    if (!active_ || levelDoId != levelDoId_) return false;
    const LevelEntity* cell = spec_.findSynthetic("battleCell", cellId);
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
    c.spec = spec_.findSynthetic(s.reserve ? "reserveCogSpec" : "cogSpec", s.cogId);
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
    const LevelEntity* parent = c.spec ? spec_.find(c.spec->parentEntId) : nullptr;
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
    zone_.update(Vec3{x, y, z}, &Level::cellChanged, this, 200.0f); // rims of gears, lifts, mixers
    if (lockZoneId_ != sentLock_) visDirty_ = true;
    if (visDirty_ && numPendingVis_ > 0) sendVisibility(client, 0, nullptr, 0);

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
        if (!s.active || s.levelDoId != levelDoId_) continue;
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
        suits.setExternal(c->doId, s.name, s.levelDist, c->x, c->y, c->z, c->h, c->walking, !s.reserve);
    }

    // BattleBlocker.startBattle: walking into a blocker joins its battle or fights its cogs.
    if (battleIdle && !requested && nowMs - blockerMs_ > 1000) {
        for (int i = 0; i < net::kMaxLevelBlockers; ++i) {
            const net::LevelBlocker& b = client.levelBlockerSlot(i);
            if (!b.active || b.levelDoId != levelDoId_ || !b.blockerActive || b.finished) continue;
            const LevelEntity* e = spec_.find(b.entId);
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
}

void Level::draw(render::Renderer& renderer, const C3D_FVec& eye) {
    if (!active_) return;
    // The spec's farPlaneDistance is 1500; the zone vis lists already bound what is drawn.
    zone_.draw(renderer, Vec3{eye.x, eye.y, eye.z}, 400.0f);
    drawDoors(renderer);
    drawButtons(renderer);
    drawStompers(renderer);
    drawLifts(renderer);
    drawBarrels(renderer);
    drawMovers(renderer);
}

} // namespace tt::world
