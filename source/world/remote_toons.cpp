#include "world/remote_toons.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "render/ui.h"
#include "ui/projection.h"
#include "ui/speedchat_data.h"

namespace tt::world {

namespace {

constexpr float kDegToRad = 3.14159265f / 180.0f;
// Exponential smoothing time constant toward the latest sample. Senders update at
// 5 Hz, so ~0.1 s keeps motion continuous without lagging far behind.
constexpr float kSmoothTau = 0.1f;
// Snap instead of gliding when a toon teleports or we missed many samples.
constexpr float kSnapDistance = 30.0f;
constexpr float kSpeedTau = 0.25f; // smoothing for the derived speed (avoids walk/run flicker)
constexpr float kNametagHeight = 4.2f;
constexpr float kMaxBalloonDistSq = 90.0f * 90.0f;

// DNA netstring byte offsets (ToonDNA.makeNetString).
constexpr int kDnaArmColor = 11, kDnaGloveColor = 12, kDnaLegColor = 13, kDnaHeadColor = 14;

float wrapDeg(float d) {
    while (d > 180.0f) d -= 360.0f;
    while (d < -180.0f) d += 360.0f;
    return d;
}

} // namespace

ToonAnim animFromState(const char* state, bool* locomotion) {
    *locomotion = !strcmp(state, "run") || !strcmp(state, "walk") || !strcmp(state, "Happy");
    if (*locomotion) return kAnimNeutral;
    if (!strncmp(state, "jump", 4)) return kAnimJumpIdle;
    return kAnimNeutral; // neutral, Sad, Sleep, emotes, ... (not baked yet)
}

RemoteToons::Remote* RemoteToons::find(u32 doId) {
    for (Remote& r : toons_) {
        if (r.active && r.doId == doId) return &r;
    }
    return nullptr;
}

void RemoteToons::applyDna(Remote& r, const net::RemoteToon& t) {
    // Interim colouring until full DNA decoding (species/clothes) lands: body colours
    // index ToonDNA.allColorsList; clothing keeps its texture colours.
    r.tints.setAll(1.0f, 1.0f, 1.0f);
    if (!t.hasDna) return;
    auto set = [&](render::TintSlot slot, int index) {
        if (index < 0 || index >= ui::kNumToonColors) return;
        for (int i = 0; i < 3; i++) r.tints.rgba[slot][i] = ui::kToonColors[index][i];
    };
    set(render::kTintArm, t.dna[kDnaArmColor]);
    set(render::kTintGlove, t.dna[kDnaGloveColor]);
    set(render::kTintLeg, t.dna[kDnaLegColor]);
    set(render::kTintHead, t.dna[kDnaHeadColor]);
}

void RemoteToons::handleEvent(const net::Event& ev, const net::Client& client) {
    using net::EventType;
    if (ev.type == EventType::LeftZone) {
        clear();
        return;
    }
    const net::RemoteToon* t = client.findToon(ev.doId);
    Remote* r = find(ev.doId);
    switch (ev.type) {
    case EventType::Spawn: {
        if (!t) return;
        if (!r) {
            for (Remote& slot : toons_) {
                if (!slot.active) {
                    r = &slot;
                    break;
                }
            }
            if (!r) return;
            *r = Remote{};
            r->active = true;
            r->doId = ev.doId;
            r->avatar.setParts(parts_);
            count_++;
        }
        r->x = r->tx = t->x;
        r->y = r->ty = t->y;
        r->z = r->tz = t->z;
        r->h = r->th = t->h;
        snprintf(r->name, sizeof(r->name), "%s", t->name);
        applyDna(*r, *t);
        r->stateAnim = animFromState(t->animState, &r->locomotion);
        r->avatar.play(r->stateAnim, 1.0f);
        break;
    }
    case EventType::Despawn:
        if (r) {
            r->active = false;
            count_--;
        }
        break;
    case EventType::Pose:
        if (r && t) {
            r->tx = t->x;
            r->ty = t->y;
            r->tz = t->z;
            r->th = t->h;
        }
        break;
    case EventType::Anim:
        if (r && t) r->stateAnim = animFromState(t->animState, &r->locomotion);
        break;
    case EventType::Name:
        if (r && t) snprintf(r->name, sizeof(r->name), "%s", t->name);
        break;
    case EventType::Dna:
        if (r && t) applyDna(*r, *t);
        break;
    case EventType::Chat:
        if (r && t) {
            r->phrase = t->scIndex >= 0 ? sc::phraseText(static_cast<uint16_t>(t->scIndex)) : t->chat;
            r->phraseAge = 0.0f;
        }
        break;
    default:
        break;
    }
}

void RemoteToons::clear() {
    for (Remote& r : toons_) r.active = false;
    count_ = 0;
}

void RemoteToons::update(float dt) {
    const float k = 1.0f - expf(-dt / kSmoothTau);
    for (Remote& r : toons_) {
        if (!r.active) continue;
        const float dx = r.tx - r.x, dy = r.ty - r.y, dz = r.tz - r.z;
        if (dx * dx + dy * dy + dz * dz > kSnapDistance * kSnapDistance) {
            r.x = r.tx;
            r.y = r.ty;
            r.z = r.tz;
        } else {
            r.x += dx * k;
            r.y += dy * k;
            r.z += dz * k;
        }
        const float px = r.x, py = r.y, ph = r.h;
        r.h = wrapDeg(r.h + wrapDeg(r.th - r.h) * k);
        if (dt > 0.0f) {
            // Forward speed along the heading, for Toon.setSpeed-style clip choice.
            const float hr = r.h * kDegToRad;
            const float fwd = ((r.x - px) * -sinf(hr) + (r.y - py) * cosf(hr)) / dt;
            const float sk = 1.0f - expf(-dt / kSpeedTau);
            r.speed += (fwd - r.speed) * sk;
            r.turnRate += (wrapDeg(r.h - ph) / dt - r.turnRate) * sk;
        }
        if (r.locomotion) {
            // OTPGlobals RunCutOff 8 / WalkCutOff 0.5, reverse = walk at -1.
            if (r.speed >= 8.0f) {
                r.avatar.play(kAnimRun, 1.0f);
            } else if (r.speed > 0.5f) {
                r.avatar.play(kAnimWalk, 1.0f);
            } else if (r.speed < -0.5f) {
                r.avatar.play(kAnimWalk, -1.0f);
            } else if (fabsf(r.turnRate) > 5.0f) {
                r.avatar.play(kAnimWalk, 1.0f);
            } else {
                r.avatar.play(kAnimNeutral, 1.0f);
            }
        } else {
            r.avatar.play(r.stateAnim, 1.0f);
        }
        r.avatar.update(dt);
        r.phraseAge += dt;
    }
}

void RemoteToons::draw(render::Renderer& renderer, float cx, float cy) {
    // Nearest kMax: partial selection over a small array is cheaper than sorting.
    Remote* order[net::kMaxRemoteToons];
    int n = 0;
    for (Remote& r : toons_) {
        if (!r.active) continue;
        r.distSq = (r.x - cx) * (r.x - cx) + (r.y - cy) * (r.y - cy);
        order[n++] = &r;
    }
    const int shown = n < kMax ? n : kMax;
    for (int i = 0; i < shown; i++) {
        int best = i;
        for (int j = i + 1; j < n; j++) {
            if (order[j]->distSq < order[best]->distSq) best = j;
        }
        Remote* tmp = order[i];
        order[i] = order[best];
        order[best] = tmp;

        Remote& r = *order[i];
        C3D_Mtx m;
        Mtx_Identity(&m);
        Mtx_Translate(&m, r.x, r.y, r.z, true);
        Mtx_RotateZ(&m, r.h * kDegToRad, true);
        r.avatar.draw(renderer, m, &r.tints);
    }
}

int RemoteToons::mapPositions(float* xy, int maxOut) const {
    int n = 0;
    for (const Remote& r : toons_) {
        if (!r.active || n >= maxOut) continue;
        xy[2 * n] = r.x;
        xy[2 * n + 1] = r.y;
        n++;
    }
    return n;
}

void RemoteToons::addBalloons(ui::Balloons& balloons, const C3D_Mtx& proj, const C3D_Mtx& view) const {
    for (const Remote& r : toons_) {
        if (!r.active || r.distSq > kMaxBalloonDistSq) continue;
        float sx, sy, depth;
        if (ui::projectToScreen(proj, view, r.x, r.y, r.z + kNametagHeight, &sx, &sy, &depth)) {
            balloons.addSpeaker(sx, sy, r.name, r.phrase, r.phraseAge, depth);
        }
    }
}

} // namespace tt::world
