#include "world/suits.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>

#include "core/log.h"
#include "ui/projection.h"
#include "world/suit_tables.h"

namespace tt::world {

namespace {

constexpr float kDegToRad = 3.14159265f / 180.0f;
constexpr float kNametagMaxDistance = 90.0f;
constexpr const char* kModelPath = "romfs:/baked/suit_placeholder.ttm";
constexpr const char* kGraphDir = "sdmc:/3ds/toontown3ds/data/zones/";

// Department index in "clms" order: tint of the stand-in box and nametag colour (ABGR).
int deptIndex(char dept) {
    switch (dept) {
    case 'c': return 0;
    case 'l': return 1;
    case 'm': return 2;
    case 's': return 3;
    default: return 0;
    }
}
constexpr float kDeptTint[4][3] = {{0.85f, 0.30f, 0.30f}, {0.30f, 0.40f, 0.85f}, {0.30f, 0.75f, 0.45f}, {0.75f, 0.35f, 0.75f}};
constexpr u32 kDeptNameColor[4] = {0xFF3030B0, 0xFFB04030, 0xFF408020, 0xFF903090};

const SuitInfo* findType(const char* code) {
    for (const SuitInfo& s : kSuitTypes) {
        if (!strcmp(s.code, code)) return &s;
    }
    return nullptr;
}

float suitWidth(float height) { return 0.6f + 0.12f * height; }

// DistributedSuit enables its floor ray on these legs only (WalkToStreet, WalkFromStreet,
// FromSuitBuilding, ToSuitBuilding): plain Walk legs keep the DNA point heights.
bool raycastLeg(u8 type) {
    return type == kLegWalkToStreet || type == kLegWalkFromStreet || type == kLegFromSuitBuilding ||
           type == kLegToSuitBuilding;
}

} // namespace

bool SuitActors::init() {
    if (!model_.load(kModelPath)) {
        TT_LOG("suits: no %s, cogs stay invisible", kModelPath);
        return false;
    }
    return true;
}

void SuitActors::shutdown() {
    for (Actor& a : actors_) {
        a.legs.clear();
        a.active = false;
    }
    graph_.unload();
    graphBranch_ = 0;
    model_.unload();
}

void SuitActors::setZone(u32 zoneId) {
    // Hood/ZoneUtil: playgrounds are zone % 1000 < 100, streets hood + n * 100 (+ 1..99 per
    // visgroup), interiors >= 500 (no suits indoors).
    const u32 inHood = zoneId % 1000;
    branch_ = inHood >= 100 && inHood < 500 ? zoneId - zoneId % 100 : 0;
}

bool SuitActors::ensureGraph(u32 branch) {
    if (branch == 0) return false;
    if (graphBranch_ == branch) return graph_.loaded();
    // One street at a time: every cog of the old graph has gone with its interest.
    for (Actor& a : actors_) {
        a.legs.clear();
        a.needBuild = a.active;
    }
    graph_.unload();
    graphBranch_ = branch;
    char path[96];
    snprintf(path, sizeof(path), "%s%lu.tsp", kGraphDir, static_cast<unsigned long>(branch));
    graphFailed_ = !graph_.load(path);
    if (!graphFailed_) {
        TT_LOG("suits: street %lu graph: %d points, %d edges, %d battle cells", static_cast<unsigned long>(branch),
               graph_.numPoints(), graph_.numEdges(), graph_.numCells());
    }
    return !graphFailed_;
}

float SuitActors::heightOf(const Actor& a) { return a.type >= 0 ? kSuitTypes[a.type].height : 5.0f; }

// DistributedSuit.setPathEndpoints: genPath + SuitBase.makeLegList.
void SuitActors::buildPath(Actor& a) {
    a.legs.clear();
    a.needBuild = false;
    if (a.pathMax == 0 || !graph_.loaded()) return; // maxPathLen 0: the suit has no path yet
    uint16_t path[kMaxSuitPathPoints];
    const int n = graph_.genPath(a.pathStart, a.pathEnd, a.pathMin, a.pathMax, path, kMaxSuitPathPoints);
    if (n < 2 || !a.legs.build(graph_, path, n)) {
        TT_LOG("suits: cog %lu: no path %u -> %u (min %u max %u)", static_cast<unsigned long>(a.doId),
               unsigned(a.pathStart), unsigned(a.pathEnd), unsigned(a.pathMin), unsigned(a.pathMax));
        return;
    }
    TT_LOG("suits: cog %lu: path %u -> %u: %d points, %d legs, %.1f s", static_cast<unsigned long>(a.doId),
           unsigned(a.pathStart), unsigned(a.pathEnd), n, a.legs.numLegs(), double(a.legs.duration()));
}

void SuitActors::refresh(Actor& a, const net::Suit& s, const net::Client& client, u32 nowMs) {
    // Identity: type, level and the nametag text.
    const SuitInfo* info = s.hasDna ? findType(s.name) : nullptr;
    a.type = info ? static_cast<int>(info - kSuitTypes) : -1;
    a.dept = info ? info->dept : s.dept;
    if (info) {
        snprintf(a.label, sizeof(a.label), "%s Level %d", info->name, info->baseLevel + s.levelDist + 1);
    } else {
        snprintf(a.label, sizeof(a.label), "Cog");
    }
    a.zone = s.zoneId;

    // Path: the planner's street (or the one the player is on) names the graph.
    u32 branch = client.suitPlannerZone(s.spDoId);
    if (branch == 0) branch = branch_;
    ensureGraph(branch);
    if (!a.haveEndpoints || s.pathStart != a.pathStart || s.pathEnd != a.pathEnd || s.pathMinLen != a.pathMin ||
        s.pathMaxLen != a.pathMax) {
        a.pathStart = s.pathStart;
        a.pathEnd = s.pathEnd;
        a.pathMin = s.pathMinLen;
        a.pathMax = s.pathMaxLen;
        a.haveEndpoints = true;
        a.needBuild = true;
    }
    if (a.needBuild) buildPath(a);
    a.posIndex = s.pathIndex;
    a.posMs = s.pathPosMs;

    if (s.pathState != a.state || a.mode == Mode::Hidden) enterState(a, s.pathState, nowMs);
}

// DistributedSuit.resumePath: the local path state machine.
void SuitActors::enterState(Actor& a, s8 state, u32 nowMs) {
    if (state == a.state && a.mode != Mode::Hidden) return;
    if (a.mode == Mode::Walk) evaluate(a, nowMs, nullptr); // the pose to freeze or fly away from
    const bool shown = a.poseValid && a.pose.visible;
    a.state = state;
    a.modeSinceMs = nowMs;
    switch (state) {
    case 1:
        a.mode = Mode::Walk;
        break;
    case 2:
    case 4:
        if (shown) {
            a.baseX = a.pose.x, a.baseY = a.pose.y, a.baseZ = a.pose.groundZ, a.baseH = a.pose.h;
            a.mode = state == 2 ? Mode::FlyAway : Mode::DanceFly;
        } else {
            a.mode = Mode::Hidden;
        }
        break;
    default: // 0 stop, 3 in battle (no-op upstream): stay where we are
        a.mode = shown ? Mode::Frozen : Mode::Hidden;
        break;
    }
}

void SuitActors::handleEvent(const net::Event& ev, const net::Client& client, u32 nowMs) {
    using net::EventType;
    if (ev.type == EventType::SuitGone) {
        for (Actor& a : actors_) {
            if (a.active && a.doId == ev.doId) {
                a.legs.clear();
                a.active = false;
            }
        }
        return;
    }
    if (ev.type == EventType::SuitPlannerUp) {
        // The planner arrived after its cogs: they may have used the wrong street.
        for (Actor& a : actors_) {
            if (!a.active) continue;
            const net::Suit* s = client.findSuit(a.doId);
            if (s) refresh(a, *s, client, nowMs);
        }
        return;
    }
    if (ev.type != EventType::SuitSpawn && ev.type != EventType::SuitUpdate) return;
    const net::Suit* s = client.findSuit(ev.doId);
    if (!s) return;
    Actor* actor = nullptr;
    for (Actor& a : actors_) {
        if (a.active && a.doId == ev.doId) actor = &a;
    }
    if (!actor) {
        for (Actor& a : actors_) {
            if (!a.active && !actor) actor = &a;
        }
        if (!actor) return;
        actor->~Actor(); // (a leftover pose must not leak into the new cog)
        new (actor) Actor();
        actor->active = true;
        actor->doId = ev.doId;
    }
    refresh(*actor, *s, client, nowMs);
    evaluate(*actor, nowMs, nullptr);
}

// The cog's pose at nowMs. collision (may be null) lifts the door legs onto the floor.
void SuitActors::evaluate(Actor& a, u32 nowMs, const CollisionWorld* collision) {
    switch (a.mode) {
    case Mode::Hidden:
        a.pose.visible = false;
        a.poseValid = true;
        return;
    case Mode::Frozen:
        return; // pose stays
    case Mode::Walk: {
        if (a.legs.numLegs() > 0 && graph_.loaded()) {
            const int idx0 = a.posIndex < a.legs.numLegs() ? a.posIndex : a.legs.numLegs() - 1;
            const float elapsed = static_cast<s32>(nowMs - a.posMs) / 1000.0f + a.legs.startTime(idx0);
            const int idx = a.legs.legIndexAtTime(elapsed, a.legIndex);
            a.legIndex = idx;
            const SuitLeg& leg = a.legs.leg(idx);
            samplePose(graph_, leg, elapsed - leg.startTime, &a.pose);
            a.legType = leg.type;
            if (collision && a.pose.visible && raycastLeg(leg.type)) {
                bool found = false;
                const float g = collision->groundHeight(a.pose.x, a.pose.y, a.pose.z + 2.0f, &found, 10.0f);
                if (found) a.pose.z = a.pose.groundZ = g;
            }
        } else {
            // No path to replay (no graph, or the search failed): stand on the battle cell of
            // our zone (docs/design/combat.md section 3, "Fallback that keeps battles working").
            a.pose.visible = false;
            a.legType = kLegOff;
            for (int i = 0; graph_.loaded() && i < graph_.numCells(); i++) {
                const SuitCell& c = graph_.cell(i);
                if (c.zone != a.zone) continue;
                a.pose.x = c.x, a.pose.y = c.y, a.pose.z = a.pose.groundZ = c.z;
                a.pose.h = 0.0f;
                a.pose.alpha = 1.0f;
                a.pose.anim = kSuitNeutral;
                a.pose.visible = true;
                break;
            }
        }
        a.poseValid = true;
        return;
    }
    case Mode::FlyAway:
    case Mode::DanceFly: {
        float t = static_cast<s32>(nowMs - a.modeSinceMs) / 1000.0f;
        if (a.mode == Mode::DanceFly) t -= kSuitVictoryDance; // DanceThenFlyAway: victory dance first
        flyAwayPose(a.baseX, a.baseY, a.baseZ, a.baseH, t, &a.pose);
        a.poseValid = true;
        return;
    }
    }
}

void SuitActors::update(float dt, u32 nowMs, const CollisionWorld* collision) {
    (void)dt;
    for (Actor& a : actors_) {
        if (!a.active) continue;
        if (a.needBuild) buildPath(a);
        evaluate(a, nowMs, collision);
    }
}

void SuitActors::draw(render::Renderer& renderer) {
    if (!model_.loaded()) return;
    C3D_Mtx m;
    for (const Actor& a : actors_) {
        if (!a.active || !a.poseValid || !a.pose.visible || a.pose.alpha <= 0.0f) continue;
        const float h = heightOf(a), w = suitWidth(h);
        const float* tint = kDeptTint[deptIndex(a.dept)];
        renderer.setBaseTint(tint[0], tint[1], tint[2], 1.0f);
        Mtx_Identity(&m);
        Mtx_Translate(&m, a.pose.x, a.pose.y, a.pose.z, true);
        Mtx_RotateZ(&m, a.pose.h * kDegToRad, true);
        Mtx_Scale(&m, w, w, h);
        renderer.drawModel(model_, m);
    }
    renderer.setBaseTint(1.0f, 1.0f, 1.0f, 1.0f);
}

void SuitActors::addBalloons(ui::Balloons& balloons, const C3D_Mtx& proj, const C3D_Mtx& view) const {
    for (const Actor& a : actors_) {
        if (!a.active || !a.poseValid || !a.pose.visible || a.pose.alpha < 0.5f) continue;
        float sx, sy, depth;
        // Avatar.py nametag height: the cog's height + 0.5.
        if (ui::projectToScreen(proj, view, a.pose.x, a.pose.y, a.pose.z + heightOf(a) + 0.5f, &sx, &sy, &depth) &&
            depth < kNametagMaxDistance) {
            balloons.addSpeaker(sx, sy, a.label, nullptr, 99.0f, depth, kDeptNameColor[deptIndex(a.dept)]);
        }
    }
}

int SuitActors::shadowPositions(float* xyzs, int maxOut) const {
    int n = 0;
    for (const Actor& a : actors_) {
        if (!a.active || !a.poseValid || !a.pose.visible || n >= maxOut) continue;
        xyzs[4 * n] = a.pose.x;
        xyzs[4 * n + 1] = a.pose.y;
        xyzs[4 * n + 2] = a.pose.groundZ;
        xyzs[4 * n + 3] = suitWidth(heightOf(a)) * 0.9f;
        n++;
    }
    return n;
}

int SuitActors::count() const {
    int n = 0;
    for (const Actor& a : actors_) n += a.active && a.poseValid && a.pose.visible;
    return n;
}

bool SuitActors::firstPosition(float* x, float* y, float* z) const {
    for (const Actor& a : actors_) {
        if (!a.active || !a.poseValid || !a.pose.visible) continue;
        *x = a.pose.x, *y = a.pose.y, *z = a.pose.z;
        return true;
    }
    return false;
}

} // namespace tt::world
