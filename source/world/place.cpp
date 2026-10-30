#include "world/place.h"

#include "render/quality.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>

#include "core/log.h"
// ---- tutorial ----
#include "world/tutorial_zones.h"
// ---- end tutorial ----

namespace tt::world {

namespace {

constexpr u32 kTtcPlayground = 2000;
// Where a toon lands in the TTC playground when there is no better anchor (ground z = 3.3
// on the prop base there; collision snaps to it).
constexpr float kDefaultSpawn[4] = {0.0f, -20.0f, 0.0f, 0.0f};
constexpr float kDegToRad = 3.14159265f / 180.0f;
// Arrival offset in front of a tunnel or door (toward the zone), toon units.
constexpr float kArrivalDistance = 8.0f;

// First entry of each hood's dropPoints (toontown/distributed/HoodMgr.py): where a toon
// lands when teleporting into a playground. x, y, z, h.
struct DropPoint {
    u32 zone;
    float x, y, z, h;
};
const DropPoint kDropPoints[] = {
    {1000, -28.0f, -2.5f, 5.8f, 120.0f},  {2000, -60.0f, -8.0f, 1.3f, -90.0f},
    {3000, 35.0f, -32.0f, 6.2f, 138.0f},   {4000, 86.0f, 44.0f, -13.5f, 121.1f},
    {5000, 0.0f, 0.0f, 0.0f, -10.5f},      {6000, -165.8f, 108.0f, 0.025f, 252.0f},
    {8000, -0.7f, 62.0f, 0.08f, 182.0f},   {9000, 77.0f, 91.0f, 0.0f, 124.4f},
    // Cog HQ courtyards (docs/design/coghq.md).
    // (Codex bakefix: checked against each HQ's collision; docs/design/coghq.md.)
    {10000, 0.0f, 0.0f, 0.0f, 0.0f},         {11000, 64.0f, -128.0f, 0.26f, 36.0f},
    {12000, 102.0f, -437.0f, -23.439f, 0.0f}, {13000, 77.5f, 129.13f, -68.4f, -166.6f},
};

bool exists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0;
}

} // namespace

PlaceKind placeKind(u32 zoneId) {
    // ---- tutorial ----
    // The Toontorial street (world/tutorial_zones.h): branch 20000 and its one visgroup 20001 are
    // a street, baked as zones/20000.tz like any other branch.
    if (zoneId == kTutorialBranch || zoneId == kTutorialStreet) return PlaceKind::Street;
    // ---- end tutorial ----
    // Cog HQ courtyards and lobbies (10000-13999) are standalone zones, one file each.
    if (zoneId >= 10000 && zoneId < 14000) return PlaceKind::Playground;
    const u32 local = zoneId % 1000;
    if (local < 100) return PlaceKind::Playground;
    if (local >= 500) return PlaceKind::Interior;
    return PlaceKind::Street;
}

u32 hoodOf(u32 zoneId) {
    // ---- tutorial ----
    // TutorialHood is Toontown Central's town: TutorialTownLoader extends TTTownLoader (TC_SZ town
    // music, TC_SZ_activity indoors, the TT sky), so its places play and show as TTC's.
    if (TutorialZones::isTutorialPlace(zoneId)) return kToontownCentral;
    // ---- end tutorial ----
    return zoneId - zoneId % 1000;
}

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

void Place::setCamera(const C3D_FVec& eye, const C3D_FVec& target) {
    zone_.setCamera(Vec3{eye.x, eye.y, eye.z}, Vec3{target.x, target.y, target.z});
}

namespace {
void applyRetain(Zone& zone) {
    if (!zone.loaded()) return;
    if (tt::render::activeQuality().new3ds)
        zone.setRetainBudget(8u * 1024u * 1024u, 12u * 1024u * 1024u);
    else
        zone.setRetainBudget(Zone::kGeometryBudget, Zone::kTextureBudget);
}
} // namespace

void Place::update(float x, float y, float z, Zone::CellChanged cb, void* user) {
    if (!zone_.loaded()) return;
    applyRetain(zone_);
    if (!zone_.update(Vec3{x, y, z}, cb, user)) TT_LOG("place: %s", zone_.error());
}

void Place::beginStreaming(float x, float y, float z, Zone::CellChanged cb, void* user) {
    if (!zone_.loaded()) return;
    applyRetain(zone_);
    const Vec3 feet{x, y, z};
    if (!zone_.update(feet, cb, user)) TT_LOG("place: %s", zone_.error());
    zone_.warmAround(feet, tt::render::activeQuality().drawDistance * 1.35f);
    if (!zone_.update(feet, cb, user)) TT_LOG("place: %s", zone_.error());
}

bool Place::streamReady() const {
    return !zone_.loaded() || (!zone_.loading() && !zone_.warming());
}

float Place::streamProgress() const {
    if (!zone_.loaded() || streamReady()) return 1.0f;
    if (!zone_.loading()) return 0.85f;
    return 0.45f;
}

void Place::finishLoading(float x, float y, float z, Zone::CellChanged cb, void* user) {
    if (!zone_.loaded()) return;
    const u64 t0 = osGetTime();
    beginStreaming(x, y, z, cb, user);
    zone_.finishLoading(Vec3{x, y, z}, cb, user);
    TT_LOG("place: %lu chunks resident in %lu ms", (unsigned long)zone_.numVisible(),
           (unsigned long)(osGetTime() - t0));
}

void Place::draw(render::Renderer& renderer, const C3D_FVec& eye) {
    if (zone_.loaded()) {
        zone_.draw(renderer, Vec3{eye.x, eye.y, eye.z}, tt::render::activeQuality().drawDistance);
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
            if (sscanf(line, "%63s %f %f %f %f", p.name, &p.x, &p.y, &p.z, &p.h) == 5) numPoints_++;
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

const Place::Point* Place::buildingPoint(u32 block) const {
    static const char* const kPrefixes[] = {"tb", "sz"};
    char name[16];
    for (const char* prefix : kPrefixes) {
        snprintf(name, sizeof(name), "%s%lu", prefix, static_cast<unsigned long>(block));
        if (const Point* p = point(name)) return p;
        snprintf(name, sizeof(name), "%s%lu:", prefix, static_cast<unsigned long>(block));
        const size_t n = strlen(name);
        for (int i = 0; i < numPoints_; i++) {
            // The DNA root itself, not its door anchors ("..._door_origin", "door_<n>").
            const char* rest = points_[i].name + n;
            if (!strncmp(points_[i].name, name, n) && !strstr(rest, "door")) return &points_[i];
        }
    }
    return nullptr;
}

const Place::Point* Place::buildingDoorPoint(u32 block, int door) const {
    // Toon HQs: "<tb|sz><block>:<DNA root>/door_<door>" (bake_zone.write_points).
    static const char* const kPrefixes[] = {"tb", "sz"};
    char prefix[16];
    char suffix[16];
    snprintf(suffix, sizeof(suffix), "/door_%d", door);
    const size_t suffixLen = strlen(suffix);
    for (const char* p : kPrefixes) {
        snprintf(prefix, sizeof(prefix), "%s%lu:", p, static_cast<unsigned long>(block));
        const size_t n = strlen(prefix);
        for (int i = 0; i < numPoints_; i++) {
            const char* name = points_[i].name;
            const size_t len = strlen(name);
            if (!strncmp(name, prefix, n) && len > suffixLen && !strcmp(name + len - suffixLen, suffix)) {
                return &points_[i];
            }
        }
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
        // Building doors: owner "sz<block>:..." (playground) or "tb<block>:..." (street).
        // Interior zone = zone - zone % 100 + 500 + block, from the exterior cell
        // (DistributedBuildingAI.getExteriorAndInteriorZoneId): TTC block 19 -> 2519,
        // Silly Street (cell 2132) block 3 -> 2603.
        if (strstr(t.name, "door") && strstr(t.name, "trigger") &&
            (!strncmp(t.owner, "sz", 2) || !strncmp(t.owner, "tb", 2))) {
            const u32 block = static_cast<u32>(strtoul(t.owner + 2, nullptr, 10));
            if (block > 0) {
                e.kind = Exit::Door;
                e.targetZone = zoneId_ - zoneId_ % 100 + 500 + block;
                snprintf(e.name, sizeof(e.name), "%s", t.owner);
                return e;
            }
        }
        // Cog HQ doors: owner "cogdoor_<destZone>_<doorIndex>_DNARoot" (lobbies, factory, ...).
        if (strstr(t.name, "door") && strstr(t.name, "trigger") && !strncmp(t.owner, "cogdoor_", 8)) {
            const u32 dest = static_cast<u32>(strtoul(t.owner + 8, nullptr, 10));
            if (dest > 0) {
                e.kind = Exit::Door;
                e.targetZone = dest;
                snprintf(e.name, sizeof(e.name), "%s", t.owner);
                return e;
            }
        }
        // Inside an interior, the exit door leads back to the exterior branch:
        // 2519 -> 2000, 2603 -> 2100 (the street then finds the exact cell underfoot).
        if (kind() == PlaceKind::Interior && strstr(t.name, "door") && strstr(t.name, "trigger")) {
            e.kind = Exit::Door;
            e.targetZone = zoneId_ - zoneId_ % 100 - 500;
            // ---- tutorial ----
            // The Toontorial's interiors open onto the tutorial street (DistributedDoor
            // setOtherZoneIdAndDoId on the street door), never the branch 20000 they are numbered from.
            if (TutorialZones::isTutorialPlace(zoneId_)) e.targetZone = kTutorialStreet;
            // ---- end tutorial ----
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
            // Street tunnels into a Cog HQ name the street's cell (linktunnel_dg_5316): match its branch.
            const u32 target = zs ? static_cast<u32>(strtoul(zs + 1, nullptr, 10)) : 0;
            if (target == fromZone ||
                (target && placeKind(target) == PlaceKind::Street && target - target % 100 == fromZone - fromZone % 100)) {
                back = &t;
            }
        } else if (!strncmp(t.owner, "cogdoor_", 8)) {
            if (static_cast<u32>(strtoul(t.owner + 8, nullptr, 10)) == fromZone) back = &t;
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
    if (!back && kind() == PlaceKind::Playground) {
        for (const DropPoint& d : kDropPoints) {
            if (d.zone == zoneId_) {
                *x = d.x;
                *y = d.y;
                *z = d.z;
                *h = d.h;
                return;
            }
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
