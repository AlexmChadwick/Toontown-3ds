// Place transitions: tunnels, building doors, teleports, and death.
#include "world/transition.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/log.h"
#include "world/collision.h"

namespace tt::world {

namespace {

constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
constexpr float kRadToDeg = 180.0f / 3.14159265358979323846f;
constexpr float kToonForwardSpeed = 20.0f; // 16 * 1.25 (ToonSpeedFactor)
constexpr float kPivotAngle = 135.0f;
constexpr float kPivotDur = 2.0f;
constexpr float kPivotLerpDur = 1.3333333f; // 2.0 * (90 / 135)
constexpr float kTunnelCenterOffset = 9.0f;
constexpr float kTunnelCenterInfluence = 0.6f;
constexpr float kPortalMaxScale = 1.17f;

inline float clamp01(float v) {
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

inline float easeInOut(float t) {
    t = clamp01(t);
    return t * t * (3.0f - 2.0f * t);
}

inline float easeIn(float t) {
    t = clamp01(t);
    return t * t;
}

inline float wrapDeg(float d) {
    while (d >= 360.0f) d -= 360.0f;
    while (d < 0.0f) d += 360.0f;
    return d;
}

inline float fitDestAngle2Src(float src, float dest) {
    float diff = dest - src;
    while (diff > 180.0f) diff -= 360.0f;
    while (diff < -180.0f) diff += 360.0f;
    return src + diff;
}

// Convert local (lx, ly, lz) in origin (ox, oy, oz, oh) space to world coordinates.
// Panda heading convention: H=0 is +Y, H=90 is -X, H=180 is -Y, H=270 is +X.
inline void localToWorld(float ox, float oy, float oz, float oh,
                         float lx, float ly, float lz, float lh,
                         float* wx, float* wy, float* wz, float* wh) {
    const float rad = oh * kDegToRad;
    const float c = cosf(rad);
    const float s = sinf(rad);
    *wx = ox + lx * c - ly * s;
    *wy = oy + lx * s + ly * c;
    *wz = oz + lz;
    *wh = wrapDeg(oh + lh);
}

// Convert world point (wx, wy, wz) to local coordinates in origin (ox, oy, oz, oh) space.
inline void worldToLocal(float ox, float oy, float oz, float oh,
                         float wx, float wy, float wz,
                         float* lx, float* ly, float* lz) {
    const float dx = wx - ox;
    const float dy = wy - oy;
    const float rad = oh * kDegToRad;
    const float c = cosf(rad);
    const float s = sinf(rad);
    *lx = dx * c + dy * s;
    *ly = -dx * s + dy * c;
    *lz = wz - oz;
}

} // namespace

void Transition::init() {
    portalModel_.load("romfs:/baked/portal.ttm");
    doorModel_.load("romfs:/baked/door_right.ttm");

    sfxTeleport_ = tt::audio::loadSfx("romfs:/baked/audio/teleport.tsnd");
    sfxDoorOpen_ = tt::audio::loadSfx("romfs:/baked/audio/door_open.tsnd");
    sfxDoorClose_ = tt::audio::loadSfx("romfs:/baked/audio/door_close.tsnd");
    sfxLose_ = tt::audio::loadSfx("romfs:/baked/audio/enc_lose.tsnd");
}

void Transition::shutdown() {
    portalModel_.unload();
    doorModel_.unload();
    reset();
}

void Transition::reset() {
    type_ = TransitionType::None;
    phase_ = TransitionPhase::Idle;
    age_ = 0.0f;
    fadeAlpha_ = 0.0f;
    fadeDir_ = 0;
    fromZone_ = 0;
    targetZone_ = 0;
    doorDoId_ = 0;
    toonScale_ = 1.0f;
    toonVisible_ = true;
    doorActive_ = false;
    portalActive_ = false;
    overrideCam_ = false;
}

void Transition::computeTunnelOrigin(const Exit& exit, const Place& place, const Player& player) {
    // The tunnel model's tunnel_origin (bake_zone.write_points "<tunnel>_origin"), which the
    // upstream tunnel tracks are relative to; the DNA root is only a fallback.
    char originName[48];
    snprintf(originName, sizeof(originName), "%s_origin", exit.name);
    const Place::Point* pt = place.point(originName);
    if (!pt) pt = place.point(exit.name);
    if (!pt) pt = place.pointWithPrefix("linktunnel_");
    if (pt) {
        tunnelOriginX_ = pt->x;
        tunnelOriginY_ = pt->y;
        tunnelOriginZ_ = pt->z;
        tunnelOriginH_ = pt->h;
    } else {
        // Fallback: place tunnel origin slightly in front of player
        tunnelOriginX_ = player.x();
        tunnelOriginY_ = player.y();
        tunnelOriginZ_ = player.z();
        tunnelOriginH_ = player.headingDeg();
    }
}

void Transition::computeDoorOrigin(const Exit& exit, const Place& place, const Player& player) {
    const Place::Point* pt = place.point(exit.name);
    if (!pt && place.kind() == PlaceKind::Interior) pt = place.point("door_origin");
    if (!pt && place.kind() == PlaceKind::Interior) pt = place.point("door_origin_0");
    if (pt) {
        doorOriginX_ = pt->x;
        doorOriginY_ = pt->y;
        doorOriginZ_ = pt->z;
        doorOriginH_ = pt->h;
    } else {
        // Fallback to player's current position and heading
        doorOriginX_ = player.x();
        doorOriginY_ = player.y();
        doorOriginZ_ = player.z();
        doorOriginH_ = player.headingDeg();
    }
}

void Transition::startTunnelOut(u32 targetZone, const Exit& exit, const Player& player, const Place& place, net::Client* net) {
    reset();
    type_ = TransitionType::Tunnel;
    phase_ = TransitionPhase::Departing;
    fromZone_ = place.zoneId();
    targetZone_ = targetZone;
    age_ = 0.0f;
    fadeAlpha_ = 0.0f;
    fadeDir_ = 0;

    computeTunnelOrigin(exit, place, player);

    // Compute toon start position in tunnel origin space
    float lz;
    worldToLocal(tunnelOriginX_, tunnelOriginY_, tunnelOriginZ_, tunnelOriginH_,
                 player.x(), player.y(), player.z(),
                 &tunnelStartX_, &tunnelStartY_, &lz);

    tunnelStartAvH_ = fitDestAngle2Src(wrapDeg(player.headingDeg() - tunnelOriginH_), 180.0f);
    tunnelStraightDur_ = fabsf(tunnelStartY_ - (-6.0f)) / kToonForwardSpeed;
    if (tunnelStraightDur_ < 0.1f) tunnelStraightDur_ = 0.1f;
    tunnelDuration_ = tunnelStraightDur_ + kPivotDur;

    TT_LOG("transition: tunnel out start -> zone %lu (start: %.2f, %.2f, dur: %.2f)",
           static_cast<unsigned long>(targetZone), tunnelStartX_, tunnelStartY_, tunnelDuration_);

    if (net) {
        net->sendTunnelOut(tunnelStartX_, tunnelStartY_, tunnelOriginX_, tunnelOriginY_, tunnelOriginZ_, tunnelOriginH_);
        net->sendAnimState("run", 1.0f);
    }
}

void Transition::startDoorOut(u32 targetZone, const Exit& exit, const Player& player, const Place& place, u32 doorDoId, net::Client* net) {
    reset();
    type_ = TransitionType::Door;
    phase_ = TransitionPhase::Departing;
    fromZone_ = place.zoneId();
    targetZone_ = targetZone;
    doorDoId_ = doorDoId;
    age_ = 0.0f;
    fadeAlpha_ = 0.0f;
    fadeDir_ = 0;
    doorActive_ = true;
    doorSwing_ = 0.0f;

    computeDoorOrigin(exit, place, player);

    float lz = 0.0f;
    worldToLocal(doorOriginX_, doorOriginY_, doorOriginZ_, doorOriginH_,
                 player.x(), player.y(), player.z(),
                 &doorStartX_, &doorStartY_, &lz);

    TT_LOG("transition: door out start -> zone %lu (doId %lu, start: %.2f, %.2f)",
           static_cast<unsigned long>(targetZone), static_cast<unsigned long>(doorDoId),
           doorStartX_, doorStartY_);

    if (net && doorDoId_ != 0) {
        net->sendDoorRequestEnter(doorDoId_);
    }
    if (net) {
        net->sendAnimState("walk", 1.0f);
    }
    tt::audio::playSfx(sfxDoorOpen_);
}

void Transition::startTeleportOut(u32 targetZone, const Player& player, net::Client* net) {
    reset();
    type_ = TransitionType::Teleport;
    phase_ = TransitionPhase::Departing;
    targetZone_ = targetZone;
    age_ = 0.0f;
    fadeAlpha_ = 0.0f;
    fadeDir_ = 0;
    toonScale_ = 1.0f;
    toonVisible_ = true;

    // Portal hole appears on the ground where the toon is standing
    portalX_ = player.x();
    portalY_ = player.y();
    portalZ_ = player.z();
    portalScale_ = 0.0f;
    portalActive_ = false;

    TT_LOG("transition: teleport out start -> zone %lu", static_cast<unsigned long>(targetZone));

    if (net) {
        net->sendAnimState("TeleportOut", 1.0f);
    }
}

void Transition::startDied(u32 safezoneId, const Player& player, net::Client* net) {
    (void)net;
    reset();
    type_ = TransitionType::Died;
    phase_ = TransitionPhase::Departing;
    targetZone_ = safezoneId;
    age_ = 0.0f;
    fadeAlpha_ = 0.0f;
    fadeDir_ = 0;
    toonScale_ = 1.0f;
    toonVisible_ = true;

    portalX_ = player.x();
    portalY_ = player.y();
    portalZ_ = player.z();

    TT_LOG("transition: died start -> safezone %lu", static_cast<unsigned long>(safezoneId));
    if (net) {
        net->sendAnimState("lose", 1.0f);
    }
}

void Transition::startTeleportIn(const Player& player, net::Client* net) {
    reset();
    type_ = TransitionType::Teleport;
    phase_ = TransitionPhase::Arriving;
    age_ = 0.0f;
    fadeAlpha_ = 0.0f;
    fadeDir_ = 0;
    portalX_ = player.x();
    portalY_ = player.y();
    portalZ_ = player.z();
    portalScale_ = 0.0f;
    portalActive_ = true;
    toonVisible_ = false;

    if (net) {
        net->sendAnimState("TeleportIn", 1.0f);
    }
}

void Transition::notifyLoaded(u32 loadedZone, Place& place, Player& player, net::Client* net) {
    (void)loadedZone;
    phase_ = TransitionPhase::Arriving;
    age_ = 0.0f;
    fadeAlpha_ = 1.0f;
    fadeDir_ = -1; // Fade in from black

    if (type_ == TransitionType::Tunnel) {
        // Look up destination tunnel origin matching fromZone
        char linkName[32];
        snprintf(linkName, sizeof(linkName), "%lu", static_cast<unsigned long>(fromZone_));
        // Prefer that tunnel's tunnel_origin ("<tunnel>_origin"), then its DNA root.
        const Place::Point* pt = nullptr;
        for (int pass = 0; pass < 2 && !pt; pass++) {
            for (int i = 0; i < place.numPoints(); i++) {
                const Place::Point& p = place.pointAt(i);
                const size_t len = strlen(p.name);
                const bool origin = len > 7 && !strcmp(p.name + len - 7, "_origin");
                if (!strncmp(p.name, "linktunnel_", 11) && strstr(p.name, linkName) && origin == (pass == 0)) {
                    pt = &p;
                    break;
                }
            }
        }
        if (!pt) pt = place.pointWithPrefix("linktunnel_");
        if (pt) {
            tunnelOriginX_ = pt->x;
            tunnelOriginY_ = pt->y;
            tunnelOriginZ_ = pt->z;
            tunnelOriginH_ = pt->h;
        } else {
            tunnelOriginX_ = player.x();
            tunnelOriginY_ = player.y();
            tunnelOriginZ_ = player.z();
            tunnelOriginH_ = player.headingDeg();
        }
        tunnelEndX_ = 0.0f; // Center exit
        tunnelStraightDur_ = 0.55f; // (5.0 - (-6.0)) / 20.0
        tunnelDuration_ = 2.8f + tunnelStraightDur_;

        // Camera placed inside tunnel entrance looking out
        float cx, cy, cz, ch;
        localToWorld(tunnelOriginX_, tunnelOriginY_, tunnelOriginZ_, tunnelOriginH_,
                     0.0f, 20.0f, 12.0f, 180.0f, &cx, &cy, &cz, &ch);
        camEye_ = FVec3_New(cx, cy, cz);
        camTarget_ = FVec3_New(tunnelOriginX_, tunnelOriginY_, tunnelOriginZ_ + 2.0f);
        overrideCam_ = true;

        if (net) {
            net->sendTunnelIn(tunnelEndX_, tunnelOriginX_, tunnelOriginY_, tunnelOriginZ_, tunnelOriginH_);
            net->sendAnimState("run", 1.0f);
        }
    } else if (type_ == TransitionType::Door) {
        // Look up interior/exterior door origin
        const Place::Point* pt = place.point("door_origin");
        if (!pt) pt = place.point("door_origin_0");
        if (pt) {
            doorOriginX_ = pt->x;
            doorOriginY_ = pt->y;
            doorOriginZ_ = pt->z;
            doorOriginH_ = pt->h;
        } else {
            doorOriginX_ = player.x();
            doorOriginY_ = player.y();
            doorOriginZ_ = player.z();
            doorOriginH_ = player.headingDeg();
        }
        doorActive_ = true;
        doorSwing_ = 100.0f; // Closes as toon walks out
        tt::audio::playSfx(sfxDoorClose_);

        if (net) {
            net->sendAnimState("walk", 1.0f);
        }
    } else if (type_ == TransitionType::Teleport || type_ == TransitionType::Died) {
        type_ = TransitionType::Teleport; // Died resumes as teleport-in
        portalX_ = player.x();
        portalY_ = player.y();
        portalZ_ = player.z();
        portalScale_ = 0.0f;
        portalActive_ = true;
        toonVisible_ = false; // Hidden until portal opens at t=0.3s

        if (net) {
            net->sendAnimState("TeleportIn", 1.0f);
        }
    }
}

void Transition::update(float dt, Player& player, Place& place, net::Client* net) {
    (void)place;
    (void)net;
    if (phase_ == TransitionPhase::Idle) return;

    age_ += dt;

    // Handle screen fade alpha
    constexpr float kFadeDur = 0.4f;
    if (fadeDir_ > 0) {
        fadeAlpha_ += dt / kFadeDur;
        if (fadeAlpha_ >= 1.0f) {
            fadeAlpha_ = 1.0f;
            phase_ = TransitionPhase::BlackFrame;
            fadeDir_ = 0;
            return;
        }
    } else if (fadeDir_ < 0) {
        fadeAlpha_ -= dt / kFadeDur;
        if (fadeAlpha_ <= 0.0f) {
            fadeAlpha_ = 0.0f;
            fadeDir_ = 0;
        }
    }

    if (phase_ == TransitionPhase::Departing) {
        switch (type_) {
        case TransitionType::Tunnel: {
            float lx = 0.0f, ly = 0.0f, lz = 0.025f, lh = 180.0f;
            if (age_ < tunnelStraightDur_) {
                const float u = age_ / tunnelStraightDur_;
                lx = tunnelStartX_;
                ly = tunnelStartY_ + u * (-6.0f - tunnelStartY_);
                const float hu = fminf(1.0f, age_ / (tunnelStraightDur_ * 0.8f));
                lh = tunnelStartAvH_ + hu * (180.0f - tunnelStartAvH_);

                // Camera lerps toward (0, 20, 12) looking at tunnel entrance
                float ecx, ecy, ecz, ech;
                localToWorld(tunnelOriginX_, tunnelOriginY_, tunnelOriginZ_, tunnelOriginH_,
                             0.0f, 20.0f, 12.0f, 180.0f, &ecx, &ecy, &ecz, &ech);
                camEye_ = FVec3_New(ecx, ecy, ecz);
                camTarget_ = FVec3_New(tunnelOriginX_, tunnelOriginY_, tunnelOriginZ_ + 2.0f);
                overrideCam_ = true;
            } else if (age_ < tunnelDuration_) {
                const float tp = age_ - tunnelStraightDur_;
                const float up = tp / kPivotDur;
                const float ul = fminf(1.0f, tp / kPivotLerpDur);
                const float targetX = kTunnelCenterOffset + (tunnelStartX_ - kTunnelCenterOffset) * (1.0f - kTunnelCenterInfluence);
                const float curPivotX = (tunnelStartX_ + 14.0f) + ul * (targetX - tunnelStartX_);
                const float theta = -kPivotAngle * up;
                const float rad = theta * kDegToRad;
                lx = -14.0f + curPivotX * cosf(rad);
                ly = -6.0f + curPivotX * sinf(rad);
                lh = 180.0f + theta;
            }

            float wx, wy, wz, wh;
            localToWorld(tunnelOriginX_, tunnelOriginY_, tunnelOriginZ_, tunnelOriginH_,
                         lx, ly, lz, lh, &wx, &wy, &wz, &wh);
            player.reset(wx, wy, wz, wh);
            if (overrideCam_) player.overrideCamera(camEye_, camTarget_);

            // Start fade out at duration - 0.5s
            if (age_ >= tunnelDuration_ - 0.5f && fadeDir_ == 0 && fadeAlpha_ < 1.0f) {
                fadeDir_ = 1;
            }
            break;
        }

        case TransitionType::Door: {
            // Door swing: opens between 0.4s and 1.0s
            if (age_ >= 0.4f && age_ < 1.0f) {
                doorSwing_ = easeInOut((age_ - 0.4f) / 0.6f) * 100.0f;
            } else if (age_ >= 1.0f) {
                doorSwing_ = (1.0f - easeInOut(fminf(1.0f, age_ - 1.0f))) * 100.0f;
            }

            // Toon walks forward in door space to (1.5, 2.0, 0.025) over 1.0s
            const float u = easeIn(fminf(1.0f, age_ / 1.0f));
            float lx = doorStartX_ + u * (1.5f - doorStartX_);
            float ly = doorStartY_ + u * (2.0f - doorStartY_);
            float lz = 0.025f;
            float lh = 0.0f; // facing into building

            float wx, wy, wz, wh;
            localToWorld(doorOriginX_, doorOriginY_, doorOriginZ_, doorOriginH_,
                         lx, ly, lz, lh, &wx, &wy, &wz, &wh);
            player.reset(wx, wy, wz, wh);

            // Camera follows behind toon at (0, -8, height)
            float cx, cy, cz, ch;
            localToWorld(doorOriginX_, doorOriginY_, doorOriginZ_, doorOriginH_,
                         0.0f, -8.0f, 4.0f, 0.0f, &cx, &cy, &cz, &ch);
            player.overrideCamera(FVec3_New(cx, cy, cz), FVec3_New(wx, wy, wz + 2.0f));

            // Start fade out at 0.5s
            if (age_ >= 0.5f && fadeDir_ == 0 && fadeAlpha_ < 1.0f) {
                fadeDir_ = 1;
            }
            break;
        }

        case TransitionType::Teleport: {
            if (age_ >= 0.5f && age_ < 0.5f + dt) {
                tt::audio::playSfx(sfxTeleport_);
            }
            if (age_ >= 1.708f) {
                portalActive_ = true;
                if (age_ < 2.0f) {
                    portalScale_ = easeInOut((age_ - 1.708f) / 0.292f) * kPortalMaxScale;
                } else if (age_ < 3.0f) {
                    portalScale_ = kPortalMaxScale;
                } else {
                    portalScale_ = (1.0f - easeInOut(fminf(1.0f, (age_ - 3.0f) / 0.4f))) * kPortalMaxScale;
                }
            }
            if (age_ >= 3.0f) {
                toonVisible_ = false;
                if (fadeDir_ == 0 && fadeAlpha_ < 1.0f) fadeDir_ = 1;
            }
            break;
        }

        case TransitionType::Died: {
            if (age_ >= 2.0f && age_ < 2.0f + dt) {
                tt::audio::playSfx(sfxLose_);
            }
            if (age_ >= 5.333f) {
                const float su = easeInOut(fminf(1.0f, (age_ - 5.333f) / 1.5f));
                toonScale_ = 1.0f - su * 0.99f;
            }
            if (age_ >= 6.433f && fadeDir_ == 0 && fadeAlpha_ < 1.0f) {
                fadeDir_ = 1;
            }
            break;
        }

        default:
            break;
        }
    } else if (phase_ == TransitionPhase::Arriving) {
        switch (type_) {
        case TransitionType::Tunnel: {
            float lx = 0.0f, ly = 0.0f, lz = 0.025f, lh = 180.0f;
            if (age_ < 0.8f) {
                const float curPivotX = tunnelEndX_ + 14.0f;
                const float theta = -kPivotAngle;
                const float rad = theta * kDegToRad;
                lx = -14.0f + curPivotX * cosf(rad);
                ly = -6.0f + curPivotX * sinf(rad);
                lh = 180.0f + theta;
            } else if (age_ < 2.8f) {
                const float up = (age_ - 0.8f) / kPivotDur;
                const float curPivotX = tunnelEndX_ + 14.0f;
                const float theta = -kPivotAngle * (1.0f - up);
                const float rad = theta * kDegToRad;
                lx = -14.0f + curPivotX * cosf(rad);
                ly = -6.0f + curPivotX * sinf(rad);
                lh = 180.0f + theta;
            } else if (age_ < tunnelDuration_) {
                const float us = (age_ - 2.8f) / tunnelStraightDur_;
                lx = tunnelEndX_;
                ly = -6.0f + us * 11.0f; // straight walk to Y = 5.0
                lh = 180.0f;
            } else {
                reset();
                return;
            }

            float wx, wy, wz, wh;
            localToWorld(tunnelOriginX_, tunnelOriginY_, tunnelOriginZ_, tunnelOriginH_,
                         lx, ly, lz, lh, &wx, &wy, &wz, &wh);
            player.reset(wx, wy, wz, wh);
            if (overrideCam_) player.overrideCamera(camEye_, camTarget_);
            break;
        }

        case TransitionType::Door: {
            // Door swings closed from 100 to 0 degrees over 1.0s
            if (age_ < 1.0f) {
                doorSwing_ = (1.0f - easeInOut(age_ / 1.0f)) * 100.0f;
                // Toon walks forward out of door from (-1.5, 0) to (-1.5, -6)
                const float u = easeInOut(age_ / 1.0f);
                float lx = -1.5f;
                float ly = -u * 6.0f;
                float lz = 0.025f;
                float lh = 179.0f;

                float wx, wy, wz, wh;
                localToWorld(doorOriginX_, doorOriginY_, doorOriginZ_, doorOriginH_,
                             lx, ly, lz, lh, &wx, &wy, &wz, &wh);
                player.reset(wx, wy, wz, wh);

                // Camera at (-1.5, 5.0, height) facing 180
                float cx, cy, cz, ch;
                localToWorld(doorOriginX_, doorOriginY_, doorOriginZ_, doorOriginH_,
                             -1.5f, 5.0f, 4.0f, 180.0f, &cx, &cy, &cz, &ch);
                player.overrideCamera(FVec3_New(cx, cy, cz), FVec3_New(wx, wy, wz + 2.0f));
            } else {
                reset();
                return;
            }
            break;
        }

        case TransitionType::Teleport: {
            portalActive_ = true;
            if (age_ < 0.3f) {
                portalScale_ = easeInOut(age_ / 0.3f) * kPortalMaxScale;
                toonVisible_ = false;
            } else if (age_ < 0.6f) {
                portalScale_ = kPortalMaxScale;
                toonVisible_ = true;
            } else if (age_ < 0.9f) {
                portalScale_ = (1.0f - easeInOut((age_ - 0.6f) / 0.3f)) * kPortalMaxScale;
                toonVisible_ = true;
            } else if (age_ >= 1.3f) {
                reset();
                return;
            }
            break;
        }

        default:
            reset();
            break;
        }
    }
}

void Transition::applyInput(circlePosition& pad, u32& held, u32& down) {
    if (phase_ == TransitionPhase::Idle) return;

    // Lock player controls during transitions
    pad.dx = 0;
    pad.dy = 0;
    held &= ~(KEY_A | KEY_B);
    down &= ~(KEY_A | KEY_B);
}

bool Transition::applyAnimation(ToonAvatar& toon, float dt) {
    (void)dt;
    if (phase_ == TransitionPhase::Idle) return false;

    if (type_ == TransitionType::Tunnel) {
        toon.play(kAnimRun, 1.0f);
        return true;
    }
    if (type_ == TransitionType::Door) {
        toon.play(kAnimWalk, 1.0f);
        return true;
    }
    if (type_ == TransitionType::Teleport) {
        if (phase_ == TransitionPhase::Departing) {
            toon.playAction(kAnimTeleport, ClipMode::Once);
            toon.seek(age_);
            return true;
        } else if (phase_ == TransitionPhase::Arriving) {
            if (age_ < 0.3f) {
                toon.hold(kAnimTeleport, 81.0f / 24.0f);
            } else {
                toon.playAction(kAnimJump, ClipMode::Once, 1.0f, 0.45f);
                toon.seek(age_ - 0.3f);
            }
            return true;
        }
    }
    if (type_ == TransitionType::Died) {
        toon.play(kAnimLose, 1.0f);
        return true;
    }
    return false;
}

bool Transition::isToonVisible() const {
    if (phase_ == TransitionPhase::Idle) return true;
    return toonVisible_;
}

const char* Transition::toonNetworkState() const {
    if (phase_ == TransitionPhase::Idle) return nullptr;
    if (type_ == TransitionType::Teleport) {
        if (phase_ == TransitionPhase::Departing) return "TeleportOut";
        if (phase_ == TransitionPhase::Arriving) return "TeleportIn";
    }
    if (type_ == TransitionType::Tunnel) return "run";
    if (type_ == TransitionType::Door) return "run"; // LocalAvatar sends "run" for all ground locomotion
    if (type_ == TransitionType::Died) return "lose";
    return nullptr;
}

void Transition::drawWorld(render::Renderer& renderer) {
    if (phase_ == TransitionPhase::Idle) return;

    if (portalActive_ && portalScale_ > 0.001f && portalModel_.loaded()) {
        C3D_Mtx m;
        Mtx_Identity(&m);
        Mtx_Translate(&m, portalX_, portalY_, portalZ_ + 0.035f, true);
        Mtx_Scale(&m, portalScale_, portalScale_, portalScale_);
        renderer.drawModel(portalModel_, m);
    }

    if (doorActive_ && doorModel_.loaded()) {
        C3D_Mtx m;
        Mtx_Identity(&m);
        // Position at door origin
        Mtx_Translate(&m, doorOriginX_, doorOriginY_, doorOriginZ_, true);
        Mtx_RotateZ(&m, doorOriginH_ * kDegToRad, true);
        Mtx_Scale(&m, 0.8f, 0.8f, 0.8f);
        // Hinge offset: right door leaf hinge is at (3.591, 0, 3.913)
        Mtx_Translate(&m, 3.591f, 0.0f, 3.913f, true);
        Mtx_RotateZ(&m, doorSwing_ * kDegToRad, true);
        renderer.drawModel(doorModel_, m);
    }
}

void Transition::drawFade(render::Renderer& renderer, bool stereo) {
    if (fadeAlpha_ <= 0.001f) return;

    const u8 a = static_cast<u8>(fminf(1.0f, fadeAlpha_) * 255.0f);
    const u32 black = C2D_Color32(0, 0, 0, a);
    const int eyes = stereo ? 2 : 1;
    for (int e = 0; e < eyes; e++) {
        C2D_SceneBegin(e == 0 ? renderer.topLeft() : renderer.topRight());
        C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
        C2D_DrawRectSolid(0, 0, 0.95f, 400, 240, black);
    }
    C3D_DepthTest(true, GPU_GEQUAL, GPU_WRITE_ALL);
}

} // namespace tt::world
