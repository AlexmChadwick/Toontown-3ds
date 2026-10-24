#include "world/suit_path.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "core/log.h"

namespace tt::world {

namespace {

constexpr float kRadToDeg = 57.2957795f;
constexpr uint16_t kNone16 = 0xFFFF;
constexpr uint32_t kNoNode = 0xFFFFFFFFu;
constexpr uint32_t kMaxPoolNodes = 1u << 20; // hard stop for a runaway search (6 MB)

// SuitBase.makeLegList / DistributedSuitBase.beginSupaFlyMove: the suit lands (or leaves)
// from SuitTimings.fromSky * SuitWalkSpeed above its point. 'landing' is 79 frames at 24 fps
// in suit{A,B,C}-landing.bam, groundF = 28, so:
//   impactLength    = 79/24 - 28/24      = 2.125 s  (waits on the ground after touching down)
//   timeTillLanding = 6.5 - impactLength = 4.375 s  (the descent)
//   waitTime        = 4.375 - 28/24      = 3.2083 s (landing anim starts this far into the leg)
constexpr float kLandingDuration = 79.0f / 24.0f;
constexpr float kImpactLength = kLandingDuration - 28.0f / 24.0f;
constexpr float kTimeTillLanding = kSuitFromSky - kImpactLength;
constexpr float kLandingWait = kTimeTillLanding - 28.0f / 24.0f;
constexpr float kFlyHeight = kSuitFromSky * kSuitWalkSpeed;
constexpr float kFadeTime = 1.0f;

// Growable array without exceptions (malloc/realloc).
template <typename T>
struct Buf {
    T* p = nullptr;
    uint32_t n = 0, cap = 0;
    ~Buf() { free(p); }
    bool push(const T& v) {
        if (n == cap) {
            const uint32_t c = cap ? cap * 2 : 256;
            T* np = static_cast<T*>(realloc(p, c * sizeof(T)));
            if (!np) return false;
            p = np;
            cap = c;
        }
        p[n++] = v;
        return true;
    }
};

float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

void lerp3(const float* a, const float* b, float f, float* out) {
    for (int i = 0; i < 3; i++) out[i] = a[i] + (b[i] - a[i]) * f;
}

float dist3(const float* a, const float* b) {
    const float dx = b[0] - a[0], dy = b[1] - a[1], dz = b[2] - a[2];
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

} // namespace

// ================================================================== SuitGraph
void SuitGraph::unload() {
    free(denseOf_);
    free(dnaIdx_);
    free(type_);
    free(pos_);
    free(block_);
    free(edgeFirst_);
    free(edgeTo_);
    free(edgeZone_);
    free(cells_);
    denseOf_ = dnaIdx_ = edgeFirst_ = edgeTo_ = edgeZone_ = block_ = nullptr;
    type_ = nullptr;
    pos_ = nullptr;
    cells_ = nullptr;
    numPoints_ = numEdges_ = numCells_ = 0;
    maxIndex_ = 0;
    branch_ = 0;
}

bool SuitGraph::load(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* data = size > 16 && size < (1 << 20) ? static_cast<uint8_t*>(malloc(static_cast<size_t>(size))) : nullptr;
    bool ok = data && fread(data, 1, static_cast<size_t>(size), f) == static_cast<size_t>(size);
    fclose(f);
    if (ok) ok = loadFromMemory(data, static_cast<size_t>(size));
    free(data);
    if (!ok) TT_LOG("suitpath: bad or missing %s", path);
    return ok;
}

namespace {
uint16_t rd16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | p[1] << 8); }
float rdf(const uint8_t* p) {
    float v;
    memcpy(&v, p, 4);
    return v;
}
} // namespace

bool SuitGraph::loadFromMemory(const uint8_t* d, size_t size) {
    unload();
    if (size < 16 || memcmp(d, "TSP1", 4) != 0 || rd16(d + 4) != 1) return false;
    const int np = rd16(d + 8), ne = rd16(d + 10), nc = rd16(d + 12);
    if (np == 0 || size != 16u + 20u * np + 6u * ne + 16u * nc) return false;
    branch_ = rd16(d + 6);
    numPoints_ = np;
    numEdges_ = ne;
    numCells_ = nc;
    dnaIdx_ = static_cast<uint16_t*>(malloc(sizeof(uint16_t) * np));
    type_ = static_cast<uint8_t*>(malloc(np));
    pos_ = static_cast<float*>(malloc(sizeof(float) * 3 * np));
    block_ = static_cast<uint16_t*>(malloc(sizeof(uint16_t) * np));
    edgeFirst_ = static_cast<uint16_t*>(calloc(np + 1, sizeof(uint16_t)));
    edgeTo_ = static_cast<uint16_t*>(malloc(sizeof(uint16_t) * (ne ? ne : 1)));
    edgeZone_ = static_cast<uint16_t*>(malloc(sizeof(uint16_t) * (ne ? ne : 1)));
    cells_ = static_cast<SuitCell*>(malloc(sizeof(SuitCell) * (nc ? nc : 1)));
    if (!dnaIdx_ || !type_ || !pos_ || !block_ || !edgeFirst_ || !edgeTo_ || !edgeZone_ || !cells_) {
        unload();
        return false;
    }
    const uint8_t* p = d + 16;
    maxIndex_ = 0;
    for (int i = 0; i < np; i++, p += 20) {
        dnaIdx_[i] = rd16(p);
        type_[i] = p[2];
        if (type_[i] > kCogHqOutPoint) {
            unload();
            return false;
        }
        for (int k = 0; k < 3; k++) pos_[3 * i + k] = rdf(p + 4 + 4 * k);
        block_[i] = rd16(p + 16);
        if (dnaIdx_[i] > maxIndex_) maxIndex_ = dnaIdx_[i];
    }
    denseOf_ = static_cast<uint16_t*>(malloc(sizeof(uint16_t) * (maxIndex_ + 1u)));
    if (!denseOf_) {
        unload();
        return false;
    }
    for (int i = 0; i <= maxIndex_; i++) denseOf_[i] = kNone16;
    // DNAStorage::_suit_point_map[index] = point: a repeated index resolves to the last one.
    for (int i = 0; i < np; i++) denseOf_[dnaIdx_[i]] = static_cast<uint16_t>(i);

    // Edges: counting sort by start point, file order preserved inside each group.
    for (int e = 0; e < ne; e++) {
        const int a = dense(rd16(p + 6 * e)), b = dense(rd16(p + 6 * e + 2));
        if (a < 0 || b < 0) {
            unload();
            return false;
        }
        edgeFirst_[a + 1]++;
    }
    for (int i = 0; i < np; i++) edgeFirst_[i + 1] = static_cast<uint16_t>(edgeFirst_[i + 1] + edgeFirst_[i]);
    uint16_t* fill = static_cast<uint16_t*>(malloc(sizeof(uint16_t) * np));
    if (!fill) {
        unload();
        return false;
    }
    memcpy(fill, edgeFirst_, sizeof(uint16_t) * np);
    for (int e = 0; e < ne; e++) {
        const int a = dense(rd16(p + 6 * e)), b = dense(rd16(p + 6 * e + 2));
        edgeTo_[fill[a]] = static_cast<uint16_t>(b);
        edgeZone_[fill[a]] = rd16(p + 6 * e + 4);
        fill[a]++;
    }
    free(fill);
    p += 6 * ne;
    for (int i = 0; i < nc; i++, p += 16) {
        cells_[i].zone = rd16(p);
        cells_[i].width = p[2];
        cells_[i].height = p[3];
        cells_[i].x = rdf(p + 4);
        cells_[i].y = rdf(p + 8);
        cells_[i].z = rdf(p + 12);
    }
    return true;
}

uint16_t SuitGraph::edgeZone(int a, int b) const {
    if (a < 0 || a >= numPoints_) return 0;
    for (int e = edgeFirst_[a]; e < edgeFirst_[a + 1]; e++) {
        if (edgeTo_[e] == b) return edgeZone_[e];
    }
    return 0;
}

// DNAStorage::get_suit_path_breadth_first, step for step:
//  * working paths are nodes (point, parent); a chain is the list of path heads, in the order
//    the original's linked chain visits them. Each generation prepends every new path as it
//    is created, so the next chain is the creation order reversed;
//  * the first min_length - 1 generations expand every path without ever looking for the end
//    point (loops allowed); they never step onto door points, nor straight back to the
//    previous point;
//  * the following generations look for an edge to the end point first (any edge, so the
//    first path in chain order that touches it wins) and otherwise extend with the same
//    rules, additionally letting each point be claimed once per generation (the original
//    passes visited_points by value, so the set starts empty again every generation).
int SuitGraph::genPath(uint16_t startDna, uint16_t endDna, int minLen, int maxLen, uint16_t* out, int cap) const {
    const int s = dense(startDna), e = dense(endDna);
    if (s < 0 || e < 0 || cap < 2) return 0;
    Buf<uint16_t> nodePt;
    Buf<uint32_t> nodePar;
    Buf<uint32_t> chain, next;
    uint8_t* visited = static_cast<uint8_t*>(malloc(static_cast<size_t>(numPoints_)));
    if (!visited) return 0;
    int result = 0;
    bool fail = !nodePt.push(static_cast<uint16_t>(s)) || !nodePar.push(kNoNode) || !chain.push(0);
    int length = 1;
    for (int phase = 0; phase < 2 && !fail; phase++) {
        // Phase 0: min_length - 1 generations of pure expansion. Phase 1: the real search.
        while (!fail && (phase == 0 ? length < minLen - 1 : (length < maxLen && chain.n > 0))) {
            ++length;
            next.n = 0;
            if (phase == 1) memset(visited, 0, static_cast<size_t>(numPoints_));
            for (uint32_t ci = 0; ci < chain.n && !fail; ci++) {
                const uint32_t node = chain.p[ci];
                const int cur = nodePt.p[node];
                const int prev = nodePar.p[node] == kNoNode ? -1 : nodePt.p[nodePar.p[node]];
                for (int k = edgeFirst_[cur]; k < edgeFirst_[cur + 1] && !fail; k++) {
                    const int nxt = edgeTo_[k];
                    if (phase == 1 && nxt == e) {
                        // Solution: path of this chain entry plus the end point.
                        int n = 1;
                        for (uint32_t q = node; q != kNoNode; q = nodePar.p[q]) n++;
                        if (n > cap) {
                            fail = true;
                            break;
                        }
                        int i = n - 1;
                        out[i--] = static_cast<uint16_t>(nxt);
                        for (uint32_t q = node; q != kNoNode; q = nodePar.p[q]) out[i--] = nodePt.p[q];
                        result = n;
                        goto done;
                    }
                    if (terminal(nxt)) continue;     // only step onto street points
                    if (nxt == cur || nxt == prev) continue; // "Invalid edge detected in dna"
                    if (phase == 1) {
                        if (visited[nxt]) continue;
                        visited[nxt] = 1;
                    }
                    if (nodePt.n >= kMaxPoolNodes || !nodePt.push(static_cast<uint16_t>(nxt)) ||
                        !nodePar.push(node) || !next.push(nodePt.n - 1)) {
                        fail = true;
                    }
                }
            }
            // The original prepends each new path: the next chain is the creation order reversed.
            chain.n = 0;
            for (uint32_t i = next.n; i > 0 && !fail; i--) {
                if (!chain.push(next.p[i - 1])) fail = true;
            }
        }
    }
    if (fail) TT_LOG("suitpath: search %u -> %u gave up (pool %u nodes)", unsigned(startDna), unsigned(endDna),
                     unsigned(nodePt.n));
done:
    free(visited);
    return result;
}

// ================================================================= SuitLegList
void SuitLegList::clear() {
    free(legs_);
    legs_ = nullptr;
    numLegs_ = 0;
}

// SuitLegList::SuitLegList (panda/src/suit/suitLegList.cxx).
bool SuitLegList::build(const SuitGraph& g, const uint16_t* path, int count, float walkSpeed) {
    clear();
    if (count < 2) return false;
    // One leg per pair of points, plus from/to legs and the CogHQ door legs.
    int cap = count + 8;
    for (int i = 0; i < count; i++) {
        if (g.type(path[i]) == kCogHqInPoint || g.type(path[i]) == kCogHqOutPoint) cap++;
    }
    legs_ = static_cast<SuitLeg*>(malloc(sizeof(SuitLeg) * static_cast<size_t>(cap)));
    if (!legs_) return false;

    auto add = [&](int type, double start, float legTime, int zone, int block, int a, int b) {
        SuitLeg& l = legs_[numLegs_++];
        l.type = static_cast<uint8_t>(type);
        l.zone = static_cast<uint16_t>(zone);
        l.block = static_cast<uint16_t>(block);
        l.a = static_cast<uint16_t>(a);
        l.b = static_cast<uint16_t>(b);
        l.startTime = static_cast<float>(start);
        l.legTime = legTime;
    };

    int i = 0;
    int pi = path[i];
    int nextPi = path[i + 1];
    // get_first_leg_type
    int type = g.type(pi) == kSideDoorPoint ? kLegFromSuitBuilding : kLegFromSky;
    double time = 0.0;
    int zone = 0;
    double legTime = type == kLegFromSky ? kSuitFromSky : (type == kLegFromSuitBuilding ? kSuitFromSuitBuilding : 0.0);
    add(type, time, static_cast<float>(legTime), zone, 0, pi, nextPi);
    time += legTime;

    i++;
    while (i < count) {
        nextPi = path[i];
        zone = g.edgeZone(pi, nextPi);
        if (g.type(pi) == kCogHqOutPoint) {
            // About to walk out of a CogHQ door: a leg to open it.
            legTime = kSuitFromSuitBuilding;
            add(kLegFromCogHq, time, static_cast<float>(legTime), zone, g.block(pi), pi, pi);
            time += legTime;
        }
        // get_next_leg_type
        if (g.type(nextPi) == kFrontDoorPoint || g.type(nextPi) == kSideDoorPoint) {
            type = kLegWalkFromStreet;
        } else if (g.type(pi) == kFrontDoorPoint || g.type(pi) == kSideDoorPoint) {
            type = kLegWalkToStreet;
        } else {
            type = kLegWalk;
        }
        // DNAStorage::get_suit_edge_travel_time: float distance / float rate
        const float travel = dist3(g.pos(nextPi), g.pos(pi)) / walkSpeed;
        legTime = travel;
        add(type, time, travel, zone, 0, pi, nextPi);
        time += legTime;
        if (g.type(nextPi) == kCogHqInPoint) {
            legTime = kSuitToSuitBuilding;
            add(kLegToCogHq, time, static_cast<float>(legTime), zone, g.block(nextPi), nextPi, nextPi);
            time += legTime;
        }
        pi = nextPi;
        i++;
    }

    // get_last_leg_type
    switch (g.type(pi)) {
    case kFrontDoorPoint:
        type = kLegToToonBuilding;
        legTime = kSuitToToonBuilding;
        break;
    case kSideDoorPoint:
        type = kLegToSuitBuilding;
        legTime = kSuitToSuitBuilding;
        break;
    default:
        type = kLegToSky;
        legTime = kSuitToSky;
        break;
    }
    const int penultimate = path[count - 2];
    add(type, time, static_cast<float>(legTime), zone, 0, penultimate, nextPi);
    time += legTime;
    add(kLegOff, time, 0.0f, zone, 0, penultimate, nextPi);
    // Extend the zone id backwards from leg 1 to leg 0.
    if (numLegs_ > 1) legs_[0].zone = legs_[1].zone;
    return true;
}

int SuitLegList::legIndexAtTime(float time, int start) const {
    if (start < 0 || start >= numLegs_ || legs_[start].startTime > time) start = 0;
    int i = start;
    while (i + 1 < numLegs_ && legs_[i + 1].startTime <= time) i++;
    return i;
}

// ================================================================== poses
float suitHeading(const float* a, const float* b) {
    const float xd = b[0] - a[0], yd = b[1] - a[1];
    if (yd == 0.0f) return xd > 0.0f ? -90.0f : 90.0f;
    if (xd == 0.0f) return yd > 0.0f ? 0.0f : 180.0f;
    return atan2f(yd, xd) * kRadToDeg - 90.0f;
}

namespace {

// beginBuildingMove-style helper: delta scaled to (length - STAND_OUTSIDE_DOOR) / length.
void shortened(const float* a, const float* b, float* delta, float* len) {
    for (int i = 0; i < 3; i++) delta[i] = b[i] - a[i];
    *len = sqrtf(delta[0] * delta[0] + delta[1] * delta[1] + delta[2] * delta[2]);
}

} // namespace

void samplePose(const SuitGraph& g, const SuitLeg& leg, float t, SuitPose* out) {
    const float* a = g.pos(leg.a);
    const float* b = g.pos(leg.b);
    const float T = leg.legTime;
    if (t < 0.0f) t = 0.0f;
    const float f = T > 0.0f ? clamp01(t / T) : 1.0f;
    out->h = suitHeading(a, b);
    out->alpha = 1.0f;
    out->anim = kSuitWalk;
    out->visible = true;
    float p[3] = {b[0], b[1], b[2]};
    float delta[3], delta2[3], len;
    switch (leg.type) {
    case kLegWalk:
        lerp3(a, b, f, p);
        break;
    case kLegWalkToStreet: { // a is the door: start STAND_OUTSIDE_DOOR out from it
        shortened(a, b, delta, &len);
        const float s = len > 0.0f ? (len - kSuitStandOutsideDoor) / len : 0.0f;
        float a1[3];
        for (int i = 0; i < 3; i++) a1[i] = b[i] - delta[i] * s;
        lerp3(a1, b, f, p);
        break;
    }
    case kLegWalkFromStreet:
    case kLegToToonBuilding: { // stop STAND_OUTSIDE_DOOR short of the door
        shortened(a, b, delta, &len);
        const float s = len > 0.0f ? (len - kSuitStandOutsideDoor) / len : 0.0f;
        float b1[3];
        for (int i = 0; i < 3; i++) b1[i] = a[i] + delta[i] * s;
        if (leg.type == kLegToToonBuilding) {
            for (int i = 0; i < 3; i++) p[i] = b1[i];
            out->anim = kSuitNeutral;
        } else {
            lerp3(a, b1, f, p);
        }
        break;
    }
    case kLegFromSuitBuilding: { // walks out of the building to STAND_OUTSIDE_DOOR from the door
        shortened(a, b, delta, &len);
        const float k = len > 0.0f ? kSuitWalkSpeed * T / len : 0.0f;
        const float s = len > 0.0f ? (len - kSuitStandOutsideDoor) / len : 0.0f;
        float a1[3], b1[3];
        for (int i = 0; i < 3; i++) {
            delta2[i] = delta[i] * k;
            b1[i] = b[i] - delta[i] * s;
            a1[i] = b1[i] - delta2[i];
        }
        lerp3(a1, b1, f, p);
        break;
    }
    case kLegToSuitBuilding: { // from STAND_OUTSIDE_DOOR before the door, on into the building
        shortened(a, b, delta, &len);
        const float k = len > 0.0f ? kSuitWalkSpeed * T / len : 0.0f;
        const float s = len > 0.0f ? (len - kSuitStandOutsideDoor) / len : 0.0f;
        float a1[3], b1[3];
        for (int i = 0; i < 3; i++) {
            delta2[i] = delta[i] * k;
            a1[i] = a[i] + delta[i] * s;
            b1[i] = a1[i] + delta2[i];
        }
        lerp3(a1, b1, f, p);
        break;
    }
    case kLegFromSky: // lands on its first point
        p[0] = a[0], p[1] = a[1], p[2] = a[2] + kFlyHeight * (1.0f - clamp01(t / kTimeTillLanding));
        out->alpha = clamp01(t / kFadeTime);
        out->anim = t < kLandingWait ? kSuitNeutral : kSuitLanding;
        break;
    case kLegToSky: // takes off from its last point after the landing anim has run backwards
        p[2] = b[2] + kFlyHeight * clamp01((t - kImpactLength) / kTimeTillLanding);
        out->alpha = clamp01((kSuitToSky - t) / kFadeTime);
        out->anim = kSuitLanding;
        break;
    case kLegToCogHq:
        out->anim = kSuitNeutral;
        break;
    case kLegFromCogHq:
        p[0] = a[0], p[1] = a[1], p[2] = a[2];
        out->anim = kSuitNeutral;
        out->visible = false;
        break;
    case kLegOff:
    default:
        out->visible = false;
        out->anim = kSuitNeutral;
        break;
    }
    out->x = p[0];
    out->y = p[1];
    out->z = p[2];
    out->groundZ = leg.type == kLegFromSky ? a[2] : (leg.type == kLegToSky ? b[2] : p[2]);
}

void flyAwayPose(float x, float y, float z, float h, float t, SuitPose* out) {
    out->x = x;
    out->y = y;
    out->groundZ = z;
    out->z = z + kFlyHeight * clamp01((t - kImpactLength) / kTimeTillLanding);
    out->h = h;
    out->alpha = t < 0.0f ? 1.0f : clamp01((kSuitToSky - t) / kFadeTime);
    out->anim = t < 0.0f ? kSuitNeutral : kSuitLanding;
    out->visible = t < kSuitToSky;
}

} // namespace tt::world
