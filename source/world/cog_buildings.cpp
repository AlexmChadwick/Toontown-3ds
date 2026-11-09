#include "world/cog_buildings.h"

#include <cstdlib>
#include <cstring>

#include <cmath>
#include <cstdio>

#include "core/log.h"
#include "net/net_platform.h"
#include "world/place.h"

namespace tt::world {
namespace {
constexpr float kDegToRad = 3.14159265f / 180.0f;
constexpr float kBoardRadius = 5.0f;
const char* kDept[] = {"c", "l", "m", "s"};
}

void CogBuildings::setZone(uint32_t zone) {
    zone_ = zone;
    requestedElevator_ = 0;
    requestMs_ = 0;
    for (Anchor& a : anchors_) a = Anchor{};

    // Cog buildings and field offices only stand on streets. The Cog landmark, sign and
    // flats load when a building of that department is first seen (ensureModels).
    const bool street = placeKind(zone) == PlaceKind::Street;
    unloadExterior();
    if (street) loadExterior(zone);
    // ---- cogdo exterior: load geometry and door anchor once per street ----
    cogdoShell_.unload();
    if (street) cogdoShell_.load("sdmc:/3ds/toontown3ds/data/cogdo/shell.ttm");
    FILE* cogdoPoints = fopen("sdmc:/3ds/toontown3ds/data/cogdo/points.bin", "rb");
    if (cogdoPoints) {
        char magic[4] = {};
        uint16_t version = 0, count = 0;
        float point[3] = {};
        const bool valid = fread(magic, 1, 4, cogdoPoints) == 4 && !memcmp(magic, "CGP1", 4) &&
            fread(&version, 2, 1, cogdoPoints) == 1 && version == 1 &&
            fread(&count, 2, 1, cogdoPoints) == 1 && count == 5 && fread(point, sizeof(point), 1, cogdoPoints) == 1;
        if (valid && std::isfinite(point[0]) && std::isfinite(point[1]) && std::isfinite(point[2])) {
            memcpy(cogdoDoor_, point, sizeof(point));
        }
        fclose(cogdoPoints);
    }
    // ---- end cogdo exterior ----
    kart_.unload();
    if (zone == 10000) kart_.load("sdmc:/3ds/toontown3ds/data/cogbldg/cog_kart.ttm"); // Bossbot HQ
}

// DistributedCogKart.announceGenerate: it drives up from 10 below its spot as the doors "open"
// (trolleyEnterTrack, 5 s) and away 45 units along heading - 90 as they "close"
// (trolleyExitTrack, 5 s, to z 0.5); gone while closed. False while it is away.
static bool kartPose(const net::CogElevator& e, uint32_t nowMs, float* x, float* y, float* z) {
    const float t = int32_t(nowMs - e.stateStartMs) / 1000.0f / 5.0f;
    const float u = t < 0 ? 0 : t > 1 ? 1 : t;
    *x = e.x, *y = e.y, *z = e.z;
    if (!strcmp(e.state, "opening")) {
        *z = e.z - 10.0f * (1.0f - u);
    } else if (!strcmp(e.state, "closing")) {
        const float a = (e.h - 90.0f) * kDegToRad;
        *x = e.x + 45.0f * std::cos(a) * u, *y = e.y + 45.0f * std::sin(a) * u;
        *z = e.z + (0.5f - e.z) * u;
    } else if (!strcmp(e.state, "closed") || !strcmp(e.state, "off") || !e.state[0]) {
        return false;
    }
    return true;
}

void CogBuildings::syncAnchors(Place& place) {
    place.zone().setDrawHook(&CogBuildings::zoneDrawHook, this);
    for (uint32_t block = 0; block < kMaxBlock; ++block) {
        if (const Place::Point* p = place.buildingPoint(block)) setAnchor(block, p->x, p->y, p->z, p->h);
    }
    for (int i = 0; i < kMaxSpots; ++i) {
        char spot[24];
        snprintf(spot, sizeof(spot), "elevator_origin_%d", i);
        const Place::Point* p = place.point(spot);
        spots_[i] = Spot{};
        if (p) spots_[i] = Spot{p->x, p->y, p->z, true};
    }
}

void CogBuildings::setAnchor(uint32_t block, float x, float y, float z, float h) {
    for (Anchor& a : anchors_) {
        if (a.used && a.block == block) { a.x = x; a.y = y; a.z = z; a.h = h; return; }
    }
    for (Anchor& a : anchors_) if (!a.used) {
        a = Anchor{block, x, y, z, h, true};
        return;
    }
}

void CogBuildings::handleEvent(const net::Event& ev, const net::Client&) {
    if (ev.type == net::EventType::CogBuildingGone) {
        message_ = nullptr;
    }
}

void CogBuildings::update(net::Client& client, float x, float y, float z, uint32_t nowMs) {
    // setToSuit / setToCogdo stash the block's Toon building: collect the Cog-owned blocks this
    // street has exterior data for, and bump the revision the zone draw hook applies when it changes.
    const Block* owned[kMaxHidden] = {};
    int ownedDept[kMaxHidden] = {};
    int numOwned = 0;
    for (int n = 0; n < net::kMaxCogBuildings && numOwned < kMaxHidden; ++n) {
        const net::CogBuilding& b = client.cogBuildingSlot(n);
        if (!b.active || (b.state != net::CogBuildingState::Suit && b.state != net::CogBuildingState::Cogdo)) continue;
        if (const Block* bk = findBlock(b.block)) {
            ownedDept[numOwned] = b.state == net::CogBuildingState::Cogdo ? kFieldOffice : deptIndex(b.track);
            owned[numOwned++] = bk;
        }
    }
    bool changed = numOwned != numHidden_;
    for (int i = 0; !changed && i < numOwned; ++i) changed = owned[i] != hidden_[i] || ownedDept[i] != hiddenDept_[i];
    if (changed) {
        // The Cog pieces load once per street, when a building of that department first shows.
        for (int i = 0; i < numOwned; ++i) {
            ensureModels(ownedDept[i], *owned[i]);
            hiddenDept_[i] = ownedDept[i];
        }
        static uint32_t revisions = 0;
        numHidden_ = numOwned;
        for (int i = 0; i < numOwned; ++i) hidden_[i] = owned[i];
        hideRevision_ = numOwned ? ++revisions : 0;
        if (hideRevision_ == 0 && numOwned) hideRevision_ = ++revisions;
        for (int k = 0; k < 3; ++k) {
            hideLo_[k] = 1e30f;
            hideHi_[k] = -1e30f;
        }
        for (int i = 0; i < numOwned; ++i)
            for (int k = 0; k < 3; ++k) {
                if (owned[i]->lo[k] < hideLo_[k]) hideLo_[k] = owned[i]->lo[k];
                if (owned[i]->hi[k] > hideHi_[k]) hideHi_[k] = owned[i]->hi[k];
            }
        TT_LOG("cog buildings: %d Cog-owned blocks hidden on zone %lu", numOwned, static_cast<unsigned long>(zone_));
    }
    static uint32_t elevatorLogMs = 0;
    if (numHidden_ && nowMs - elevatorLogMs > 10000) { // which elevator state each Cog building shows
        elevatorLogMs = nowMs;
        for (int n = 0; n < net::kMaxCogBuildings; ++n) {
            const net::CogBuilding& b = client.cogBuildingSlot(n);
            if (!b.active || b.state != net::CogBuildingState::Suit) continue;
            const net::CogElevator* found = nullptr;
            for (int k = 0; k < net::kMaxCogElevators && !found; ++k) {
                const net::CogElevator& e = client.cogElevatorSlot(k);
                if (e.active && e.buildingDoId == b.doId) found = &e;
            }
            TT_LOG("cog building block %lu dept %c floors %d: elevator %s", static_cast<unsigned long>(b.block),
                   b.track > 0 ? char(b.track) : '?', int(b.numFloors), found ? found->state : "(none)");
        }
    }
    const uint32_t me = client.avatarId();
    for (int n = 0; n < net::kMaxCogElevators; ++n) {
        const net::CogElevator& e = client.cogElevatorSlot(n);
        if (!e.active) continue;
        bool aboard = false;
        for (uint32_t s : e.slots) aboard |= s == me && me != 0;
        // Facility elevators send the riders their interior zone as they leave.
        if (aboard && e.destZone && e.destZone != lastDest_) {
            pendingDest_ = lastDest_ = e.destZone;
            requestedElevator_ = 0;
        }
        // Cog building elevators: when the doors close with us aboard, go inside
        // (DistributedElevatorExt.getZoneId: the building's interior zone).
        if (aboard && e.buildingDoId && (!strcmp(e.state, "closed") || !strcmp(e.state, "closing"))) {
            if (const net::CogBuilding* b = client.findCogBuilding(e.buildingDoId)) {
                const uint32_t inside = b->interiorZoneId ? b->interiorZoneId : zone_ - zone_ % 100 + 500 + b->block;
                if (inside != lastDest_) pendingDest_ = lastDest_ = inside;
            }
        }
        // Facility elevators stand at the HQ's elevator_origin_<n> anchors; the Bossbot golf
        // karts (DistributedCogKart) where their setPosHpr puts them.
        const bool anchored = e.origin >= 0 && e.origin < kMaxSpots && spots_[e.origin].used;
        if ((anchored || e.hasPose) && !aboard && requestedElevator_ == 0) {
            const float sx = e.hasPose ? e.x : spots_[e.origin].x;
            const float sy = e.hasPose ? e.y : spots_[e.origin].y;
            const float sz = e.hasPose ? e.z : spots_[e.origin].z;
            const float dx = x - sx, dy = y - sy, dz = z - sz;
            if (dx * dx + dy * dy + dz * dz < kBoardRadius * kBoardRadius && client.sendCogElevatorBoard(e.doId)) {
                requestedElevator_ = e.doId;
                requestMs_ = nowMs;
            }
        }
        if (requestedElevator_ && nowMs - requestMs_ > 5000) requestedElevator_ = 0;
        if (e.buildingDoId == 0) continue;
        const net::CogBuilding* b = client.findCogBuilding(e.buildingDoId);
        if (!b) continue;
        Anchor* a = nullptr;
        for (Anchor& candidate : anchors_) if (candidate.used && candidate.block == b->block) a = &candidate;
        if (!a) continue;
        // The elevator stands on the suit landmark's door origin, or (field office) on the
        // field office's door, 15.463 along the same suit origin.
        float targetX = a->x, targetY = a->y, targetZ = a->z;
        if (const Block* bk = findBlock(b->block)) {
            if (b->state == net::CogBuildingState::Cogdo) {
                const float* L = bk->landmark;
                for (int j = 0; j < 3; ++j)
                    (j == 0 ? targetX : j == 1 ? targetY : targetZ) =
                        cogdoDoor_[0] * L[j] + cogdoDoor_[1] * L[3 + j] + cogdoDoor_[2] * L[6 + j] + L[9 + j];
            } else {
                float m[12];
                elevatorMatrix(*bk, deptIndex(b->track), m);
                targetX = m[9], targetY = m[10], targetZ = m[11];
            }
        }
        const float dx = x - targetX, dy = y - targetY, dz = z - targetZ;
        if (dx * dx + dy * dy + dz * dz < kBoardRadius * kBoardRadius &&
            requestedElevator_ == 0 && client.sendCogElevatorBoard(e.doId)) {
            requestedElevator_ = e.doId;
            requestMs_ = nowMs;
        }
        if (requestedElevator_ && nowMs - requestMs_ > 5000) requestedElevator_ = 0;
    }
}

bool CogBuildings::suitElevatorPoint(const net::Client& client, float* x, float* y, float* z, float* h) const {
    for (int n = 0; n < net::kMaxCogElevators; ++n) {
        const net::CogElevator& e = client.cogElevatorSlot(n);
        if (!e.active || e.buildingDoId == 0) continue;
        const net::CogBuilding* b = client.findCogBuilding(e.buildingDoId);
        if (!b || b->state != net::CogBuildingState::Suit) continue;
        if (const Block* bk = findBlock(b->block)) {
            float m[12];
            elevatorMatrix(*bk, deptIndex(b->track), m);
            *x = m[9], *y = m[10], *z = m[11];
            *h = atan2f(-m[3], m[4]) / kDegToRad + 180.0f; // facing into the cab
            return true;
        }
        for (const Anchor& a : anchors_) {
            if (!a.used || a.block != b->block) continue;
            *x = a.x;
            *y = a.y;
            *z = a.z;
            *h = a.h;
            return true;
        }
    }
    return false;
}

bool CogBuildings::blockPoint(uint32_t block, float* x, float* y, float* z, float* h) const {
    for (const Anchor& a : anchors_) {
        if (!a.used || a.block != block) continue;
        *x = a.x, *y = a.y, *z = a.z, *h = a.h;
        return true;
    }
    return false;
}

void CogBuildings::draw(render::Renderer& renderer, const net::Client& client) const {
    for (int n = 0; kart_.loaded() && n < net::kMaxCogElevators; ++n) {
        const net::CogElevator& e = client.cogElevatorSlot(n);
        float kx, ky, kz;
        if (!e.active || !e.hasPose || !kartPose(e, net::plat::nowMs(), &kx, &ky, &kz)) continue;
        C3D_Mtx m;
        Mtx_Identity(&m);
        Mtx_Translate(&m, kx, ky, kz, true);
        Mtx_RotateZ(&m, e.h * kDegToRad, true);
        renderer.drawModel(const_cast<render::Model&>(kart_), m);
    }
    for (int n = 0; n < net::kMaxCogBuildings; ++n) {
        const net::CogBuilding& b = client.cogBuildingSlot(n);
        if (!b.active) continue;
        // A block without exterior data keeps its Toon look (update() does not hide it).
        const Block* bk = findBlock(b.block);
        if (!bk) continue;
        if (b.state == net::CogBuildingState::Suit) drawSuit(renderer, client, b, *bk);
        else if (b.state == net::CogBuildingState::Cogdo) drawCogdo(renderer, *bk);
    }
}

void CogBuildings::shutdown() {
    cogdoShell_.unload(); // cogdo exterior
    unloadExterior();
}

const char* CogBuildings::popMessage() {
    const char* out = message_;
    message_ = nullptr;
    return out;
}

// ---- street look of a Cog building ----
namespace {
constexpr char kCogDir[] = "sdmc:/3ds/toontown3ds/data/cogbldg/";

// Panda row-vector 3x4 (x axis, y axis, z axis, origin): out = a then b.
void mul(const float* a, const float* b, float* out) {
    float r[12];
    for (int i = 0; i < 4; ++i)
        for (int c = 0; c < 3; ++c)
            r[3 * i + c] = a[3 * i] * b[c] + a[3 * i + 1] * b[3 + c] + a[3 * i + 2] * b[6 + c] + (i == 3 ? b[9 + c] : 0.0f);
    memcpy(out, r, sizeof(r));
}

C3D_Mtx toMtx(const float* rows) {
    C3D_Mtx m;
    Mtx_Identity(&m);
    for (int r = 0; r < 3; ++r) {
        m.r[r].x = rows[r];
        m.r[r].y = rows[3 + r];
        m.r[r].z = rows[6 + r];
        m.r[r].w = rows[9 + r];
    }
    return m;
}

void normalizeAxes(float* rows) {
    for (int i = 0; i < 3; ++i) {
        float* v = rows + 3 * i;
        const float n = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (n > 1e-6f) v[0] /= n, v[1] /= n, v[2] /= n;
    }
}

bool readAll(FILE* f, void* dst, size_t n) { return fread(dst, 1, n, f) == n; }

// p in the box's frame (rows: box from world).
bool inBox(const float* rows, const float* lo, const float* hi, const float* p, float* localZ) {
    for (int j = 0; j < 3; ++j) {
        const float q = p[0] * rows[j] + p[1] * rows[3 + j] + p[2] * rows[6 + j] + rows[9 + j];
        if (q < lo[j] || q > hi[j]) return false;
        if (j == 2 && localZ) *localZ = q;
    }
    return true;
}
} // namespace

int CogBuildings::deptIndex(int8_t track) {
    switch (track) { // setSuitData sends ord(dept)
    case 'l': return 1;
    case 'm': return 2;
    case 's': return 3;
    default: return 0;
    }
}

void CogBuildings::loadExterior(uint32_t zone) {
    char path[112];
    if (!exteriorOk_) {
        snprintf(path, sizeof(path), "%ssuit_exterior.bin", kCogDir);
        if (FILE* f = fopen(path, "rb")) {
            char magic[4];
            uint16_t version = 0, count = 0;
            exteriorOk_ = readAll(f, magic, 4) && !memcmp(magic, "TCX1", 4) && readAll(f, &version, 2) &&
                          version == 1 && readAll(f, &count, 2) && count == kDepts;
            for (int d = 0; exteriorOk_ && d < kDepts; ++d)
                exteriorOk_ = readAll(f, signOrigin_[d], 48) && readAll(f, doorOrigin_[d], 48);
            fclose(f);
        }
        if (!exteriorOk_) TT_LOG("cog buildings: %s missing or bad", path);
    }
    snprintf(path, sizeof(path), "sdmc:/3ds/toontown3ds/data/zones/%lu.tsb", static_cast<unsigned long>(zone));
    FILE* f = fopen(path, "rb");
    if (!f) {
        TT_LOG("cog buildings: no %s (Cog buildings keep the Toon look)", path);
        return;
    }
    char magic[4];
    uint16_t version = 0, count = 0;
    bool ok = readAll(f, magic, 4) && !memcmp(magic, "TSB1", 4) && readAll(f, &version, 2) && version == 2 &&
              readAll(f, &count, 2) && count <= kMaxAnchors;
    // u16 offsets index flats_ / boxes_.
    // Two passes: count flats and boxes, then read into exact-size arrays.
    long start = ftell(f);
    int flats = 0, boxes = 0;
    for (int i = 0; ok && i < count; ++i) {
        uint16_t head[6];
        ok = readAll(f, head, 12);
        if (!ok) break;
        flats += head[2];
        boxes += head[3] + head[4];
        ok = fseek(f, 72 + head[2] * 52 + (head[3] + head[4]) * 72, SEEK_CUR) == 0;
    }
    if (ok) {
        blocks_ = static_cast<Block*>(calloc(count ? count : 1, sizeof(Block)));
        flats_ = static_cast<Flat*>(calloc(flats ? flats : 1, sizeof(Flat)));
        boxes_ = static_cast<Box*>(calloc(boxes ? boxes : 1, sizeof(Box)));
        ok = blocks_ && flats_ && boxes_ && fseek(f, start, SEEK_SET) == 0;
    }
    int nf = 0, nb = 0;
    for (int i = 0; ok && i < count; ++i) {
        Block& b = blocks_[i];
        uint16_t pad;
        ok = readAll(f, &b.block, 2) && readAll(f, &b.signBatch, 2) && readAll(f, &b.numFlats, 2) &&
             readAll(f, &b.numHide, 2) && readAll(f, &b.numKeep, 2) && readAll(f, &pad, 2) &&
             readAll(f, b.landmark, 48) && readAll(f, b.lo, 12) && readAll(f, b.hi, 12);
        b.firstFlat = static_cast<uint16_t>(nf);
        b.firstBox = static_cast<uint16_t>(nb);
        for (int k = 0; ok && k < b.numFlats; ++k, ++nf) {
            uint8_t tail[4];
            ok = readAll(f, flats_[nf].rows, 48) && readAll(f, tail, 4) && tail[0] < kWalls;
            flats_[nf].wall = tail[0];
            flats_[nf].door = tail[1];
        }
        for (int k = 0; ok && k < b.numHide + b.numKeep; ++k, ++nb)
            ok = readAll(f, boxes_[nb].rows, 48) && readAll(f, boxes_[nb].lo, 12) && readAll(f, boxes_[nb].hi, 12);
    }
    fclose(f);
    if (!ok) {
        TT_LOG("cog buildings: %s is corrupt, ignored", path);
        unloadExterior();
        return;
    }
    numBlocks_ = count;
    TT_LOG("cog buildings: %s: %d blocks, %d flats, %d boxes", path, count, flats, boxes);
}

void CogBuildings::unloadExterior() {
    free(blocks_);
    free(flats_);
    free(boxes_);
    blocks_ = nullptr;
    flats_ = nullptr;
    boxes_ = nullptr;
    numBlocks_ = 0;
    numHidden_ = 0;
    hideRevision_ = 0;
    for (render::Model& m : landmarks_) m.unload();
    for (render::Model& m : icons_) m.unload();
    for (render::Model& m : walls_) m.unload();
    for (render::Model& m : cogdoWalls_) m.unload();
    cogdoTried_ = 0;
    for (render::Model& m : lights_) m.unload();
    suitDoor_.unload();
    signs_.unload();
    cab_.unload();
    leftDoor_.unload();
    rightDoor_.unload();
    triedModels_ = 0;
}

const CogBuildings::Block* CogBuildings::findBlock(uint32_t block) const {
    if (!exteriorOk_) return nullptr;
    for (int i = 0; i < numBlocks_; ++i)
        if (blocks_[i].block == block) return &blocks_[i];
    return nullptr;
}

bool CogBuildings::blockOwned(const net::Client& client, uint32_t block) const {
    for (int n = 0; n < net::kMaxCogBuildings; ++n) {
        const net::CogBuilding& b = client.cogBuildingSlot(n);
        if (b.active && b.block == block &&
            (b.state == net::CogBuildingState::Suit || b.state == net::CogBuildingState::Cogdo))
            return findBlock(block) != nullptr;
    }
    return false;
}

// loadElevator: the elevator hangs on the landmark's *_door_origin, at unit scale.
void CogBuildings::elevatorMatrix(const Block& b, int dept, float out[12]) const {
    mul(doorOrigin_[dept], b.landmark, out);
    normalizeAxes(out);
}

void CogBuildings::ensureModels(int dept, const Block& b) {
    char path[112];
    auto once = [&](uint32_t bit, render::Model& m, const char* name) {
        if (triedModels_ & bit) return;
        triedModels_ |= bit;
        snprintf(path, sizeof(path), "%s%s.ttm", kCogDir, name);
        if (!m.load(path)) TT_LOG("cog buildings: cannot load %s", path);
    };
    char name[32];
    if (dept != kFieldOffice) {
        snprintf(name, sizeof(name), "suit_landmark_%s", kDept[dept]);
        once(1u << dept, landmarks_[dept], name);
        snprintf(name, sizeof(name), "cog_icon_%s", kDept[dept]);
        once(1u << (4 + dept), icons_[dept], name);
    }
    if (!(triedModels_ & (1u << 8))) {
        triedModels_ |= 1u << 8;
        snprintf(path, sizeof(path), "sdmc:/3ds/toontown3ds/data/zones/%lu_cogsign.ttm", static_cast<unsigned long>(zone_));
        if (!signs_.load(path)) TT_LOG("cog buildings: cannot load %s", path);
    }
    once(1u << 17, suitDoor_, "suit_door");
    if (dept == kFieldOffice) {
        for (int k = 0; k < b.numFlats; ++k) {
            const int w = flats_[b.firstFlat + k].wall;
            if (cogdoTried_ & (1u << w)) continue;
            cogdoTried_ |= 1u << w;
            snprintf(path, sizeof(path), "%scogdo_wall_%d.ttm", kCogDir, w);
            if (!cogdoWalls_[w].load(path)) TT_LOG("cog buildings: cannot load %s", path);
        }
        return;
    }
    once(1u << 9, cab_, "elevator_cab");
    once(1u << 10, leftDoor_, "elevator_left-door");
    once(1u << 11, rightDoor_, "elevator_right-door");
    for (int i = 0; i < 5; ++i) {
        snprintf(name, sizeof(name), "elevator_floor_light_%d", i + 1);
        once(1u << (12 + i), lights_[i], name);
    }
    for (int k = 0; k < b.numFlats; ++k) {
        const int w = flats_[b.firstFlat + k].wall;
        snprintf(name, sizeof(name), "suit_wall_%d", w);
        once(1u << (18 + w), walls_[w], name);
    }
}

void CogBuildings::zoneDrawHook(void* user, render::Model& model, bool far) {
    CogBuildings& self = *static_cast<CogBuildings*>(user);
    if (!self.numHidden_) {
        model.setHidden(0, nullptr, nullptr);
        return;
    }
    const float* lo = model.boundsMin();
    const float* hi = model.boundsMax();
    bool overlap = true;
    for (int k = 0; k < 3; ++k) overlap = overlap && lo[k] <= self.hideHi_[k] + 0.1f && self.hideLo_[k] - 0.1f <= hi[k];
    self.hideFar_ = far;
    model.setHidden(self.hideRevision_, overlap ? &CogBuildings::hideTest : nullptr, &self);
}

// bake_cog_exterior.py: three corners in one hide box, not all within 0.4 of the building's
// base (its local z 0; some landmarks reach below the street, so not the box floor) and not all in one keep box. Far meshes cluster their
// corners, so they test the centroid.
bool CogBuildings::hideTest(void* user, const float* a, const float* b, const float* c) {
    const CogBuildings& self = *static_cast<const CogBuildings*>(user);
    const float centre[3] = {(a[0] + b[0] + c[0]) / 3, (a[1] + b[1] + c[1]) / 3, (a[2] + b[2] + c[2]) / 3};
    const float* pts[3] = {a, b, c};
    const int numPts = self.hideFar_ ? 1 : 3;
    if (self.hideFar_) pts[0] = centre;
    for (int h = 0; h < self.numHidden_; ++h) {
        const Block& bk = *self.hidden_[h];
        bool near = true;
        for (int i = 0; i < numPts && near; ++i)
            for (int k = 0; k < 3; ++k) near = near && pts[i][k] >= bk.lo[k] - 0.1f && pts[i][k] <= bk.hi[k] + 0.1f;
        if (!near) continue;
        bool hide = false;
        for (int x = 0; x < bk.numHide && !hide; ++x) {
            const Box& box = self.boxes_[bk.firstBox + x];
            float top = -1e30f;
            bool all = true;
            for (int i = 0; i < numPts && all; ++i) {
                float z = 0;
                all = inBox(box.rows, box.lo, box.hi, pts[i], &z);
                if (z > top) top = z;
            }
            hide = all && top > 0.4f; // local z 0 is the building's base: the street there stays
        }
        if (!hide) continue;
        for (int x = 0; x < bk.numKeep; ++x) {
            const Box& box = self.boxes_[bk.firstBox + bk.numHide + x];
            bool all = true;
            for (int i = 0; i < numPts && all; ++i) all = inBox(box.rows, box.lo, box.hi, pts[i], nullptr);
            if (all) return false;
        }
        return true;
    }
    return false;
}

void CogBuildings::drawSuit(render::Renderer& renderer, const net::Client& client, const net::CogBuilding& b,
                            const Block& bk) const {
    const int dept = deptIndex(b.track);
    auto draw = [&](const render::Model& m, const C3D_Mtx& mtx) {
        if (m.loaded()) renderer.drawModel(const_cast<render::Model&>(m), mtx);
    };
    // setupSuitBuilding: suit_landmark_<dept><level> (one model per department), its sign.
    const C3D_Mtx landmark = toMtx(bk.landmark);
    draw(landmarks_[dept], landmark);
    if (signs_.loaded() && bk.signBatch + 2u * dept + 1 < signs_.numBatches()) {
        for (int part = 0; part < 2; ++part) {
            renderer.setOnlyBatch(bk.signBatch + 2 * dept + part);
            draw(signs_, landmark);
        }
        renderer.setOnlyBatch(-1);
    }
    // loadElevator: the cab with the department icon and one grey light per floor; the
    // doors follow the exterior elevator's state.
    float rows[12];
    elevatorMatrix(bk, dept, rows);
    const C3D_Mtx cab = toMtx(rows);
    draw(cab_, cab);
    draw(icons_[dept], cab);
    float open = 0.0f;
    for (int n = 0; n < net::kMaxCogElevators; ++n) {
        const net::CogElevator& e = client.cogElevatorSlot(n);
        if (!e.active || e.buildingDoId != b.doId) continue;
        const float age = int32_t(net::plat::nowMs() - e.stateStartMs) / 1000.0f;
        const float t = age < 0 ? 0 : age > 1 ? 1 : age;
        if (!strcmp(e.state, "opening")) open = t;
        else if (!strcmp(e.state, "waitEmpty") || !strcmp(e.state, "waitCountdown")) open = 1.0f;
        else if (!strcmp(e.state, "closing")) open = 1.0f - t;
        break;
    }
    for (int side = 0; side < 2; ++side) {
        C3D_Mtx door = cab;
        Mtx_Translate(&door, (side ? -1.0f : 1.0f) * 3.5f * (1.0f - open), 0, 0, true);
        draw(side ? rightDoor_ : leftDoor_, door);
    }
    renderer.setBaseTint(0.5f, 0.5f, 0.5f, 1.0f); // ElevatorConstants.LIGHT_OFF_COLOR
    for (int i = 0; i < 5 && i < b.numFloors; ++i) draw(lights_[i], cab);
    renderer.setBaseTint(1, 1, 1, 1);
    // libtoontown setup_suit_flat_building: a suit wall per Toon flat, suit_door if it had one.
    for (int k = 0; k < bk.numFlats; ++k) {
        const Flat& fl = flats_[bk.firstFlat + k];
        draw(walls_[fl.wall], toMtx(fl.rows));
        if (!fl.door) continue;
        float d[12];
        memcpy(d, fl.rows, sizeof(d));
        for (int j = 0; j < 3; ++j) d[9 + j] += 0.5f * fl.rows[j];
        normalizeAxes(d);
        draw(suitDoor_, toMtx(d));
    }
}
// setupCogdo: the field office (with elevatorB on its door) 15.463 along the suit origin and its
// "<title> Field Office" sign; libtoontown setup_cogdo_flat_building: a cogdo wall per Toon
// flat, suit_door if it had one. setToCogdo colour scales every piece 0.6 (the field office
// and its sign are baked with it).
void CogBuildings::drawCogdo(render::Renderer& renderer, const Block& bk) const {
    auto draw = [&](const render::Model& m, const C3D_Mtx& mtx) {
        if (m.loaded()) renderer.drawModel(const_cast<render::Model&>(m), mtx);
    };
    const C3D_Mtx landmark = toMtx(bk.landmark);
    draw(cogdoShell_, landmark);
    if (signs_.loaded() && bk.signBatch + 2u * kFieldOffice + 1 < signs_.numBatches()) {
        for (int part = 0; part < 2; ++part) {
            renderer.setOnlyBatch(bk.signBatch + 2 * kFieldOffice + part);
            draw(signs_, landmark);
        }
        renderer.setOnlyBatch(-1);
    }
    renderer.setBaseTint(0.6f, 0.6f, 0.6f, 1.0f);
    for (int k = 0; k < bk.numFlats; ++k) {
        const Flat& fl = flats_[bk.firstFlat + k];
        draw(cogdoWalls_[fl.wall], toMtx(fl.rows));
        if (!fl.door) continue;
        float d[12];
        memcpy(d, fl.rows, sizeof(d));
        for (int j = 0; j < 3; ++j) d[9 + j] += 0.5f * fl.rows[j];
        normalizeAxes(d);
        draw(suitDoor_, toMtx(d));
    }
    renderer.setBaseTint(1, 1, 1, 1);
}
// ---- end street look ----

} // namespace tt::world
