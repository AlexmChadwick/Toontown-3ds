#include "world/level.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/log.h"
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
    // VisibilityExtender: a door that is not closed extends its zone's visibility with the
    // extender's newZones (the event fires with "not closed", DistributedDoorEntity.changedOnState).
    uint16_t extra[64];
    int numExtra = 0;
    for (int i = 0; i < net::kMaxLevelEntities; ++i) {
        const net::LevelEntityObj& d = client.levelEntitySlot(i);
        if (!d.active || d.levelDoId != levelDoId_ || d.classId != dc::DistributedDoorEntity::kClassId) continue;
        const LevelEntity* e = spec_.find(d.entId);
        if (!e) continue;
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
}

bool Level::pushOut(float* x, float* y, float z) const {
    if (!active_ || !doorClient_) return false;
    bool moved = false;
    for (int i = 0; i < net::kMaxLevelEntities; ++i) {
        const net::LevelEntityObj& d = doorClient_->levelEntitySlot(i);
        if (!d.active || d.levelDoId != levelDoId_ || d.classId != dc::DistributedDoorEntity::kClassId) continue;
        const DoorPose p = doorPose(d, doorNowMs_);
        if (!p.outer && !p.inner) continue;
        const LevelEntity* e = spec_.find(d.entId);
        if (!e) continue;
        float l[3];
        toLocal(e->world, *x, *y, z, l);
        const float r = kToonRadius;
        if (l[0] < doorBox_[0] || l[0] > doorBox_[3] || l[2] < doorBox_[2] - 1.0f || l[2] > doorBox_[5]) continue;
        const float front = doorBox_[1] - r, back = doorBox_[4] + r;
        if (l[1] <= front || l[1] >= back) continue;
        const float target = (l[1] - front < back - l[1]) ? front : back;
        // Back to world along the door's (normalized) local Y axis.
        const float* ay = e->world + 3;
        const float len = std::sqrt(ay[0] * ay[0] + ay[1] * ay[1] + ay[2] * ay[2]);
        const float scale = len > 1e-6f ? (target - l[1]) * len : 0.0f;
        const float nx = len > 1e-6f ? ay[0] / len : 0.0f, ny = len > 1e-6f ? ay[1] / len : 0.0f;
        *x += nx * scale, *y += ny * scale;
        moved = true;
    }
    return moved;
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
    *action = LevelBattleAction{};
    if (!active_) return;
    updateDoors(client, x, y, z, nowMs);
    zone_.update(Vec3{x, y, z}, &Level::cellChanged, this);
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
}

} // namespace tt::world
