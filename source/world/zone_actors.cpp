#include "world/zone_actors.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/log.h"
#include "net/dc_generated.h"
#include "ui/projection.h"
#include "world/collision.h"

namespace tt::world {

namespace {

constexpr float kDegToRad = 3.14159265f / 180.0f;
constexpr float kTreasureRadius = 2.0f; // DistributedTreasure.getSphereRadius
constexpr float kTreasureFadeSec = 1.0f; // treasureFlyTrack: 0.8 s out + 0.2 s
constexpr float kNametagMaxDistance = 60.0f; // far tags behind buildings are just clutter

} // namespace

bool ZoneActors::init(ToonCache* cache, const char* pointsPath, const char* treasureModelPath) {
    cache_ = cache;
    loadPoints(pointsPath);
    treasureModel_.load(treasureModelPath);
    return true;
}

void ZoneActors::loadPoints(const char* pointsPath) {
    numPoints_ = 0;
    if (FILE* f = pointsPath && pointsPath[0] ? fopen(pointsPath, "r") : nullptr) {
        char line[128];
        while (numPoints_ < kMaxPoints && fgets(line, sizeof(line), f)) {
            Point& p = points_[numPoints_];
            if (sscanf(line, "%39s %f %f %f %f", p.name, &p.x, &p.y, &p.z, &p.h) == 5) numPoints_++;
        }
        fclose(f);
    }
    TT_LOG("zone: %d anchor points from %s", numPoints_, pointsPath ? pointsPath : "(none)");
}

void ZoneActors::shutdown() {
    for (Npc& n : npcs_) {
        if (n.active && cache_) cache_->release(&n.look);
        n.active = false;
    }
    treasureModel_.unload();
}

const ZoneActors::Point* ZoneActors::findPoint(const char* name) const {
    for (int i = 0; i < numPoints_; i++) {
        if (!strcmp(points_[i].name, name)) return &points_[i];
    }
    return nullptr;
}

void ZoneActors::handleEvent(const net::Event& ev, const net::Client& client) {
    using net::EventType;
    switch (ev.type) {
    case EventType::NpcSpawn: {
        const net::NpcToon* src = client.findNpc(ev.doId);
        if (!src) return;
        // Same anchor lookup as upstream initToonState (DistributedNPC*.py).
        char anchor[40];
        if (src->classId == dc::DistributedNPCFisherman::kClassId) {
            snprintf(anchor, sizeof(anchor), "npc_fisherman_origin_%u", unsigned(src->posIndex));
        } else if (src->classId == dc::DistributedNPCPartyPerson::kClassId) {
            snprintf(anchor, sizeof(anchor), "party_person_%s", src->posIndex % 2 == 0 ? "left" : "right");
        } else {
            snprintf(anchor, sizeof(anchor), "npc_origin_%u", unsigned(src->posIndex));
        }
        const Point* p = findPoint(anchor);
        if (!p) {
            TT_LOG("zone: NPC %s has no anchor %s (interior NPC?)", src->name, anchor);
            return;
        }
        Npc* n = nullptr;
        for (Npc& s : npcs_) {
            if (!s.active && !n) n = &s;
        }
        if (!n) return;
        n->active = true;
        n->doId = ev.doId;
        snprintf(n->name, sizeof(n->name), "%s", src->name);
        n->x = p->x;
        n->y = p->y;
        n->z = p->z;
        n->h = p->h;
        ToonDna dna;
        if (!src->hasDna || !parseNetString(src->dna, net::kDnaSize, &dna)) dna = defaultToonDna();
        cache_->acquire(dna, ToonLod::High, &n->look);
        n->avatar.setLook(&n->look, &cache_->fallback());
        n->avatar.play(kAnimNeutral, 0.9f); // initToonState: setAnimState('neutral', 0.9)
        break;
    }
    case EventType::NpcDespawn:
        for (Npc& n : npcs_) {
            if (n.active && n.doId == ev.doId) {
                cache_->release(&n.look);
                n.active = false;
            }
        }
        break;
    case EventType::TreasureSpawn: {
        for (int i = 0; i < net::kMaxTreasures; i++) {
            const net::Treasure& src = client.treasureSlot(i);
            if (!src.active || src.doId != ev.doId) continue;
            Treasure* t = nullptr;
            for (Treasure& s : treasures_) {
                if (!s.active && !t) t = &s;
            }
            if (!t) return;
            *t = Treasure{};
            t->active = true;
            t->doId = ev.doId;
            t->x = src.x;
            t->y = src.y;
            t->z = src.z;
        }
        break;
    }
    case EventType::TreasureGone:
        for (Treasure& t : treasures_) {
            if (t.active && t.doId == ev.doId) t.grabbed = true; // fade out, then free
        }
        break;
    case EventType::LeftZone:
        for (Npc& n : npcs_) {
            if (n.active) cache_->release(&n.look);
            n.active = false;
        }
        for (Treasure& t : treasures_) t.active = false;
        break;
    default:
        break;
    }
}

void ZoneActors::update(float dt) {
    time_ += dt;
    for (Npc& n : npcs_) {
        if (n.active) n.avatar.update(dt);
    }
    for (Treasure& t : treasures_) {
        if (!t.active || !t.grabbed) continue;
        t.fade -= dt / kTreasureFadeSec;
        if (t.fade <= 0.0f) t.active = false;
    }
}

void ZoneActors::draw(render::Renderer& renderer, const C3D_FVec& eye) {
    C3D_Mtx m;
    for (Npc& n : npcs_) {
        if (!n.active) continue;
        Mtx_Identity(&m);
        Mtx_Translate(&m, n.x, n.y, n.z, true);
        Mtx_RotateZ(&m, n.h * kDegToRad, true);
        n.avatar.draw(renderer, m);
    }
    for (Treasure& t : treasures_) {
        if (!t.active) continue;
        // Upstream treasures are camera-facing sprites (cards face -Y): turn -Y toward the eye.
        const float heading = atan2f(eye.x - t.x, -(eye.y - t.y));
        const float s = t.grabbed ? (t.fade > 0.0f ? t.fade : 0.0f) : 1.0f;
        Mtx_Identity(&m);
        Mtx_Translate(&m, t.x, t.y, t.z + 0.15f * sinf(time_ * 2.0f), true);
        Mtx_RotateZ(&m, heading, true);
        if (s < 1.0f) Mtx_Scale(&m, s, s, s);
        renderer.drawModel(treasureModel_, m);
    }
}

void ZoneActors::addBalloons(ui::Balloons& balloons, const C3D_Mtx& proj, const C3D_Mtx& view) const {
    for (const Npc& n : npcs_) {
        if (!n.active) continue;
        float sx, sy, depth;
        if (ui::projectToScreen(proj, view, n.x, n.y, n.z + n.look.nametagHeight, &sx, &sy, &depth) &&
            depth < kNametagMaxDistance) {
            balloons.addSpeaker(sx, sy, n.name, nullptr, 99.0f, depth);
        }
    }
}

u32 ZoneActors::touchingTreasure(float x, float y, float z) const {
    const float reach = kTreasureRadius + kToonRadius;
    for (const Treasure& t : treasures_) {
        if (!t.active || t.grabbed) continue;
        const float dx = t.x - x, dy = t.y - y, dz = t.z - (z + kToonRadius);
        if (dx * dx + dy * dy + dz * dz < reach * reach) return t.doId;
    }
    return 0;
}

u32 ZoneActors::touchingNpc(float x, float y, float z, const net::Client& client, uint16_t classId) const {
    constexpr float kTubeRadius = 3.25f, kTubeLength = 5.0f;
    // GravityWalker event sphere: centre r - 0.1 above the feet, radius r * 1.04.
    const float reach = kTubeRadius + kToonRadius * 1.04f;
    const float cz = z + kToonRadius - 0.1f;
    for (const Npc& n : npcs_) {
        if (!n.active) continue;
        const net::NpcToon* src = client.findNpc(n.doId);
        if (!src || src->classId != classId) continue;
        const float h = n.h * kDegToRad;
        const float ax = n.x - sinf(h), ay = n.y + cosf(h); // tube axis: local (0, 1) rotated by heading
        const float dz = cz < n.z ? n.z - cz : cz > n.z + kTubeLength ? cz - (n.z + kTubeLength) : 0.0f;
        const float dx = x - ax, dy = y - ay;
        if (dx * dx + dy * dy + dz * dz < reach * reach) return n.doId;
    }
    return 0;
}

bool ZoneActors::nearestTreasure(float x, float y, float* tx, float* ty, float* tz) const {
    const Treasure* best = nullptr;
    float bestD = 0.0f;
    for (const Treasure& t : treasures_) {
        if (!t.active || t.grabbed) continue;
        const float d = (t.x - x) * (t.x - x) + (t.y - y) * (t.y - y);
        if (!best || d < bestD) {
            best = &t;
            bestD = d;
        }
    }
    if (!best) return false;
    *tx = best->x;
    *ty = best->y;
    *tz = best->z;
    return true;
}

int ZoneActors::shadowPositions(float* xyzs, int maxOut) const {
    int n = 0;
    for (const Npc& s : npcs_) {
        if (!s.active || n >= maxOut) continue;
        xyzs[4 * n] = s.x;
        xyzs[4 * n + 1] = s.y;
        xyzs[4 * n + 2] = s.z;
        xyzs[4 * n + 3] = s.look.valid() ? s.look.scale : 1.0f;
        n++;
    }
    return n;
}

int ZoneActors::npcCount() const {
    int n = 0;
    for (const Npc& s : npcs_) n += s.active;
    return n;
}

} // namespace tt::world
