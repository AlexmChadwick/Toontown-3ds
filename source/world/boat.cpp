#include "core/gpu_sync.h"
#include "world/boat.h"

#include <cmath>

#include "core/log.h"
#include "world/boat_paths.h"

namespace tt::world {

namespace {

constexpr const char* kModelPath = "sdmc:/3ds/toontown3ds/data/zones/1000_boat.ttm";
constexpr const char* kDeckPath = "sdmc:/3ds/toontown3ds/data/zones/1000_boat.ttc";
constexpr const char* kSfxPaths[] = {
    "sdmc:/3ds/toontown3ds/data/audio/boat_bell.tsnd",
    "sdmc:/3ds/toontown3ds/data/audio/boat_foghorn.tsnd",
    "sdmc:/3ds/toontown3ds/data/audio/boat_creak.tsnd",
    "sdmc:/3ds/toontown3ds/data/audio/boat_lap.tsnd",
};
constexpr float kDegToRad = 3.14159265f / 180.0f;
// Source clip length (seconds), to end-align the foghorn (ParallelEndTogether), and
// DistributedBoat PIER_TIME, for the creak of the rising pier (also end-aligned).
constexpr float kFoghornSecs = 2.194f;
constexpr float kPierTime = 5.0f;
// Positional sound: full volume inside kNear, 1/d beyond, silent past kFar (the boat is a
// big source; Panda's 3D audio drops off with distance too).
constexpr float kNear = 30.0f;
constexpr float kFar = 250.0f;
// A sound whose start time was crossed longer ago than this (joined mid-sail) is skipped.
constexpr float kLateSecs = 0.75f;

// The pier ramps (boat.md "Bakefix pier handoff"): world pivot, zone-space heading, and
// DistributedBoat's pitches (0.25 flat for a docked boat, -44.2601 raised, PIER_TIME lerp).
struct PierSpec {
    const char* model;
    const char* collision;
    float pivot[3];
    float heading;
};
constexpr PierSpec kPierSpecs[2] = {
    {"sdmc:/3ds/toontown3ds/data/zones/1000_pier_east.ttm", "sdmc:/3ds/toontown3ds/data/zones/1000_pier_east.ttc",
     {-105.389381f, -7.499506f, 5.267346f}, -90.0f},
    {"sdmc:/3ds/toontown3ds/data/zones/1000_pier_west.ttm", "sdmc:/3ds/toontown3ds/data/zones/1000_pier_west.ttc",
     {55.554008f, 12.140208f, 5.279396f}, 90.0f},
};
constexpr float kPierFlat = 0.25f;
constexpr float kPierRaised = -44.2601f;

// A pier lerping between two pitches over PIER_TIME starting at `start` seconds into the sail.
float pierLerp(float t, float start, float from, float to) {
    float f = (t - start) / kPierTime;
    f = f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
    return from + (to - from) * f;
}

void transformPoint(const C3D_Mtx& m, const float* v, float* out) {
    for (int i = 0; i < 3; i++) out[i] = m.r[i].x * v[0] + m.r[i].y * v[1] + m.r[i].z * v[2] + m.r[i].w;
}

// Closest point of triangle (a, b, c) to p (Ericson, Real-Time Collision Detection 5.1.5).
void closestOnTri(const float* p, const float* a, const float* b, const float* c, float* q) {
    float ab[3], ac[3], ap[3];
    for (int i = 0; i < 3; i++) ab[i] = b[i] - a[i], ac[i] = c[i] - a[i], ap[i] = p[i] - a[i];
    auto dot = [](const float* u, const float* v) { return u[0] * v[0] + u[1] * v[1] + u[2] * v[2]; };
    auto set = [&](float u, float v) { for (int i = 0; i < 3; i++) q[i] = a[i] + ab[i] * u + ac[i] * v; };
    const float d1 = dot(ab, ap), d2 = dot(ac, ap);
    if (d1 <= 0 && d2 <= 0) return set(0, 0);
    float bp[3], cp[3];
    for (int i = 0; i < 3; i++) bp[i] = p[i] - b[i], cp[i] = p[i] - c[i];
    const float d3 = dot(ab, bp), d4 = dot(ac, bp);
    if (d3 >= 0 && d4 <= d3) return set(1, 0);
    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) return set(d1 / (d1 - d3), 0);
    const float d5 = dot(ab, cp), d6 = dot(ac, cp);
    if (d6 >= 0 && d5 <= d6) return set(0, 1);
    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) return set(0, d2 / (d2 - d6));
    const float va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) {
        const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        for (int i = 0; i < 3; i++) q[i] = b[i] + (c[i] - b[i]) * w;
        return;
    }
    const float den = 1.0f / (va + vb + vc);
    set(vb * den, vc * den);
}

// Samples a baked Mopath at t seconds (clamped to its duration): linear between samples.
void samplePath(const float (*pts)[4], int count, float duration, float t, float* out) {
    if (t < 0) t = 0;
    if (t > duration) t = duration;
    float f = t / kBoatPathStep;
    int i = static_cast<int>(f);
    if (i > count - 2) i = count - 2;
    f -= static_cast<float>(i);
    if (f > 1.0f) f = 1.0f;
    for (int k = 0; k < 4; k++) out[k] = pts[i][k] + (pts[i + 1][k] - pts[i][k]) * f;
}

} // namespace

void BoatActor::handleEvent(const net::Event& ev, const net::Client& client) {
    using net::EventType;
    if (ev.type == EventType::BoatGone) {
        if (active_ && doId_ == ev.doId) shutdown();
        return;
    }
    if (ev.type != EventType::BoatSpawn && ev.type != EventType::BoatState) return;
    const net::Boat* b = client.boat();
    if (!b || b->doId != ev.doId) return;
    if (!active_) {
        wantModel_ = true; // requested on the loader thread from update()
        if (!deck_.load(kDeckPath)) TT_LOG("boat: no deck collision %s; the toon cannot ride", kDeckPath);
        sfxLoaded_ = 0;
        fired_ = 0;
        active_ = true;
        Mtx_Identity(&matrix_);
        doId_ = b->doId;
    }
    // Upstream finish()es the previous track on a state change, which leaves the boat at the
    // end pose that the next state starts from, so only the state and its clock change.
    if (state_ != b->state || stateStartMs_ != b->stateStartMs) fired_ = 0;
    state_ = b->state;
    stateStartMs_ = b->stateStartMs;
}

// DistributedBoat state machine (docs/design/boat.md):
//   DockedEast  = end of dd-w-e    SailingWest = dd-e-w from t
//   DockedWest  = end of dd-e-w    SailingEast = dd-w-e from t
void BoatActor::pose(u32 nowMs) {
    const s32 ms = static_cast<s32>(nowMs - stateStartMs_);
    const float t = ms > 0 ? ms / 1000.0f : 0.0f;
    float p[4];
    switch (state_) {
    case net::BoatState::SailingWest:
        samplePath(kBoatEW, kBoatEWCount, kBoatEWDuration, t, p);
        break;
    case net::BoatState::DockedWest:
        samplePath(kBoatEW, kBoatEWCount, kBoatEWDuration, kBoatEWDuration, p);
        break;
    case net::BoatState::SailingEast:
        samplePath(kBoatWE, kBoatWECount, kBoatWEDuration, t, p);
        break;
    default: // DockedEast
        samplePath(kBoatWE, kBoatWECount, kBoatWEDuration, kBoatWEDuration, p);
        break;
    }
    x_ = p[0], y_ = p[1], z_ = p[2], h_ = p[3];
}

void BoatActor::setZone(u32 zone) {
    const bool want = zone == 1000;
    if (want == piersWanted_) return;
    piersWanted_ = want;
    if (!want) {
        unloadPiers();
        return;
    }
    for (int i = 0; i < kPiers; i++) {
        Pier& p = piers_[i];
        if (!p.collision.load(kPierSpecs[i].collision)) TT_LOG("boat: no pier collision %s", kPierSpecs[i].collision);
        p.ticket = AssetLoader::requestModel(kPierSpecs[i].model);
        p.pitch = 1e9f; // placed on the next update
    }
    pierSolids_.floor = &BoatActor::pierFloor;
    pierSolids_.push = &BoatActor::pierPush;
    pierSolids_.carry = nullptr;
    pierSolids_.user = this;
    piersReady_ = false;
}

void BoatActor::unloadPiers() {
    bool synced = false;
    for (Pier& p : piers_) {
        if (p.ticket != kNoTicket) AssetLoader::cancel(p.ticket);
        p.ticket = kNoTicket;
        if (p.model.loaded()) {
            if (!synced) ::tt::gpu::frameSync(), synced = true;
            p.model.unload();
        }
        p.collision.unload();
        p.numWorld = 0;
    }
    piersReady_ = false;
}

void BoatActor::placePier(int i, float pitch) {
    Pier& p = piers_[i];
    if (pitch == p.pitch) return;
    p.pitch = pitch;
    const PierSpec& s = kPierSpecs[i];
    Mtx_Identity(&p.matrix);
    Mtx_Translate(&p.matrix, s.pivot[0], s.pivot[1], s.pivot[2], true);
    Mtx_RotateZ(&p.matrix, s.heading * kDegToRad, true);
    Mtx_RotateX(&p.matrix, pitch * kDegToRad, true);
    // Full HPR on the collision too: a pitching ramp needs its real slope (boat.md).
    p.numWorld = 0;
    const CollTri* tris = p.collision.loaded() ? p.collision.tris() : nullptr;
    const int n = tris ? static_cast<int>(p.collision.numTris()) : 0;
    for (int k = 0; k < n && p.numWorld < 16; k++) {
        CollTri& w = p.world[p.numWorld++];
        transformPoint(p.matrix, tris[k].v0, w.v0);
        transformPoint(p.matrix, tris[k].v1, w.v1);
        transformPoint(p.matrix, tris[k].v2, w.v2);
        float e1[3], e2[3];
        for (int j = 0; j < 3; j++) e1[j] = w.v1[j] - w.v0[j], e2[j] = w.v2[j] - w.v0[j];
        w.n[0] = e1[1] * e2[2] - e1[2] * e2[1];
        w.n[1] = e1[2] * e2[0] - e1[0] * e2[2];
        w.n[2] = e1[0] * e2[1] - e1[1] * e2[0];
        const float len = sqrtf(w.n[0] * w.n[0] + w.n[1] * w.n[1] + w.n[2] * w.n[2]);
        if (len > 1e-9f) for (float& c : w.n) c /= len;
        w.d = w.n[0] * w.v0[0] + w.n[1] * w.v0[1] + w.n[2] * w.v0[2];
        w.flags = tris[k].flags;
    }
}

// DistributedBoat.setupTracks (ParallelEndTogether), seeked by the synced state clock.
void BoatActor::updatePiers(u32 nowMs) {
    if (!piersWanted_) return;
    bool ready = true;
    for (Pier& p : piers_) {
        if (p.ticket == kNoTicket) continue;
        const LoadStatus st = AssetLoader::poll(p.ticket);
        if (st == LoadStatus::Ready) {
            AssetLoader::takeModel(p.ticket, p.model);
            p.ticket = kNoTicket;
        } else if (st != LoadStatus::Pending) {
            TT_LOG("boat: no pier model (bake_street.py --zone 1000 --separate-boat)");
            AssetLoader::cancel(p.ticket);
            p.ticket = kNoTicket;
        } else {
            ready = false;
        }
    }
    float east = kPierRaised, west = kPierRaised; // no boat: the model's own pose
    if (active()) {
        const s32 ms = static_cast<s32>(nowMs - stateStartMs_);
        const float t = ms > 0 ? ms / 1000.0f : 0.0f;
        switch (state_) {
        case net::BoatState::DockedEast: east = kPierFlat; break;
        case net::BoatState::DockedWest: west = kPierFlat; break;
        case net::BoatState::SailingWest:
            east = pierLerp(t, 0.0f, kPierFlat, kPierRaised);
            west = pierLerp(t, kBoatEWDuration - kPierTime, kPierRaised, kPierFlat);
            break;
        case net::BoatState::SailingEast:
            west = pierLerp(t, 0.0f, kPierFlat, kPierRaised);
            east = pierLerp(t, kBoatWEDuration - kPierTime, kPierRaised, kPierFlat);
            break;
        default: break;
        }
    }
    placePier(0, east);
    placePier(1, west);
    piersReady_ = ready;
}

bool BoatActor::pierFloor(void* self, float x, float y, float z, float* top) {
    const BoatActor& b = *static_cast<const BoatActor*>(self);
    bool found = false;
    for (const Pier& p : b.piers_) {
        for (int k = 0; k < p.numWorld; k++) {
            const CollTri& t = p.world[k];
            if (!(t.flags & kCollideFloor) || t.n[2] < 0.1f) continue;
            // Inside the XY projection (either winding)?
            auto side = [&](const float* a, const float* c) {
                return (c[0] - a[0]) * (y - a[1]) - (c[1] - a[1]) * (x - a[0]);
            };
            const float s0 = side(t.v0, t.v1), s1 = side(t.v1, t.v2), s2 = side(t.v2, t.v0);
            if ((s0 < 0 || s1 < 0 || s2 < 0) && (s0 > 0 || s1 > 0 || s2 > 0)) continue;
            const float h = (t.d - t.n[0] * x - t.n[1] * y) / t.n[2];
            if (h > z + kFloorReach || h < z - 20.0f) continue;
            if (!found || h > *top) *top = h, found = true;
        }
    }
    return found;
}

void BoatActor::pierPush(void* self, float* x, float* y, float z, float radius) {
    const BoatActor& b = *static_cast<const BoatActor*>(self);
    for (const Pier& p : b.piers_) {
        for (int k = 0; k < p.numWorld; k++) {
            const CollTri& t = p.world[k];
            if (!(t.flags & kCollideWall)) continue;
            const float c[3] = {*x, *y, z + radius};
            float q[3];
            closestOnTri(c, t.v0, t.v1, t.v2, q);
            float dx = c[0] - q[0], dy = c[1] - q[1];
            const float dz = c[2] - q[2];
            const float d2 = dx * dx + dy * dy + dz * dz;
            if (d2 >= radius * radius) continue;
            float l = sqrtf(dx * dx + dy * dy);
            if (l < 1e-4f) { // centre on the wall: out along its normal
                dx = t.n[0], dy = t.n[1];
                l = sqrtf(dx * dx + dy * dy);
                if (l < 1e-4f) continue;
            }
            const float push = radius - sqrtf(d2);
            *x += dx / l * push;
            *y += dy / l * push;
        }
    }
}

void BoatActor::update(u32 nowMs) {
    // The model loads on the background loader; swap it in when it is ready.
    if (wantModel_ && modelTicket_ == kNoTicket) modelTicket_ = AssetLoader::requestModel(kModelPath);
    if (modelTicket_ != kNoTicket) {
        const LoadStatus st = AssetLoader::poll(modelTicket_);
        if (st == LoadStatus::Ready) {
            AssetLoader::takeModel(modelTicket_, model_);
            modelTicket_ = kNoTicket;
            wantModel_ = false;
            TT_LOG("boat: model ready (%u tris)", unsigned(model_.numTriangles()));
        } else if (st != LoadStatus::Pending) {
            TT_LOG("boat: no model %s (bake tools/pipeline/bake_boat.py)", kModelPath);
            AssetLoader::cancel(modelTicket_);
            modelTicket_ = kNoTicket;
            wantModel_ = false;
        }
    }
    if (active()) pose(nowMs);
    updatePiers(nowMs);
    if (!active()) return;
    Mtx_Identity(&matrix_);
    Mtx_Translate(&matrix_, x_, y_, z_, true);
    Mtx_RotateZ(&matrix_, h_ * kDegToRad, true);
    const s32 ms = static_cast<s32>(nowMs - stateStartMs_);
    sailT_ = ms > 0 ? ms / 1000.0f : 0.0f;
    updateSounds();
}

// Positional one-shot: gain falls off with distance from the camera, pan follows its right vector.
void BoatActor::playAt(int sfx, float x, float y) {
    if (sfx_[sfx] == audio::kInvalidSfx) return;
    const float dx = x - listener_[0], dy = y - listener_[1];
    const float d = sqrtf(dx * dx + dy * dy);
    if (d >= kFar) return;
    const float gain = d <= kNear ? 1.0f : kNear / d;
    float fx = listener_[2] - listener_[0], fy = listener_[3] - listener_[1];
    const float fl = sqrtf(fx * fx + fy * fy);
    float pan = 0.0f;
    if (fl > 0.01f && d > 1.0f) {
        fx /= fl, fy /= fl;
        pan = (dx * fy - dy * fx) / d * 0.8f; // right = (fy, -fx)
    }
    audio::playSfx(sfx_[sfx], gain, pan);
}

// DistributedBoat tracks: bell at the start of a sail, foghorn end-aligned, the dock creak of
// the pier tilting away at t = 0 (departure side) and of the other pier rising at the end.
void BoatActor::updateSounds() {
    if (sfxLoaded_ < kSfxCount && audio::available()) { // one file per frame: no hitch
        sfx_[sfxLoaded_] = audio::loadSfx(kSfxPaths[sfxLoaded_]);
        if (sfx_[sfxLoaded_] == audio::kInvalidSfx) TT_LOG("boat: no sound %s", kSfxPaths[sfxLoaded_]);
        sfxLoaded_++;
    }
    setRider(rider_); // restarts the loop if its voice was stolen
    const bool west = state_ == net::BoatState::SailingWest;
    if (!west && state_ != net::BoatState::SailingEast) return;
    const float dur = west ? kBoatEWDuration : kBoatWEDuration;
    // Pier docks = ends of the paths: east = start of dd-e-w, west = its end.
    const float* eastDock = kBoatEW[0];
    const float* westDock = kBoatEW[kBoatEWCount - 1];
    const float* downDock = west ? eastDock : westDock;
    const float* upDock = west ? westDock : eastDock;
    struct Ev {
        float t;
        int sfx;
        float x, y;
    };
    const Ev events[4] = {{0.0f, kBell, x_, y_},
                          {0.0f, kCreak, downDock[0], downDock[1]},
                          {dur - kPierTime, kCreak, upDock[0], upDock[1]},
                          {dur - kFoghornSecs, kFoghorn, x_, y_}};
    for (int i = 0; i < 4; i++) {
        if ((fired_ & (1 << i)) || sailT_ < events[i].t) continue;
        fired_ |= static_cast<u8>(1 << i);
        if (sailT_ - events[i].t < kLateSecs) playAt(events[i].sfx, events[i].x, events[i].y);
    }
}

void BoatActor::setRider(bool onDeck) {
    rider_ = onDeck;
    if (onDeck && active_ && sfx_[kLap] != audio::kInvalidSfx) {
        if (!audio::voicePlaying(lapVoice_)) lapVoice_ = audio::playSfx(sfx_[kLap], 1.0f, 0.0f, true);
    } else if (lapVoice_ != audio::kNoVoice) {
        audio::stopVoice(lapVoice_);
        lapVoice_ = audio::kNoVoice;
    }
}

void BoatActor::stopSounds() {
    if (lapVoice_ != audio::kNoVoice) audio::stopVoice(lapVoice_);
    lapVoice_ = audio::kNoVoice;
    for (audio::SfxId& id : sfx_) {
        if (id != audio::kInvalidSfx) audio::freeSfx(id);
        id = audio::kInvalidSfx;
    }
    sfxLoaded_ = 0;
}

void BoatActor::draw(render::Renderer& renderer) {
    if (piersWanted_)
        for (Pier& p : piers_)
            if (p.model.loaded() && p.pitch < 1e8f) renderer.drawModel(p.model, p.matrix);
    if (!active() || !model_.loaded()) return;
    renderer.drawModel(model_, matrix_);
}

void BoatActor::shutdown() {
    if (modelTicket_ != kNoTicket) AssetLoader::cancel(modelTicket_);
    modelTicket_ = kNoTicket;
    wantModel_ = false;
    stopSounds();
    rider_ = false;
    if (model_.loaded()) {
        ::tt::gpu::frameSync();
        model_.unload();
    }
    deck_.unload();
    active_ = false;
    state_ = net::BoatState::Off;
}

} // namespace tt::world
