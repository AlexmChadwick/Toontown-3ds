#include "world/place.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>

#include "core/log.h"

namespace tt::world {

namespace {

constexpr u32 kTtcPlayground = 2000;
// Where a toon lands in the TTC playground when there is no better anchor (ground z = 3.3
// on the prop base there; collision snaps to it).
constexpr float kDefaultSpawn[4] = {0.0f, -20.0f, 0.0f, 0.0f};
constexpr float kDegToRad = 3.14159265f / 180.0f;
// Arrival offset in front of a tunnel or door (toward the zone), toon units.
constexpr float kArrivalDistance = 8.0f;

bool exists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0;
}

} // namespace

PlaceKind placeKind(u32 zoneId) {
    const u32 local = zoneId % 1000;
    if (local < 100) return PlaceKind::Playground;
    if (local >= 500) return PlaceKind::Interior;
    return PlaceKind::Street;
}

u32 hoodOf(u32 zoneId) { return zoneId - zoneId % 1000; }

bool Place::resolve(u32 zoneId, const char* ext, char* out, size_t outSize) const {
    if (zoneId == kTtcPlayground) {
        snprintf(out, outSize, "romfs:/baked/ttc_sz.%s", ext);
        return exists(out);
    }
    const PlaceKind kind = placeKind(zoneId);
    const char* dir = kind == PlaceKind::Interior ? "interiors" : "zones";
    // A street is one file for the whole branch (2100 = Silly Street); its cells
    // (2101..2199) are server zones inside it.
    const u32 fileZone = kind == PlaceKind::Street ? zoneId - zoneId % 100 : zoneId;
    snprintf(out, outSize, "sdmc:/3ds/toontown3ds/data/%s/%lu.%s", dir, static_cast<unsigned long>(fileZone), ext);
    return exists(out);
}

namespace {

bool tzPath(u32 zoneId, char* out, size_t outSize) {
    if (placeKind(zoneId) == PlaceKind::Interior) return false;
    const u32 fileZone = placeKind(zoneId) == PlaceKind::Street ? zoneId - zoneId % 100 : zoneId;
    snprintf(out, outSize, "sdmc:/3ds/toontown3ds/data/zones/%lu.tz", static_cast<unsigned long>(fileZone));
    return exists(out);
}

} // namespace

bool Place::available(u32 zoneId) {
    Place probe;
    char path[96];
    if (tzPath(zoneId, path, sizeof(path))) return true;
    return probe.resolve(zoneId, "ttm", path, sizeof(path)) && probe.resolve(zoneId, "ttc", path, sizeof(path));
}

void Place::update(float x, float y, float z, Zone::CellChanged cb, void* user) {
    if (!zone_.loaded()) return;
    if (!zone_.update(Vec3{x, y, z}, cb, user)) TT_LOG("place: %s", zone_.error());
}

void Place::draw(render::Renderer& renderer, const C3D_FVec& eye) {
    if (zone_.loaded()) {
        zone_.draw(renderer, Vec3{eye.x, eye.y, eye.z}, 120.0f);
        return;
    }
    C3D_Mtx m;
    Mtx_Identity(&m);
    renderer.drawModel(model_, m);
}

void Place::unload() {
    zone_.unload();
    model_.unload();
    collision_.unload();
    numPoints_ = 0;
    zoneId_ = 0;
}

bool Place::load(u32 zoneId) {
    char tz[96];
    if (tzPath(zoneId, tz, sizeof(tz))) {
        unload();
        const u64 t0 = svcGetSystemTick();
        if (!zone_.load(tz)) {
            TT_LOG("place: %s", zone_.error());
            unload();
            return false;
        }
        zoneId_ = zoneId;
        loadPoints();
        TT_LOG("place: zone %lu streamed from %s (%d chunks) in %lu ms", static_cast<unsigned long>(zoneId), tz,
               zone_.numChunks(), static_cast<unsigned long>((svcGetSystemTick() - t0) / (SYSCLOCK_ARM11 / 1000)));
        return true;
    }
    char ttm[96], ttc[96];
    if (!resolve(zoneId, "ttm", ttm, sizeof(ttm)) || !resolve(zoneId, "ttc", ttc, sizeof(ttc))) {
        TT_LOG("place: zone %lu is not installed", static_cast<unsigned long>(zoneId));
        return false;
    }
    unload();
    const u64 t0 = svcGetSystemTick();
    if (!model_.load(ttm) || !collision_.load(ttc)) {
        unload();
        return false;
    }
    zoneId_ = zoneId;
    loadPoints();
    TT_LOG("place: zone %lu loaded in %lu ms (%d points)", static_cast<unsigned long>(zoneId),
           static_cast<unsigned long>((svcGetSystemTick() - t0) / (SYSCLOCK_ARM11 / 1000)), numPoints_);
    return true;
}

void Place::loadPoints() {
    numPoints_ = 0;
    pointsPath_[0] = '\0';
    if (!resolve(zoneId_, "pts", pointsPath_, sizeof(pointsPath_))) {
        pointsPath_[0] = '\0';
        return;
    }
    if (FILE* f = fopen(pointsPath_, "r")) {
        char line[128];
        while (numPoints_ < kMaxPoints && fgets(line, sizeof(line), f)) {
            Point& p = points_[numPoints_];
            if (sscanf(line, "%39s %f %f %f %f", p.name, &p.x, &p.y, &p.z, &p.h) == 5) numPoints_++;
        }
        fclose(f);
    }
}

const Place::Point* Place::point(const char* name) const {
    for (int i = 0; i < numPoints_; i++) {
        if (!strcmp(points_[i].name, name)) return &points_[i];
    }
    return nullptr;
}

const Place::Point* Place::pointWithPrefix(const char* prefix) const {
    const size_t n = strlen(prefix);
    for (int i = 0; i < numPoints_; i++) {
        if (!strncmp(points_[i].name, prefix, n)) return &points_[i];
    }
    return nullptr;
}

Exit Place::exitAt(float x, float y, float z) const {
    Exit e;
    int hits[8];
    // GravityWalker's event sphere (centre r - 0.1, radius r * 1.04) is slightly bigger than
    // the wall sphere, so a toon pressed against a door still touches its flush trigger.
    const CollisionWorld& cw = collision();
    const int n = cw.triggersAt(Vec3{x, y, z + kToonRadius - 0.1f}, kToonRadius * 1.04f, hits, 8);
    for (int i = 0; i < n && i < 8; i++) {
        const CollTrigger& t = cw.trigger(hits[i]);
        // Tunnels: owner "linktunnel_<hood>_<zone>_DNARoot" names the target zone
        // (HoodMgr.addLinkTunnelHooks).
        if (strstr(t.name, "tunnel_trigger") && !strncmp(t.owner, "linktunnel_", 11)) {
            const char* z = strchr(t.owner + 11, '_');
            if (z) {
                e.kind = Exit::Tunnel;
                e.targetZone = static_cast<u32>(strtoul(z + 1, nullptr, 10));
                snprintf(e.name, sizeof(e.name), "%s", t.owner);
                return e;
            }
        }
        // Building doors: owner "sz<block>:..." (playground) or "tb<block>:..." (street);
        // the interior is hood + 500 + block (ZoneUtil.getBranchZone / DistributedDoor).
        if (strstr(t.name, "door") && strstr(t.name, "trigger") &&
            (!strncmp(t.owner, "sz", 2) || !strncmp(t.owner, "tb", 2))) {
            const u32 block = static_cast<u32>(strtoul(t.owner + 2, nullptr, 10));
            if (block > 0) {
                e.kind = Exit::Door;
                e.targetZone = hoodOf(zoneId_) + 500 + block;
                snprintf(e.name, sizeof(e.name), "%s", t.owner);
                return e;
            }
        }
        // Inside an interior, the exit door leads back to the hood's playground/street.
        if (kind() == PlaceKind::Interior && strstr(t.name, "door") && strstr(t.name, "trigger")) {
            e.kind = Exit::Door;
            e.targetZone = hoodOf(zoneId_);
            snprintf(e.name, sizeof(e.name), "%s", t.name);
            return e;
        }
    }
    return e;
}

void Place::arrival(u32 fromZone, float* x, float* y, float* z, float* h) const {
    // Arrive at the exit that leads back to fromZone: the tunnel named for it, or the door
    // of its block. Works for both .ttc and .tz zones (triggers live in the collision).
    const CollisionWorld& cw = collision();
    const CollTrigger* back = nullptr;
    for (int i = 0; i < cw.numTriggers() && fromZone && !back; i++) {
        const CollTrigger& t = cw.trigger(i);
        if (!strstr(t.name, "trigger")) continue;
        if (!strncmp(t.owner, "linktunnel_", 11)) {
            const char* zs = strchr(t.owner + 11, '_');
            if (zs && static_cast<u32>(strtoul(zs + 1, nullptr, 10)) == fromZone) back = &t;
        } else if (placeKind(fromZone) == PlaceKind::Interior &&
                   (!strncmp(t.owner, "sz", 2) || !strncmp(t.owner, "tb", 2)) &&
                   static_cast<u32>(strtoul(t.owner + 2, nullptr, 10)) == fromZone % 100) {
            back = &t;
        }
    }
    if (!back && kind() == PlaceKind::Interior) {
        for (int i = 0; i < cw.numTriggers() && !back; i++) {
            if (strstr(cw.trigger(i).name, "door")) back = &cw.trigger(i);
        }
    }
    if (!back) {
        *x = kDefaultSpawn[0];
        *y = kDefaultSpawn[1];
        *z = kDefaultSpawn[2];
        *h = kDefaultSpawn[3];
        if (zone_.loaded()) { // streets: the middle of the zone, collision finds the ground
            *x = (boundsMin()[0] + boundsMax()[0]) * 0.5f;
            *y = (boundsMin()[1] + boundsMax()[1]) * 0.5f;
            *z = boundsMax()[2];
        }
        return;
    }
    // Step out of the exit's open side, facing away from it (upstream walks the toon out of
    // the tunnel mouth / door). The open side is the direction with the longest clear line
    // of sight (camera-mask rays); ties go toward the zone's centre.
    const float cx = (back->min[0] + back->max[0]) * 0.5f, cy = (back->min[1] + back->max[1]) * 0.5f;
    const float cz = back->min[2] + 2.0f;
    const float mx = (boundsMin()[0] + boundsMax()[0]) * 0.5f, my = (boundsMin()[1] + boundsMax()[1]) * 0.5f;
    float dx = 0.0f, dy = 1.0f, bestScore = -1.0f;
    for (int i = 0; i < 8; i++) {
        const float a = i * 3.14159265f / 4.0f;
        const float ux = cosf(a), uy = sinf(a);
        float t = 1.0f;
        const float reach = 40.0f;
        cw.cameraRaycast(Vec3{cx, cy, cz}, Vec3{cx + ux * reach, cy + uy * reach, cz}, &t);
        float toCentre = ux * (mx - cx) + uy * (my - cy);
        toCentre = toCentre > 0 ? 1.0f : 0.0f;
        const float score = t * reach + toCentre * 2.0f;
        if (score > bestScore) {
            bestScore = score;
            dx = ux;
            dy = uy;
        }
    }
    *x = cx + dx * kArrivalDistance;
    *y = cy + dy * kArrivalDistance;
    *z = back->max[2];
    *h = atan2f(-dx, dy) / kDegToRad; // Panda heading: H=0 faces +Y
}

} // namespace tt::world
