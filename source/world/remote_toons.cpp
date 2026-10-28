#include "world/remote_toons.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "ui/projection.h"
#include "ui/speedchat_data.h"
#include "net/net_platform.h"

namespace tt::world {

namespace {

constexpr float kDegToRad = 3.14159265f / 180.0f;
// Exponential smoothing time constant toward the latest sample. Senders update at
// 5 Hz. A steadily moving toon trails its latest sample by speed * tau, so tau is the
// extra on-screen latency: 0.06 s (was 0.1 s) keeps the 200 ms sample steps blended
// while holding that lag inside the ~50 ms-class budget for cross-play.
constexpr float kSmoothTau = 0.06f;
// Snap instead of gliding when a toon teleports or we missed many samples.
constexpr float kSnapDistance = 30.0f;
constexpr float kSpeedTau = 0.25f; // smoothing for the derived speed (avoids walk/run flicker)
constexpr float kLowLodDistSq = 45.0f * 45.0f;
constexpr float kHighLodDistSq = 35.0f * 35.0f;
constexpr float kMaxBalloonDistSq = 90.0f * 90.0f;


float wrapDeg(float d) {
    while (d > 180.0f) d -= 360.0f;
    while (d < -180.0f) d += 360.0f;
    return d;
}

} // namespace

ToonAnim animFromState(const char* state, bool* locomotion) {
    *locomotion = !strcmp(state, "run") || !strcmp(state, "walk") || !strcmp(state, "Happy");
    if (*locomotion) return kAnimNeutral;
    if (!strcmp(state,"Sad")) { *locomotion=true;return kAnimSadNeutral; }
    if (!strcmp(state,"Catching")) { *locomotion=true;return kAnimCatchNeutral; }
    if (!strcmp(state,"CatchEating")) { *locomotion=true;return kAnimCatchEatneutral; }
    if (!strcmp(state,"SitStart")) return kAnimSitStart;
    if (!strcmp(state,"Sit")) return kAnimSit;
    if (!strcmp(state,"swim") || !strcmp(state,"swimhold")) return kAnimSwim;
    if (!strcmp(state,"jumpLand")) return kAnimJumpLand;
    if (!strcmp(state,"jumpSquat")) return kAnimJumpSquat;
    if (!strncmp(state,"jump",4)) return kAnimJumpIdle;
    if (!strcmp(state,"TeleportOut")) return kAnimTeleport;
    if (!strcmp(state,"Died")) return kAnimLose; // Toon.enterDied plays 'lose'
    if (!strcmp(state,"TeleportIn")) return kAnimJump;
    return kAnimNeutral;
}

// Boat-local pose -> world through the boat's matrix (translate * rotateZ), like a node
// parented to it. Without an active boat the local pose is shown as is.
void RemoteToons::updateWorldPose(Remote& r) const {
    if (r.parent == net::kParentDonaldsBoat && boat_ && boat_->active()) {
        const C3D_Mtx& m = boat_->matrix();
        r.wx = m.r[0].x * r.x + m.r[0].y * r.y + m.r[0].z * r.z + m.r[0].w;
        r.wy = m.r[1].x * r.x + m.r[1].y * r.y + m.r[1].z * r.z + m.r[1].w;
        r.wz = m.r[2].x * r.x + m.r[2].y * r.y + m.r[2].z * r.z + m.r[2].w;
        r.wh = wrapDeg(r.h + boat_->heading());
    } else {
        r.wx = r.x, r.wy = r.y, r.wz = r.z, r.wh = r.h;
    }
}

RemoteToons::Remote* RemoteToons::find(u32 doId) {
    for (Remote& r : toons_) {
        if (r.active && r.doId == doId) return &r;
    }
    return nullptr;
}

void RemoteToons::applyDna(Remote& r, const net::RemoteToon& t) {
    ToonDna dna;
    if (t.hasDna && parseNetString(t.dna, net::kDnaSize, &dna)) {
        r.dna = dna;
        r.hasDna = true;
    }
    refreshLook(r);
}

void RemoteToons::refreshLook(Remote& r) {
    if (!cache_) return;
    if (!cache_->acquire(r.dna, r.lod, &r.look)) {
        // Part missing from the SD set: fall back to the default body.
        cache_->acquire(defaultToonDna(), r.lod, &r.look);
    }
    r.avatar.setLook(&r.look, &cache_->fallback());
}

void RemoteToons::despawn(Remote& r) {
    if (cache_) cache_->release(&r.look);
    r.avatar.cancel();
    r.avatar.setLook(nullptr);
    r.active = false;
    r.gamePlaced = false;
    count_--;
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
            r->active = true;
            r->doId = ev.doId;
            r->lod = ToonLod::High;
            r->hasDna = false;
            r->dna = defaultToonDna();
            r->phrase = nullptr;
            r->phraseAge = 99.0f;
            r->speed = r->turnRate = 0.0f;
            r->emoteSerial=0;
            r->catchFallAge=2;
            r->gamePlaced = false;
            count_++;
        }
        r->x = r->tx = t->x;
        r->y = r->ty = t->y;
        r->z = r->tz = t->z;
        r->h = r->th = t->h;
        r->parent = t->parentToken;
        updateWorldPose(*r);
        snprintf(r->name, sizeof(r->name), "%s", t->name);
        applyDna(*r, *t);
        r->stateAnim = animFromState(t->animState, &r->locomotion);
        r->avatar.play(r->stateAnim, 1.0f);
        r->stateRate=t->animRate;
        if(t->emoteSerial) { r->emoteSerial=t->emoteSerial;r->avatar.playEmote(t->emoteId,clipElapsed(net::plat::nowMs(),t->emoteMs)); }
        break;
    }
    case EventType::CatchHitBySuit:
        if (r) r->catchFallAge=clipElapsed(net::plat::nowMs(),static_cast<u32>(ev.arg));
        break;
    case EventType::Emote:
        if (r && t && t->emoteSerial!=r->emoteSerial) {
            r->emoteSerial=t->emoteSerial;
            r->avatar.playEmote(t->emoteId,clipElapsed(net::plat::nowMs(),t->emoteMs));
        }
        break;
    case EventType::Despawn:
        if (r) despawn(*r);
        break;
    case EventType::Pose:
        if (r && t && !r->gamePlaced) { // a game placing this toon owns its pose (placeExternal)
            r->tx = t->x;
            r->ty = t->y;
            r->tz = t->z;
            r->th = t->h;
            if (t->parentToken != r->parent) { // boarded/left the boat: the frame changed, so snap
                r->parent = t->parentToken;
                r->x = r->tx, r->y = r->ty, r->z = r->tz, r->h = r->th;
                r->speed = r->turnRate = 0.0f;
            }
        }
        break;
    case EventType::Anim:
        if (r && t) { r->stateAnim = animFromState(t->animState, &r->locomotion); r->stateRate=t->animRate; }
        break;
    case EventType::TunnelOut:
        if (r && t) {
            r->inTunnelTrack = true;
            r->isTunnelOut = true;
            r->tunnelAge = 0.0f;
            r->tunnelStartX = t->tunnelStartX;
            r->tunnelStartY = t->tunnelStartY;
            r->tunnelOriginX = t->tunnelX;
            r->tunnelOriginY = t->tunnelY;
            r->tunnelOriginZ = t->tunnelZ;
            r->tunnelOriginH = t->tunnelH;
            r->tunnelStraightDur = fabsf(t->tunnelStartY - (-6.0f)) / 20.0f;
            r->stateAnim = kAnimRun;
            r->stateRate = 1.0f;
            r->locomotion = false;
        }
        break;
    case EventType::TunnelIn:
        if (r && t) {
            r->inTunnelTrack = true;
            r->isTunnelOut = false;
            r->tunnelAge = 0.0f;
            r->tunnelEndX = t->tunnelEndX;
            r->tunnelOriginX = t->tunnelX;
            r->tunnelOriginY = t->tunnelY;
            r->tunnelOriginZ = t->tunnelZ;
            r->tunnelOriginH = t->tunnelH;
            r->tunnelStraightDur = 0.55f;
            r->stateAnim = kAnimRun;
            r->stateRate = 1.0f;
            r->locomotion = false;
        }
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

void RemoteToons::say(u32 doId, const char* phrase) {
    for (Remote& r : toons_) {
        if (r.active && r.doId == doId) {
            r.phrase = phrase;
            r.phraseAge = 0.0f;
            return;
        }
    }
}

void RemoteToons::clear() {
    for (Remote& r : toons_) {
        if (r.active) despawn(r);
    }
    count_ = 0;
}

void RemoteToons::placeExternal(u32 doId, float x, float y, float z, float h) {
    Remote* r = find(doId);
    if (!r) return;
    r->tx = x;
    r->ty = y;
    r->tz = z;
    r->th = h;
    r->parent = net::kParentRender;
    r->gamePlaced = true;
}

void RemoteToons::update(float dt) {
    const float k = 1.0f - expf(-dt / kSmoothTau);
    for (Remote& r : toons_) {
        if (!r.active) continue;
        const float px=r.x,py=r.y,ph=r.h;
        if (r.inTunnelTrack) {
            r.tunnelAge += dt;
            float lx = 0.0f, ly = 0.0f, lz = 0.025f, lh = 180.0f;
            if (r.isTunnelOut) {
                if (r.tunnelAge < r.tunnelStraightDur && r.tunnelStraightDur > 0.001f) {
                    const float u = r.tunnelAge / r.tunnelStraightDur;
                    lx = r.tunnelStartX;
                    ly = r.tunnelStartY + u * (-6.0f - r.tunnelStartY);
                    lh = 180.0f;
                } else if (r.tunnelAge < r.tunnelStraightDur + 2.0f) {
                    const float tp = r.tunnelAge - r.tunnelStraightDur;
                    const float up = tp / 2.0f;
                    const float ul = fminf(1.0f, tp / 1.3333f);
                    const float targetX = 9.0f + (r.tunnelStartX - 9.0f) * 0.4f;
                    const float curPivotX = (r.tunnelStartX + 14.0f) + ul * (targetX - r.tunnelStartX);
                    const float theta = -135.0f * up;
                    const float rad = theta * kDegToRad;
                    lx = -14.0f + curPivotX * cosf(rad);
                    ly = -6.0f + curPivotX * sinf(rad);
                    lh = 180.0f + theta;
                } else {
                    r.inTunnelTrack = false;
                }
            } else {
                if (r.tunnelAge < 0.8f) {
                    const float theta = -135.0f;
                    const float rad = theta * kDegToRad;
                    const float curPivotX = r.tunnelEndX + 14.0f;
                    lx = -14.0f + curPivotX * cosf(rad);
                    ly = -6.0f + curPivotX * sinf(rad);
                    lh = 180.0f + theta;
                } else if (r.tunnelAge < 2.8f) {
                    const float up = (r.tunnelAge - 0.8f) / 2.0f;
                    const float theta = -135.0f * (1.0f - up);
                    const float rad = theta * kDegToRad;
                    const float curPivotX = r.tunnelEndX + 14.0f;
                    lx = -14.0f + curPivotX * cosf(rad);
                    ly = -6.0f + curPivotX * sinf(rad);
                    lh = 180.0f + theta;
                } else if (r.tunnelAge < 2.8f + r.tunnelStraightDur) {
                    const float us = (r.tunnelAge - 2.8f) / r.tunnelStraightDur;
                    lx = r.tunnelEndX;
                    ly = -6.0f + us * 11.0f;
                    lh = 180.0f;
                } else {
                    r.inTunnelTrack = false;
                    r.locomotion = true;
                }
            }
            const float radH = r.tunnelOriginH * kDegToRad;
            r.x = r.wx = r.tunnelOriginX + lx * cosf(radH) - ly * sinf(radH);
            r.y = r.wy = r.tunnelOriginY + lx * sinf(radH) + ly * cosf(radH);
            r.z = r.wz = r.tunnelOriginZ + lz;
            r.h = r.wh = wrapDeg(r.tunnelOriginH + lh);
            r.speed = 20.0f;
        } else {
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
            r.h = wrapDeg(r.h + wrapDeg(r.th - r.h) * k);
        }
        if (dt > 0.0f) {
            // Forward speed along the heading, for Toon.setSpeed-style clip choice.
            const float hr = r.h * kDegToRad;
            const float fwd = ((r.x - px) * -sinf(hr) + (r.y - py) * cosf(hr)) / dt;
            const float sk = 1.0f - expf(-dt / kSpeedTau);
            r.speed += (fwd - r.speed) * sk;
            r.turnRate += (wrapDeg(r.h - ph) / dt - r.turnRate) * sk;
        }
        const ToonLod want=r.distSq>kLowLodDistSq?ToonLod::Low:r.distSq<kHighLodDistSq?ToonLod::High:r.lod;
        if (want!=r.lod) { r.lod=want;refreshLook(r); }
        if (r.catchFallAge<56/54.0f) {
            r.avatar.playAction(kAnimSlipBackward,ClipMode::Once,54/24.0f,12/24.0f,67/24.0f);
            r.avatar.seek(r.catchFallAge);
            r.catchFallAge+=dt;
        } else if (r.locomotion) {
            ToonAnim idle=kAnimNeutral,walk=kAnimWalk,run=kAnimRun;
            float rate=1;
            if (r.stateAnim==kAnimSadNeutral) { idle=kAnimSadNeutral;walk=run=kAnimSadWalk;rate=1.2f; }
            if (r.stateAnim==kAnimCatchNeutral) { idle=kAnimCatchNeutral;walk=run=kAnimCatchRun; }
            if (r.stateAnim==kAnimCatchEatneutral) { idle=kAnimCatchEatneutral;walk=run=kAnimCatchEatnrun; }
            if(r.speed>=8) r.avatar.play(run,rate);
            else if(r.speed>.5f || fabsf(r.turnRate)>5) r.avatar.play(walk,rate);
            else if(r.speed<-.5f) r.avatar.play(walk,-1);
            else r.avatar.play(idle,1);
        } else if (!r.avatar.emoteActive()) {
            const bool once=r.stateAnim==kAnimJumpSquat || r.stateAnim==kAnimJumpLand ||
                r.stateAnim==kAnimSitStart || r.stateAnim==kAnimTeleport || r.stateAnim==kAnimJump;
            r.avatar.playAction(r.stateAnim,once?ClipMode::Once:ClipMode::Loop,r.stateRate);
        }
        updateWorldPose(r);
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
        r.distSq = (r.wx - cx) * (r.wx - cx) + (r.wy - cy) * (r.wy - cy);
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
        Mtx_Translate(&m, r.wx, r.wy, r.wz, true);
        Mtx_RotateZ(&m, r.wh * kDegToRad, true);
        r.avatar.draw(renderer, m);
    }
}

int RemoteToons::mapPositions(float* xy, int maxOut) const {
    int n = 0;
    for (const Remote& r : toons_) {
        if (!r.active || n >= maxOut) continue;
        xy[2 * n] = r.wx;
        xy[2 * n + 1] = r.wy;
        n++;
    }
    return n;
}

int RemoteToons::shadowPositions(float* xyzs, int maxOut) const {
    int n = 0;
    for (const Remote& r : toons_) {
        if (!r.active || n >= maxOut) continue;
        xyzs[4 * n] = r.wx;
        xyzs[4 * n + 1] = r.wy;
        xyzs[4 * n + 2] = r.wz;
        xyzs[4 * n + 3] = r.look.valid() ? r.look.scale : 1.0f;
        n++;
    }
    return n;
}

void RemoteToons::addBalloons(ui::Balloons& balloons, const C3D_Mtx& proj, const C3D_Mtx& view) const {
    for (const Remote& r : toons_) {
        if (!r.active || r.distSq > kMaxBalloonDistSq) continue;
        float sx, sy, depth;
        if (ui::projectToScreen(proj, view, r.wx, r.wy, r.wz + r.look.nametagHeight, &sx, &sy, &depth)) {
            balloons.addSpeaker(sx, sy, r.name, r.phrase, r.phraseAge, depth);
        }
    }
}

const ToonAvatar* RemoteToons::avatar(u32 doId) const {
    for (const Remote& r : toons_)
        if (r.active && r.doId == doId) return &r.avatar;
    return nullptr;
}

} // namespace tt::world
