#include "world/suits.h"

#include "core/memory.h"
#include "world/suit_assets.h"

#include <algorithm> // cogdo: bounded external actor scale
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
constexpr const char* kSuitDir = "sdmc:/3ds/toontown3ds/data/suit/";

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
    loadDepartments();
    if (!model_.load(kModelPath)) {
        TT_LOG("suits: no %s, cogs stay invisible", kModelPath);
        return false;
    }
    return true;
}

// departments.ttx: TTX1 with four tables (c, l, m, s) of three textures (blazer, sleeve, legs).
void SuitActors::loadDepartments() {
    char path[96];
    snprintf(path, sizeof(path), "%sdepartments.ttx", kSuitDir);
    FILE* f = fopen(path, "rb");
    if (!f) return;
    struct {
        char magic[4];
        u16 version, numTextures, counts[4];
        u32 mapOffset, texOffset, reserved[2];
    } h;
    static_assert(sizeof(h) == 32, "TTX1 header");
    u16 map[12];
    render::TexEntry entries[12];
    bool ok = fread(&h, sizeof(h), 1, f) == 1 && !memcmp(h.magic, "TTX1", 4) && h.version == 1 &&
              h.counts[0] == 3 && h.counts[1] == 3 && h.counts[2] == 3 && h.counts[3] == 3 && h.numTextures <= 12 &&
              fseek(f, h.mapOffset, SEEK_SET) == 0 && fread(map, 2, 12, f) == 12 && fseek(f, h.texOffset, SEEK_SET) == 0 &&
              fread(entries, sizeof(render::TexEntry), h.numTextures, f) == h.numTextures;
    for (int d = 0; ok && d < 4; d++) {
        for (int e = 0; ok && e < 3; e++) {
            const u16 t = map[d * 3 + e];
            ok = t < h.numTextures && render::loadTexture(f, entries[t], &deptTex_[d][e]);
        }
    }
    fclose(f);
    deptLoaded_ = ok;
    TT_LOG("suits: department pack %s", ok ? "loaded" : "missing or bad");
}

// File stem of body index b: "suitA" .. "suitC", then "suitA_skeleton" .. "suitC_skeleton".
static void bodyStem(int b, char* out, size_t size) {
    snprintf(out, size, "suit%c%s", static_cast<char>('A' + b % 3), b >= 3 ? "_skeleton" : "");
}

bool SuitActors::loadBody(int b) {
    if (b < 0 || b >= 6) return false;
    Body& body = bodies_[b];
    if (body.tried) return body.model.loaded();
    body.tried = true;
    char stem[24], path[96];
    bodyStem(b, stem, sizeof(stem));
    snprintf(path, sizeof(path), "%s%s_full.ttm", kSuitDir, stem);
    if (!assetBatch_.load(body.model, path)) return false;
    snprintf(path, sizeof(path), "%s%s_full_neutral.tta", kSuitDir, stem);
    assetBatch_.load(body.neutral, path);
    snprintf(path, sizeof(path), "%s%s_full_walk.tta", kSuitDir, stem);
    assetBatch_.load(body.walk, path);
    if (b >= 3) loadTies();
    return body.model.loaded();
}

// Suit.py generateCorporateTie for skeletons: skeleton_ties.ttx, one entry per department.
void SuitActors::loadTies() {
    if (tiesLoaded_) return;
    tiesLoaded_ = true;
    char path[96];
    snprintf(path, sizeof(path), "%sskeleton_ties.ttx", kSuitDir);
    FILE* f = fopen(path, "rb");
    if (!f) return;
    struct {
        char magic[4];
        u16 version, numTextures, counts[4];
        u32 mapOffset, texOffset, reserved[2];
    } h;
    static_assert(sizeof(h) == 32, "TTX1 header");
    u16 map[4];
    render::TexEntry entries[4];
    bool ok = fread(&h, sizeof(h), 1, f) == 1 && !memcmp(h.magic, "TTX1", 4) && h.version == 1 &&
              h.counts[0] == 1 && h.counts[1] == 1 && h.counts[2] == 1 && h.counts[3] == 1 && h.numTextures <= 4 &&
              fseek(f, h.mapOffset, SEEK_SET) == 0 && fread(map, 2, 4, f) == 4 && fseek(f, h.texOffset, SEEK_SET) == 0 &&
              fread(entries, sizeof(render::TexEntry), h.numTextures, f) == h.numTextures;
    for (int d = 0; ok && d < 4; d++) ok = map[d] < h.numTextures && render::loadTexture(f, entries[map[d]], &tieTex_[d]);
    fclose(f);
    if (!ok) memset(tieTex_, 0, sizeof(tieTex_));
    TT_LOG("suits: skeleton ties %s", ok ? "loaded" : "missing or bad");
}

SuitActors::Head* SuitActors::head(const char* code) {
    for (Head& h : heads_)
        if (h.tried && !strcmp(h.code, code)) return h.model.loaded() ? &h : nullptr;
    for (Head& h : heads_) {
        if (h.tried) continue;
        h.tried = true;
        snprintf(h.code, sizeof(h.code), "%s", code);
        char path[96];
        snprintf(path, sizeof(path), "%shead_%s.ttm", kSuitDir, code);
        assetBatch_.load(h.model, path);
        return nullptr;
    }
    return nullptr; // cache full (a street rarely shows more than a dozen head types)
}

const render::Anim* SuitActors::battleClip(int b, const char* name) {
    if (!name || !*name || b < 0 || b >= 6) return nullptr;
    Body& body = bodies_[b];
    for (int i = 0; i < Body::kClips; i++)
        if (body.clipName[i][0] && !strcmp(body.clipName[i], name)) return body.clips[i].loaded() ? &body.clips[i] : nullptr;
    for (int i = 0; i < Body::kClips; i++) {
        if (body.clipName[i][0]) continue;
        snprintf(body.clipName[i], sizeof(body.clipName[i]), "%s", name);
        char stem[24], path[112];
        bodyStem(b, stem, sizeof(stem));
        snprintf(path, sizeof(path), "%s%s_full_%s.tta", kSuitDir, stem, name);
        assetBatch_.load(body.clips[i], path);
        return nullptr;
    }
    return nullptr;
}

bool SuitActors::loadLose(int b) {
    if (b < 0 || b >= 6) return false;
    Body& body = bodies_[b];
    if (!body.loseTried) {
        body.loseTried = true;
        char stem[24], path[112];
        bodyStem(b, stem, sizeof(stem));
        snprintf(path, sizeof(path), "%s%s_lose_full.ttm", kSuitDir, stem);
        body.loseModel.load(path);
        snprintf(path, sizeof(path), "%s%s_lose_full_lose.tta", kSuitDir, stem);
        body.loseClip.load(path);
        if (!body.loseModel.loaded() || !body.loseClip.loaded() ||
            body.loseClip.numVertices() != body.loseModel.numVertices()) {
            TT_LOG("suits: no usable lose model for %s", stem);
        }
    }
    return body.loseModel.loaded() && body.loseClip.loaded() &&
           body.loseClip.numVertices() == body.loseModel.numVertices();
}

// actor transform * species scale * body morph; head at the body clip's attach matrix.
void SuitActors::drawReal(render::Renderer& renderer, const Actor& a, const C3D_Mtx& world) {
    const SuitAsset* info = a.type >= 0 ? findSuitAsset(kSuitTypes[a.type].code) : nullptr;
    const int b = info ? info->body - 'a' + (a.skelecog ? 3 : 0) : -1;
    Body& body = bodies_[b < 0 ? 0 : b];
    render::Tints tints;
    tints.setAll(1.0f, 1.0f, 1.0f);
    for (int i = 0; i < 4 && !a.skelecog; i++) tints.rgba[render::kTintLeg][i] = info ? info->hand[i] : 1.0f; // slot 4: hands
    const int d = deptIndex(a.dept);
    const C3D_Tex* slot[render::kTexSlotCount] = {nullptr, &deptTex_[d][0], &deptTex_[d][1], &deptTex_[d][2],
                                                  tieTex_[d].data ? &tieTex_[d] : nullptr};
    C3D_Mtx m = world;
    // Suit.makeSkeleton: getGeomNode().setScale(scale * 1.0173).
    const float scale = info->scale * (a.skelecog ? 1.0173f : 1.0f);
    Mtx_Scale(&m, scale, scale, scale);
    // Battle clips (world/battle.h kSuitAnim*) play once from clipStartMs and hold their last frame.
    const char* battleName = nullptr;
    switch (a.pose.anim) {
    case 10: battleName = "pie-small-react"; break;
    case 11: battleName = "squirt-small-react"; break;
    case 12: battleName = a.clipArg >= 0 && a.clipArg < 6 && info->attackClips[a.clipArg][0] ? info->attackClips[a.clipArg] : "throw-paper"; break;
    case 13: battleName = "lose"; break;
    case 14: battleName = "sidestep-left"; break;
    default: break;
    }
    // A defeated Cog plays 'lose' on its lose model (Suit.getLoseActor), never on the body.
    const bool losing = a.pose.anim == 13 && loadLose(b < 0 ? 0 : b);
    render::Model& model = losing ? body.loseModel : body.model;
    const render::Anim* battle = losing ? &body.loseClip : battleName ? battleClip(b, battleName) : nullptr;
    const render::Anim& clip = battle ? *battle : a.pose.anim == kSuitWalk && body.walk.loaded() ? body.walk : body.neutral;
    float t = a.animTime;
    if (battle) {
        t = static_cast<s32>(nowMs_ - a.clipStartMs) / 1000.0f;
        const float last = (clip.numFrames() - 1) / clip.fps();
        t = t < 0 ? 0 : t > last ? last : t;
    }
    if (clip.loaded() && clip.numVertices() == model.numVertices()) {
        u32 fa, fb;
        float blend;
        render::sampleClip(clip, t, &fa, &fb, &blend);
        renderer.drawMorph(model, clip, fa, fb, blend, m, &tints, deptLoaded_ ? slot : nullptr);
        if (clip.hasAttach() && !a.skelecog) { // a skelecog has no head of its own
            if (Head* h = head(info->code)) {
                C3D_Mtx attach, hm;
                clip.attach(fa, fb, blend, &attach);
                Mtx_Multiply(&hm, &m, &attach);
                renderer.drawModel(h->model, hm);
            }
        }
    } else {
        renderer.drawModel(body.model, m, &tints, deptLoaded_ ? slot : nullptr);
    }
}

void SuitActors::shutdown() {
    assetBatch_.clear();
    for (Actor& a : actors_) {
        a.legs.clear();
        a.active = false;
    }
    graph_.unload();
    graphBranch_ = 0;
    C3D_FrameSync();
    model_.unload();
    for (Body& b : bodies_) {
        b.model.unload();
        b.neutral.unload();
        b.walk.unload();
        for (int i = 0; i < Body::kClips; i++) {
            b.clips[i].unload();
            b.clipName[i][0] = 0;
        }
        b.loseModel.unload();
        b.loseClip.unload();
        b.loseTried = false;
        b.tried = false;
    }
    for (Head& h : heads_) {
        h.model.unload();
        h.tried = false;
    }
    if (deptLoaded_) {
        for (auto& d : deptTex_)
            for (C3D_Tex& t : d) {
                tt::mem::LinearGuard g;
                C3D_TexDelete(&t);
            }
    }
    deptLoaded_ = false;
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
    a.skelecog = s.skelecog != 0;
    if (info) {
        // TTLocalizer.SuitBaseNameWithLevel; makeSkeleton renames the cog "Skelecog".
        snprintf(a.label, sizeof(a.label), "%s Level %d", a.skelecog ? "Skelecog" : info->name,
                 info->baseLevel + s.levelDist + 1);
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
    case Mode::External: // the level runtime sets the pose (setExternal)
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
    nowMs_ = nowMs;
    for (Actor& a : actors_) {
        if (!a.active) continue;
        a.animTime += dt;
        if (a.needBuild) buildPath(a);
        evaluate(a, nowMs, collision);
    }
}

void SuitActors::draw(render::Renderer& renderer) {
    if (!model_.loaded()) return;
    C3D_Mtx m;
    for (const Actor& a : actors_) {
        if (!a.active || !a.poseValid || !a.pose.visible || a.pose.alpha <= 0.0f) continue;
        const SuitAsset* info = a.type >= 0 ? findSuitAsset(kSuitTypes[a.type].code) : nullptr;
        if (info && loadBody(info->body - 'a' + (a.skelecog ? 3 : 0))) {
            Mtx_Identity(&m);
            Mtx_Translate(&m, a.pose.x, a.pose.y, a.pose.z, true);
            Mtx_RotateZ(&m, a.pose.h * kDegToRad, true);
            // ---- cogdo: model-space scale from CogdoMazeGameGlobals.SuitData ----
            Mtx_Scale(&m, a.externalScale, a.externalScale, a.externalScale);
            // ---- end cogdo ----
            const float f = a.flash;
            renderer.setBaseTint(1.0f, 1.0f - f * 0.6f, 1.0f - f * 0.6f, 1.0f); // hit flash
            drawReal(renderer, a, m);
            continue;
        }
        const float h = heightOf(a), w = suitWidth(h);
        const float* tint = kDeptTint[deptIndex(a.dept)];
        const float f = a.flash;
        renderer.setBaseTint(tint[0] + (1.0f - tint[0]) * f, tint[1] * (1.0f - f), tint[2] * (1.0f - f), 1.0f);
        Mtx_Identity(&m);
        Mtx_Translate(&m, a.pose.x, a.pose.y, a.pose.z, true);
        Mtx_RotateZ(&m, a.pose.h * kDegToRad, true);
        Mtx_Scale(&m, w * a.externalScale, w * a.externalScale, h * a.externalScale); // cogdo
        renderer.drawModel(model_, m);
    }
    renderer.setBaseTint(1.0f, 1.0f, 1.0f, 1.0f);
}

void SuitActors::addBalloons(ui::Balloons& balloons, const C3D_Mtx& proj, const C3D_Mtx& view) const {
    for (const Actor& a : actors_) {
        if (!a.active || !a.poseValid || !a.pose.visible || a.pose.alpha < 0.5f) continue;
        // MovieUtil.createSuitDeathTrack hides the Cog (and its nametag) for its lose actor.
        if (a.pose.anim == 13) continue;
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

u32 SuitActors::touching(float x, float y, float z) const {
    for (const Actor& a : actors_) {
        if (!a.active || !a.poseValid || !a.pose.visible || a.mode != Mode::Walk || a.legType != kLegWalk) continue;
        const float dx = a.pose.x - x, dy = a.pose.y - y;
        const float reach = kToonRadius + 1.0f; // tube radius ~1 (Suit.py collision tube) + toon sphere
        if (dx * dx + dy * dy < reach * reach && z > a.pose.z - 1.0f && z < a.pose.z + heightOf(a)) return a.doId;
    }
    return 0;
}

bool SuitActors::setExternal(u32 doId, const char* code, int levelDist, float x, float y, float z, float h,
                             bool walking, bool visible, bool skelecog, float externalScale) {
    Actor* actor = nullptr;
    for (Actor& a : actors_)
        if (a.active && a.doId == doId) actor = &a;
    if (!actor) {
        for (Actor& a : actors_)
            if (!a.active && !actor) actor = &a;
        if (!actor) return false;
        actor->~Actor();
        new (actor) Actor();
        actor->active = true;
        actor->doId = doId;
        actor->mode = Mode::External;
        const SuitInfo* info = findType(code);
        actor->type = info ? static_cast<int>(info - kSuitTypes) : -1;
        actor->dept = info ? info->dept : 'c';
        actor->skelecog = skelecog;
        if (info) snprintf(actor->label, sizeof(actor->label), "%s Level %d", skelecog ? "Skelecog" : info->name,
                           info->baseLevel + levelDist + 1);
        else snprintf(actor->label, sizeof(actor->label), "Cog");
    }
    actor->pose.x = x, actor->pose.y = y, actor->pose.z = actor->pose.groundZ = z, actor->pose.h = h;
    actor->externalScale = std::clamp(externalScale, 0.1f, 3.0f); // cogdo source scale
    actor->pose.alpha = 1.0f;
    actor->pose.visible = visible;
    actor->pose.anim = walking ? kSuitWalk : kSuitNeutral;
    actor->legType = walking ? kLegWalk : kLegOff;
    actor->poseValid = true;
    return true;
}

void SuitActors::removeExternal(u32 doId) {
    for (Actor& a : actors_)
        if (a.active && a.doId == doId && a.mode == Mode::External) a.active = false;
}

bool SuitActors::pose(u32 doId, float* x, float* y, float* z, float* h) const {
    for (const Actor& a : actors_) {
        if (!a.active || a.doId != doId || !a.poseValid) continue;
        *x = a.pose.x, *y = a.pose.y, *z = a.pose.z, *h = a.pose.h;
        return true;
    }
    return false;
}

void SuitActors::overridePose(u32 doId, float x, float y, float z, float h, bool hidden, float flash, u8 anim,
                              s8 clipArg, u32 clipStartMs) {
    for (Actor& a : actors_) {
        if (!a.active || a.doId != doId) continue;
        a.pose.x = x, a.pose.y = y, a.pose.z = a.pose.groundZ = z, a.pose.h = h;
        a.pose.visible = !hidden;
        a.pose.alpha = 1.0f;
        a.poseValid = true;
        a.mode = Mode::Frozen; // the battle owns it now; a later setPathState resumes the walk
        a.flash = flash;
        a.pose.anim = anim;
        a.clipArg = clipArg;
        a.clipStartMs = clipStartMs;
    }
}

void SuitActors::drawShowcase(render::Renderer& renderer, const char* code, float x, float y, float z, float h,
                              bool walk, float t) {
    Actor a;
    for (int i = 0; i < kNumSuitTypes; i++)
        if (!strcmp(kSuitTypes[i].code, code)) a.type = i, a.dept = kSuitTypes[i].dept;
    const SuitAsset* info = a.type >= 0 ? findSuitAsset(code) : nullptr;
    if (!info || !loadBody(info->body - 'a')) return;
    a.pose.anim = walk ? kSuitWalk : kSuitNeutral;
    a.animTime = t;
    C3D_Mtx m;
    Mtx_Identity(&m);
    Mtx_Translate(&m, x, y, z, true);
    Mtx_RotateZ(&m, h * kDegToRad, true);
    drawReal(renderer, a, m);
}

void SuitActors::logState() const {
    for (const Actor& a : actors_) {
        if (!a.active) continue;
        TT_LOG("suits: %u mode %d state %d legs %d leg %d type %d valid %d vis %d at %.1f,%.1f,%.1f", unsigned(a.doId),
               int(a.mode), int(a.state), a.legs.numLegs(), a.legIndex, int(a.legType), int(a.poseValid),
               int(a.pose.visible), double(a.pose.x), double(a.pose.y), double(a.pose.z));
    }
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
