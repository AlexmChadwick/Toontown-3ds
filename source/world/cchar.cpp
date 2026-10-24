#include "world/cchar.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>

#include "core/log.h"
#include "net/dc_generated.h"
#include "ui/cchar_chatter_data.h"

#ifdef __3DS__
#include "render/renderer.h"
#include "ui/projection.h"
#endif

namespace tt::world {

namespace {

constexpr const char* kDir = "sdmc:/3ds/toontown3ds/data/chars/";
constexpr float kDegToRad = 3.14159265f / 180.0f;
[[maybe_unused]] constexpr float kNametagMaxDistance = 60.0f;

} // namespace

// Upstream: toontown/char/Char.py (heights), CharDNA.py, classicchars/Distributed*.py.
struct CharInfo {
    u16 classId;
    const char* asset; // model file stem (mid LOD)
    const char* graph; // walk graph stem; <graph>_<hood>.tcp
    const char* name;
    float height;
    const char* walkStart;    // played once before walking (Pluto stand, Donald trans-back)
    const char* neutralStart; // played once on stopping (Pluto sit, Donald trans)
    float neutralRate;
    bool nametag;
};

namespace {

const CharInfo kChars[] = {
    {dc::DistributedMickey::kClassId, "mickey_800", "mickey", "Mickey", 3.0f, nullptr, nullptr, 1.0f, true},
    {dc::DistributedVampireMickey::kClassId, "vampire_mickey_800", "vampire_mickey", "Mickey", 3.0f, nullptr, nullptr, 1.0f, true},
    {dc::DistributedWitchMinnie::kClassId, "witch_minnie_800", "witch_minnie", "Minnie", 3.0f, nullptr, nullptr, 1.0f, true},
    {dc::DistributedMinnie::kClassId, "minnie_800", "minnie", "Minnie", 3.0f, nullptr, nullptr, 1.0f, true},
    {dc::DistributedGoofy::kClassId, "goofy_1000", "goofy", "Goofy", 4.8f, nullptr, nullptr, 1.0f, true},
    {dc::DistributedGoofySpeedway::kClassId, "goofy_1000", "goofy", "Goofy", 4.8f, nullptr, nullptr, 1.0f, true},
    {dc::DistributedSuperGoofy::kClassId, "super_goofy_800", "super_goofy", "Goofy", 4.8f, nullptr, nullptr, 1.0f, true},
    {dc::DistributedDaisy::kClassId, "daisy_800", "daisy", "Daisy", 4.5f, nullptr, nullptr, 1.0f, true},
    {dc::DistributedSockHopDaisy::kClassId, "sockHop_daisy_default", "sockHop_daisy", "Daisy", 4.5f, nullptr, nullptr, 1.0f, true},
    {dc::DistributedChip::kClassId, "chip_500", "chip", "Chip", 2.0f, nullptr, nullptr, 1.0f, true},
    {dc::DistributedPoliceChip::kClassId, "police_chip_default", "police_chip", "Chip", 2.0f, nullptr, nullptr, 1.0f, true},
    {dc::DistributedDale::kClassId, "dale_500", "dale", "Dale", 2.0f, nullptr, nullptr, 1.0f, true},
    {dc::DistributedJailbirdDale::kClassId, "jailbird_dale_default", "jailbird_dale", "Dale", 2.0f, nullptr, nullptr, 1.0f, true},
    {dc::DistributedDonald::kClassId, "donald_500", "donald", "Donald", 4.5f, "trans-back", "trans", 0.5f, true},
    {dc::DistributedFrankenDonald::kClassId, "franken_donald_default", "franken_donald", "Donald", 4.5f, nullptr, nullptr, 1.0f, true},
    {dc::DistributedDonaldDock::kClassId, "donald-wheel_1000", "donald-wheel", "Donald", 4.5f, nullptr, nullptr, 1.0f, false},
    {dc::DistributedPluto::kClassId, "pluto_500", "pluto", "Pluto", 3.0f, "stand", "sit", 1.0f, true},
    {dc::DistributedWesternPluto::kClassId, "western_pluto_800", "western_pluto", "Pluto", 4.5f, "stand", "sit", 1.0f, true},
};

const CharInfo* infoFor(u16 classId) {
    for (const CharInfo& c : kChars)
        if (c.classId == classId) return &c;
    return nullptr;
}

float wrap180(float h) {
    h = fmodf(h, 360.0f);
    if (h > 180.0f) h -= 360.0f;
    if (h < -180.0f) h += 360.0f;
    return h;
}

// Once-only clips hold their last frame.
float clampOnce(const render::Anim& clip, float t) {
    const float last = (clip.numFrames() - 1) / clip.fps();
    return t < 0 ? 0 : (t > last ? last : t);
}

} // namespace

bool CCharActors::loadGraph(Actor& a, const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        TT_LOG("cchar: no walk graph %s", path);
        return false;
    }
    struct Header {
        char magic[4];
        u16 version, nodes, edges, points;
        float speed;
        char start[8];
    } h;
    static_assert(sizeof(Header) == 24, "char_paths_bin.py header");
    bool ok = fread(&h, sizeof(h), 1, f) == 1 && !memcmp(h.magic, "TCP1", 4) && h.version == 1 &&
              h.nodes <= kMaxNodes && h.edges <= kMaxEdges && h.points <= kMaxPathPoints;
    for (int i = 0; ok && i < h.nodes; i++) {
        ok = fread(a.nodeName[i], 8, 1, f) == 1 && fread(a.nodePos[i], 12, 1, f) == 1;
        a.nodeName[i][7] = 0;
    }
    for (int i = 0; ok && i < h.edges; i++) {
        u8 e[4];
        u16 first[2];
        ok = fread(e, 4, 1, f) == 1 && fread(first, 4, 1, f) == 1 && e[0] < h.nodes && e[1] < h.nodes &&
             first[0] + e[3] <= h.points && e[3] >= 2;
        a.edgeSrc[i] = e[0];
        a.edgeDst[i] = e[1];
        a.edgeRay[i] = e[2];
        a.edgeCount[i] = e[3];
        a.edgeFirst[i] = first[0];
    }
    ok = ok && fread(a.points, 12, h.points, f) == h.points;
    fclose(f);
    if (!ok) {
        TT_LOG("cchar: bad walk graph %s", path);
        a.numNodes = 0;
        return false;
    }
    a.numNodes = h.nodes;
    a.numEdges = h.edges;
    a.numPoints = h.points;
    a.speed = h.speed;
    return true;
}

int CCharActors::nodeIndex(const Actor& a, const char* name) const {
    for (int i = 0; i < a.numNodes; i++)
        if (!strcmp(a.nodeName[i], name)) return i;
    return -1;
}

bool CCharActors::spawn(Actor& a, const net::CChar& c) {
    // A reused slot must not inherit the previous character's graph, turn or balloon.
    a.~Actor();
    new (&a) Actor();
    const CharInfo* info = infoFor(c.classId);
    if (!info) {
        TT_LOG("cchar: no asset for class %u", unsigned(c.classId));
        return false;
    }
    char path[96];
    snprintf(path, sizeof(path), "%s%s.ttm", kDir, info->asset);
    if (!a.model.load(path)) return false;
    const char* clipNames[kClipCount] = {"neutral", "walk", info->walkStart, info->neutralStart};
    for (int i = 0; i < kClipCount; i++) {
        a.clips[i].unload();
        if (!clipNames[i]) continue;
        snprintf(path, sizeof(path), "%s%s_%s.tta", kDir, info->asset, clipNames[i]);
        a.clips[i].load(path);
    }
    if (!a.clips[kWalk].loaded()) {
        snprintf(path, sizeof(path), "%s%s_neutral.tta", kDir, info->asset); // the wheel has no walk
        a.clips[kWalk].load(path);
    }
    snprintf(path, sizeof(path), "%s%s_%lu.tcp", kDir, info->graph, static_cast<unsigned long>(hood_));
    loadGraph(a, path);
    a.info = info;
    a.doId = c.doId;
    a.active = true;
    a.walking = false;
    a.numSeg = 0;
    a.fade = 0;
    // DistributedCCharBase.generate: start at node 'a', heading 0.
    const int start = nodeIndex(a, "a");
    a.x = start >= 0 ? a.nodePos[start][0] : 0;
    a.y = start >= 0 ? a.nodePos[start][1] : 0;
    a.z = start >= 0 ? a.nodePos[start][2] : 0;
    a.h = 0;
    if (c.classId == dc::DistributedDonaldDock::kClassId) a.z += 3.95f, a.y -= 1.0f; // on the boat
    a.isDale = (c.classId == dc::DistributedDale::kClassId || c.classId == dc::DistributedJailbirdDale::kClassId);
    a.chipId = c.chipId;
    if (a.isDale) {
        const int aIdx = nodeIndex(a, "a");
        if (aIdx >= 0) {
            a.daleOffsets[aIdx][0] = 3.0f;
            a.daleOffsets[aIdx][1] = 0.0f;
            a.hasDaleOffset[aIdx] = true;
        }
        a.x += 3.0f; // ToontownGlobals.DaleOrbitDistance
        a.h = 0;
    }
    a.clip = kNeutral;
    a.clipTime = 0;
    TT_LOG("cchar: %s (%s) at %.1f,%.1f,%.1f, %d nodes", info->name, info->asset, double(a.x), double(a.y),
           double(a.z), a.numNodes);
    return true;
}

int CCharActors::buildSegments(Segment* seg, int maxSeg, const float pts[][3], int n, float speed, float initialHeading) {
    if (n < 2 || speed <= 0 || maxSeg <= 0) return 0;
    float heading = initialHeading;
    const float* origin = pts[0]; // headsUp runs before deferred setPos: always from the first point
    int numSeg = 0;
    for (int k = 0; k + 1 < n && numSeg < maxSeg; k++) {
        Segment& s = seg[numSeg++];
        memcpy(s.start, pts[k], 12);
        memcpy(s.end, pts[k + 1], 12);
        const float dx = s.end[0] - s.start[0], dy = s.end[1] - s.start[1], dz = s.end[2] - s.start[2];
        const float duration = sqrtf(dx * dx + dy * dy + dz * dz) / speed;
        const float cur = wrap180(heading);
        float dest = wrap180(atan2f(-(s.end[0] - origin[0]), s.end[1] - origin[1]) / kDegToRad);
        while (dest - cur > 180.0f) dest -= 360.0f;
        while (dest - cur < -180.0f) dest += 360.0f;
        s.h0 = cur;
        s.h1 = dest;
        s.turnTime = fabsf(dest) / 270.0f;
        if (duration - s.turnTime > 0.01f) {
            s.moveTime = duration - s.turnTime;
            s.length = s.turnTime > s.moveTime ? s.turnTime : s.moveTime;
        } else { // only the queued jump to the segment start remains
            s.moveTime = 0;
            s.turnTime = 0;
            s.h1 = cur;
            s.length = 0;
        }
        heading = dest;
    }
    return numSeg;
}

CCharActors::DaleOrbit CCharActors::computeDaleOrbit(float t, float duration, const float srcPos[3], const float dstPos[3],
                                                    float srcTheta, float destTheta, float revs, float orbitRadius) {
    DaleOrbit out{};
    if (t < duration && duration > 0.001f) {
        const float normT = t / duration;
        const float curX = srcPos[0] + (dstPos[0] - srcPos[0]) * normT;
        const float curY = srcPos[1] + (dstPos[1] - srcPos[1]) * normT;
        const float curZ = srcPos[2] + (dstPos[2] - srcPos[2]) * normT;
        const float curTheta = srcTheta + normT * revs * 2.0f * 3.14159265f;
        out.x = curX + cosf(curTheta) * orbitRadius;
        out.y = curY + sinf(curTheta) * orbitRadius;
        out.z = curZ;
        float deg = curTheta / kDegToRad;
        deg = fmodf(deg, 360.0f);
        if (deg < 0) deg += 360.0f;
        out.h = deg;
    } else {
        out.x = dstPos[0] + cosf(destTheta) * orbitRadius;
        out.y = dstPos[1] + sinf(destTheta) * orbitRadius;
        out.z = dstPos[2];
        float deg = destTheta / kDegToRad;
        deg = fmodf(deg, 360.0f);
        if (deg < 0) deg += 360.0f;
        out.h = deg;
    }
    return out;
}

// CharStateDatas.CharWalkState.enter, including its interval-building quirks
// (docs/design/chars.md "Path file contract and exact replay", step 5).
void CCharActors::startWalk(Actor& a, const net::CChar& c) {
    const int src = nodeIndex(a, c.src), dst = nodeIndex(a, c.dst);
    a.walkStartMs = c.walkStartMs;
    if (src >= 0 && src == dst) { // standing at a node (CharNeutralState)
        a.x = a.nodePos[src][0], a.y = a.nodePos[src][1], a.z = a.nodePos[src][2];
        return;
    }
    if (src < 0 || dst < 0 || a.speed <= 0) return;
    int edge = -1;
    bool reverse = false;
    for (int i = 0; i < a.numEdges && edge < 0; i++) {
        if (a.edgeSrc[i] == src && a.edgeDst[i] == dst) edge = i;
    }
    for (int i = 0; i < a.numEdges && edge < 0; i++) {
        if (a.edgeSrc[i] == dst && a.edgeDst[i] == src) edge = i, reverse = true;
    }
    float pts[kMaxSegments + 1][3];
    int n = 0;
    if (edge >= 0) {
        for (int k = 0; k < a.edgeCount[edge] && n <= kMaxSegments; k++) {
            const int idx = a.edgeFirst[edge] + (reverse ? a.edgeCount[edge] - 1 - k : k);
            memcpy(pts[n++], a.points[idx], 12);
        }
        a.raycast = a.edgeRay[edge] != 0;
    } else { // not in the graph: straight line
        memcpy(pts[n++], a.nodePos[src], 12);
        memcpy(pts[n++], a.nodePos[dst], 12);
        a.raycast = false;
    }

    if (a.isDale) {
        if (c.chipId) a.chipId = c.chipId;
        if (c.hasFollowChip && dst >= 0) {
            a.daleOffsets[dst][0] = c.offsetX;
            a.daleOffsets[dst][1] = c.offsetY;
            a.hasDaleOffset[dst] = true;
        }
        float sOff[2] = {0, 0}, dOff[2] = {0, 0};
        if (src >= 0 && a.hasDaleOffset[src]) {
            sOff[0] = a.daleOffsets[src][0];
            sOff[1] = a.daleOffsets[src][1];
        }
        if (dst >= 0 && a.hasDaleOffset[dst]) {
            dOff[0] = a.daleOffsets[dst][0];
            dOff[1] = a.daleOffsets[dst][1];
        }
        a.daleSrcOffset[0] = sOff[0];
        a.daleSrcOffset[1] = sOff[1];
        a.daleDstOffset[0] = dOff[0];
        a.daleDstOffset[1] = dOff[1];

        // Orbit radius override for b<->c and e<->f
        if ((!strcmp(c.src, "b") && !strcmp(c.dst, "c")) || (!strcmp(c.src, "c") && !strcmp(c.dst, "b")) ||
            (!strcmp(c.src, "e") && !strcmp(c.dst, "f")) || (!strcmp(c.src, "f") && !strcmp(c.dst, "e"))) {
            a.daleOrbitDist = 2.5f;
        } else {
            a.daleOrbitDist = 3.0f;
        }

        float srcTheta = atan2f(sOff[1], sOff[0]);
        if (srcTheta < 0) srcTheta += 2.0f * 3.14159265f;
        const float srcRev = (srcTheta > 0) ? (2.0f * 3.14159265f - srcTheta) / (2.0f * 3.14159265f) : 0.0f;
        float destTheta = atan2f(dOff[1], dOff[0]);
        if (destTheta < 0) destTheta += 2.0f * 3.14159265f;

        float chipDistance = 0.0f;
        for (int k = 0; k + 1 < n; k++) {
            const float dx = pts[k + 1][0] - pts[k][0], dy = pts[k + 1][1] - pts[k][1], dz = pts[k + 1][2] - pts[k][2];
            chipDistance += sqrtf(dx * dx + dy * dy + dz * dz);
        }

        float revs = chipDistance / 13.0f;
        revs += srcRev;
        const float endingTheta = srcTheta + fmodf(revs, 1.0f) * 2.0f * 3.14159265f;
        const float diffTheta = destTheta - endingTheta;
        const float destRev = diffTheta / (2.0f * 3.14159265f);
        revs += destRev;
        while (revs < 1.0f) revs += 1.0f;

        a.daleSrcTheta = srcTheta;
        a.daleDestTheta = destTheta;
        a.daleRevs = revs;
        a.daleChipSrc[0] = pts[0][0] - sOff[0];
        a.daleChipSrc[1] = pts[0][1] - sOff[1];
        a.daleChipSrc[2] = pts[0][2];
        a.daleChipDst[0] = pts[n - 1][0] - dOff[0];
        a.daleChipDst[1] = pts[n - 1][1] - dOff[1];
        a.daleChipDst[2] = pts[n - 1][2];

        // Track duration is Chip's actual walk sequence duration
        float chipDuration = 0.0f;
        if (a.chipId) {
            for (const Actor& other : actors_) {
                if (other.active && other.doId == a.chipId && other.walkDuration > 0.001f) {
                    chipDuration = other.walkDuration;
                    break;
                }
            }
        }
        if (chipDuration <= 0.001f) {
            Segment chipSeg[kMaxSegments];
            const int nSeg = buildSegments(chipSeg, kMaxSegments, pts, n, 3.0f, a.h);
            for (int k = 0; k < nSeg; k++) chipDuration += chipSeg[k].length;
        }
        a.walkDuration = chipDuration;
        a.numSeg = 1;
        a.walking = true;
        return;
    }

    a.numSeg = buildSegments(a.seg, kMaxSegments, pts, n, a.speed, a.h);
    a.walkDuration = 0;
    for (int k = 0; k < a.numSeg; k++) a.walkDuration += a.seg[k].length;
    a.walking = true;
}

void CCharActors::sample(Actor& a, float t, const CollisionWorld* collision) {
    if (a.isDale) {
        if (t < a.walkDuration && a.walkDuration > 0.001f) {
            a.clip = kWalk;
            a.clipTime = t;
        } else {
            a.clip = kNeutral;
            a.clipTime = (t - a.walkDuration) * a.info->neutralRate;
        }
        DaleOrbit orb = computeDaleOrbit(t, a.walkDuration, a.daleChipSrc, a.daleChipDst,
                                         a.daleSrcTheta, a.daleDestTheta, a.daleRevs, a.daleOrbitDist);
        a.x = orb.x;
        a.y = orb.y;
        a.z = orb.z;
        a.h = orb.h;
        if (a.raycast && collision) {
            bool found = false;
            const float g = collision->groundHeight(a.x, a.y, a.z + 2.0f, &found, 10.0f);
            if (found) a.z = g;
        }
        return;
    }
    const render::Anim& walkStart = a.clips[kWalkStart];
    const float startLen = walkStart.loaded() ? walkStart.duration() : 0.0f;
    if (t < startLen) {
        a.clip = kWalkStart;
        a.clipTime = clampOnce(walkStart, t);
        if (a.numSeg) a.x = a.seg[0].start[0], a.y = a.seg[0].start[1], a.z = a.seg[0].start[2];
        return;
    }
    t -= startLen;
    if (t < a.walkDuration) {
        a.clip = kWalk;
        a.clipTime = t;
        for (int k = 0; k < a.numSeg; k++) {
            const Segment& s = a.seg[k];
            if (t >= s.length && k + 1 < a.numSeg) {
                t -= s.length;
                continue;
            }
            const float pm = s.moveTime > 0 ? (t < s.moveTime ? t / s.moveTime : 1.0f) : 0.0f;
            const float ph = s.turnTime > 0 ? (t < s.turnTime ? t / s.turnTime : 1.0f) : 1.0f;
            a.x = s.start[0] + (s.end[0] - s.start[0]) * pm;
            a.y = s.start[1] + (s.end[1] - s.start[1]) * pm;
            a.z = s.start[2] + (s.end[2] - s.start[2]) * pm;
            a.h = s.h0 + (s.h1 - s.h0) * ph;
            break;
        }
        if (a.raycast && collision) {
            bool found = false;
            const float g = collision->groundHeight(a.x, a.y, a.z + 2.0f, &found, 10.0f);
            if (found) a.z = g;
        }
        return;
    }
    // CharNeutralState (Pluto sits, Donald turns into the sleepwalker).
    t -= a.walkDuration;
    const Segment& last = a.seg[a.numSeg - 1];
    a.x = last.end[0], a.y = last.end[1], a.z = last.end[2];
    a.h = last.h1;
    if (a.raycast && collision) {
        bool found = false;
        const float g = collision->groundHeight(a.x, a.y, a.z + 2.0f, &found, 10.0f);
        if (found) a.z = g;
    }
    const render::Anim& stop = a.clips[kNeutralStart];
    if (stop.loaded() && t < stop.duration()) {
        a.clip = kNeutralStart;
        a.clipTime = clampOnce(stop, t);
    } else {
        a.clip = kNeutral;
        a.clipTime = (t - (stop.loaded() ? stop.duration() : 0.0f)) * a.info->neutralRate;
    }
}

void CCharActors::handleEvent(const net::Event& ev, const net::Client& client, u32 nowMs) {
    (void)nowMs;
    using net::EventType;
    if (ev.type == EventType::CCharGone) {
        for (Actor& a : actors_) {
            if (a.active && a.doId == ev.doId) {
#ifdef __3DS__
                C3D_FrameSync();
#endif
                a.model.unload();
                for (auto& c : a.clips) c.unload();
                a.active = false;
            }
        }
        return;
    }
    if (ev.type == EventType::CCharChat) {
        Actor* actor = nullptr;
        for (Actor& a : actors_)
            if (a.active && a.doId == ev.doId) actor = &a;
        if (!actor) return;
        const net::CChar* c = client.findCChar(ev.doId);
        if (!c) return;
        const char* targetName = client.avatarName(c->chatAvatarDoId);
        if (ui::chatter::formatChatter(actor->chatPhrase, sizeof(actor->chatPhrase),
                                      actor->info->classId, 0, c->chatCategory, c->chatMsgIndex, targetName)) {
            actor->chatTimer = 6.0f; // show balloon for about 6 s
            actor->chatAge = 0.0f;
            // Shortest heading turn toward target at 180 deg/s (no turn for GOODBYE)
            if (c->chatCategory != ui::chatter::kGoodbye) {
                float tx = 0, ty = 0, tz = 0;
                if (client.avatarPos(c->chatAvatarDoId, &tx, &ty, &tz)) {
                    const float dx = tx - actor->x, dy = ty - actor->y;
                    float destH = wrap180(atan2f(-dx, dy) / kDegToRad);
                    float curH = wrap180(actor->h);
                    while (destH - curH > 180.0f) destH -= 360.0f;
                    while (destH - curH < -180.0f) destH += 360.0f;
                    actor->turnH0 = curH;
                    actor->turnH1 = destH;
                    actor->turnTime = fabsf(destH - curH) / 180.0f;
                    actor->turnElapsed = 0.0f;
                    actor->turning = true;
                }
            }
        }
        return;
    }
    if (ev.type != EventType::CCharSpawn && ev.type != EventType::CCharWalk) return;
    const net::CChar* c = client.findCChar(ev.doId);
    if (!c) return;
    Actor* actor = nullptr;
    for (Actor& a : actors_)
        if (a.active && a.doId == ev.doId) actor = &a;
    if (!actor) {
        for (Actor& a : actors_)
            if (!a.active && !actor) actor = &a;
        if (!actor || !spawn(*actor, *c)) return;
    }
    if (actor->walking && actor->numSeg) { // finish the previous leg where it ended
        const Segment& last = actor->seg[actor->numSeg - 1];
        actor->x = last.end[0], actor->y = last.end[1], actor->z = last.end[2];
        actor->h = last.h1;
    }
    actor->walking = false;
    startWalk(*actor, *c);
}

void CCharActors::update(float dt, u32 nowMs, const CollisionWorld* collision) {
    for (Actor& a : actors_) {
        if (!a.active) continue;
        a.fade = a.fade + dt * 2.0f > 1.0f ? 1.0f : a.fade + dt * 2.0f;
        if (a.chatTimer > 0.0f) {
            a.chatTimer -= dt;
            a.chatAge += dt;
            if (a.chatTimer <= 0.0f) a.chatPhrase[0] = 0;
        }
        if (a.turning) {
            a.turnElapsed += dt;
            const float p = a.turnTime > 0.001f ? (a.turnElapsed < a.turnTime ? a.turnElapsed / a.turnTime : 1.0f) : 1.0f;
            a.h = wrap180(a.turnH0 + (a.turnH1 - a.turnH0) * p);
            if (!a.walking) {
                if (a.turnTime > 0.2f && p < 1.0f) {
                    a.clip = kWalk;
                    a.clipTime += dt;
                } else {
                    a.clip = kNeutral;
                    a.clipTime += dt * a.info->neutralRate;
                }
            }
            if (p >= 1.0f) a.turning = false;
        }
        if (a.walking && a.numSeg) {
            const s32 ms = static_cast<s32>(nowMs - a.walkStartMs);
            sample(a, ms > 0 ? ms / 1000.0f : 0.0f, collision);
        } else if (!a.turning) {
            a.clip = kNeutral;
            a.clipTime += dt * a.info->neutralRate;
        }
    }
}

void CCharActors::draw(render::Renderer& renderer) {
#ifdef __3DS__
    C3D_Mtx m;
    for (Actor& a : actors_) {
        if (!a.active || !a.model.loaded()) continue;
        const render::Anim* clip = &a.clips[a.clip];
        if (!clip->loaded()) clip = &a.clips[kNeutral];
        if (a.info->classId == dc::DistributedDonaldDock::kClassId) {
            // DistributedDonaldDock.generate: reparentTo(boat), setPos(0, -1, 3.95).
            if (!boat_ || !boat_->active()) continue;
            m = boat_->matrix();
            Mtx_Translate(&m, BoatActor::kDonaldOffset[0], BoatActor::kDonaldOffset[1],
                          BoatActor::kDonaldOffset[2], true);
        } else {
            Mtx_Identity(&m);
            Mtx_Translate(&m, a.x, a.y, a.z, true);
            Mtx_RotateZ(&m, a.h * kDegToRad, true);
        }
        if (clip->loaded() && clip->numVertices() == a.model.numVertices()) {
            u32 fa, fb;
            float blend;
            render::sampleClip(*clip, a.clipTime, &fa, &fb, &blend);
            renderer.drawMorph(a.model, *clip, fa, fb, blend, m);
        } else {
            renderer.drawModel(a.model, m);
        }
    }
#else
    (void)renderer;
#endif
}

void CCharActors::addBalloons(ui::Balloons& balloons, const C3D_Mtx& proj, const C3D_Mtx& view) const {
#ifdef __3DS__
    for (const Actor& a : actors_) {
        if (!a.active || !a.info->nametag) continue; // (DonaldDock hides its nametag)
        float sx, sy, depth;
        if (ui::projectToScreen(proj, view, a.x, a.y, a.z + a.info->height + 0.5f, &sx, &sy, &depth) &&
            depth < kNametagMaxDistance) {
            // Note: Upstream DistributedCCharBase uses thought bubbles (CFThought) for
            // Pluto, Western Pluto and Donald, while others use speech balloons (CFSpeech).
            // Citro2d Balloons currently renders all bubbles as speech balloons.
            const char* phrase = (a.chatTimer > 0.0f && a.chatPhrase[0]) ? a.chatPhrase : nullptr;
            balloons.addSpeaker(sx, sy, a.info->name, phrase, phrase ? a.chatAge : 99.0f, depth);
        }
    }
#else
    (void)balloons;
    (void)proj;
    (void)view;
#endif
}

int CCharActors::shadowPositions(float* xyzs, int maxOut) const {
    int n = 0;
    for (const Actor& a : actors_) {
        if (!a.active || n >= maxOut || a.info->classId == dc::DistributedDonaldDock::kClassId) continue;
        xyzs[4 * n] = a.x;
        xyzs[4 * n + 1] = a.y;
        xyzs[4 * n + 2] = a.z;
        xyzs[4 * n + 3] = a.info->height / 3.0f;
        n++;
    }
    return n;
}

int CCharActors::count() const {
    int n = 0;
    for (const Actor& a : actors_) n += a.active;
    return n;
}

bool CCharActors::firstPosition(float* x, float* y, float* z) const {
    for (const Actor& a : actors_) {
        if (!a.active) continue;
        *x = a.x, *y = a.y, *z = a.z;
        return true;
    }
    return false;
}

void CCharActors::shutdown() {
    for (Actor& a : actors_) {
        a.model.unload();
        for (auto& c : a.clips) c.unload();
        a.active = false;
    }
}

} // namespace tt::world
