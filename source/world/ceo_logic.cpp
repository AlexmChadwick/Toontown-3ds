#include "world/ceo_logic.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace tt::world {

namespace {

constexpr float kDegreesToRadians = 0.01745329252f;

} // namespace

Vec3 ceoRotate(Vec3 point, float heading) {
    const float radians = heading * kDegreesToRadians;
    const float sine = std::sin(radians);
    const float cosine = std::cos(radians);
    return {
        point.x * cosine - point.y * sine,
        point.x * sine + point.y * cosine,
        point.z
    };
}

Vec3 ceoSeat(const CeoLayout& layout, int table, int chair) {
    const CeoPose& root = layout.tables[table];
    const Vec3 local = ceoRotate(layout.chairs[chair].position, root.heading);
    return {
        root.position.x + local.x,
        root.position.y + local.y,
        root.position.z + local.z
    };
}

Vec3 ceoGolfPosition(int index) {
    // Upstream DistributedGolfSpot.positions: X = -45 + 30 * index, Y = 100, Z = .25.
    // GolfGlobals.GOLF_BALL_RADIUS is .25 feet.
    return {-45.0f + 30.0f * index, 100.0f, 0.25f};
}

Vec3 ceoBallPosition(Vec3 origin, uint8_t power, float heading, float seconds) {
    // Upstream DistributedGolfSpot.startBallPlayback:
    // Power fraction maps to flight duration (2.0s down to 1.5s) and distance (100 to 300 feet).
    const float fraction = std::clamp(power / 100.0f, 0.0f, 1.0f);
    const float duration = 2.0f - 0.5f * fraction;
    const float distance = 100.0f + 200.0f * fraction;

    // Standard gravity arc: 0.5 * g * t * (T - t) where g = 32.174 ft/s^2, 0.5 * g = 16.087.
    const Vec3 offset = ceoRotate(
        {
            0.0f,
            distance / duration * seconds,
            16.087f * seconds * (duration - seconds)
        },
        heading
    );

    return {
        origin.x + offset.x,
        origin.y + offset.y,
        origin.z + offset.z
    };
}

float ceoSpeedDamage(const net::BossState& state, uint32_t nowMs) {
    // Speed damage recovers over time at speedRecover rate (units per minute).
    const float elapsedSeconds = std::max(0.0f, static_cast<int32_t>(nowMs - state.speedMs) / 1000.0f);
    const float recovered = state.speedRecover * elapsedSeconds / 60.0f;
    return std::clamp(state.speedDamage - recovered, 0.0f, kCeoMaxSpeedDamage);
}

float ceoRollSpeed(const net::BossState& state, uint32_t nowMs) {
    // Upstream ToontownGlobals: BossbotRollSpeedMax = 22, Min = 7.5.
    // Rolling speed slows down as speed damage increases.
    const float damageFraction = ceoSpeedDamage(state, nowMs) / kCeoMaxSpeedDamage;
    return kCeoRollSpeedMax - (kCeoRollSpeedMax - kCeoRollSpeedMin) * damageFraction;
}

void ceoIntro(float t, CeoIntroShot* out) {
    // BossbotElevCamPosHpr, then the close-up 20 forward / 2 down with pitch 5; loseCogSuits at
    // the resistance Toon's spot + (0, -5, 4) h 180 for 1 s + Wait(2); back to the close-up with
    // FightWaiter, Wait(1), LerpHprInterval(2) to h -15; the walk-off; Wait(3).
    const Vec3 elevator{0, -100.544f, 7.18258f}, closeUp{0, -80.544f, 5.18258f};
    if (t < 3) {
        const float u = t / 3;
        bossCameraAim({0, elevator.y + 20 * u, elevator.z - 2 * u}, 0, 5 * u, &out->eye, &out->target);
    } else if (t < 6 || (t >= 9 && t < 10)) {
        bossCameraAim(closeUp, 0, 5, &out->eye, &out->target);
    } else if (t < 9) {
        bossCameraAim({0, -69, 4}, 180, 0, &out->eye, &out->target);
    } else {
        bossCameraAim(closeUp, -15 * std::clamp((t - 10) / 2, 0.0f, 1.0f), 5, &out->eye, &out->target);
    }
    out->resistance = {40 * std::clamp((t - 13) / 2.5f, 0.0f, 1.0f), -64, 0};
    out->resistanceHeading = 180 + 90 * std::clamp(t - 12, 0.0f, 1.0f);
    out->resistanceVisible = t < kCeoIntroSeconds;
    out->resistanceWalking = t >= 12 && t < 15.5f;
    out->chat = nullptr, out->chatAge = 0;
    if (t < 3) out->chat = "You toons will need different disguises.", out->chatAge = t;
    else if (t < 6) out->chat = "First take off your cog suits...", out->chatAge = t - 3;
    else if (t >= 9) out->chat = "and then fight these waiters.", out->chatAge = t - 9;
}

void ceoElevator(float t, bool riding, CeoElevatorShot* out) {
    bossCameraAim({-0.001f, -101.487f, 6.983f + (riding ? bossElevatorBob(t) : 0)}, 0, 0, &out->eye, &out->target);
    out->doorOpen = riding ? std::clamp((t - 8) / 4, 0.0f, 1.0f) : 0;
}

float ceoBarrierDelay(const char* phase) {
    if (!std::strcmp(phase, "WaitForToons")) {
        return 0.5f;
    }
    if (!std::strcmp(phase, "Elevator")) {
        return 12.0f; // two 4 s rides + ELEVATOR_BB openTime 4
    }
    if (!std::strcmp(phase, "Introduction")) {
        return kCeoIntroSeconds;
    }
    if (!std::strcmp(phase, "PrepareBattleTwo")) {
        return 15.0f;
    }
    if (!std::strcmp(phase, "BattleTwo")) {
        return 300.5f;
    }
    if (!std::strcmp(phase, "PrepareBattleThree")) {
        return 20.0f;
    }
    if (!std::strcmp(phase, "PrepareBattleFour")) {
        return 15.0f;
    }
    if (!std::strcmp(phase, "Victory")) {
        return 20.0f;
    }
    if (!std::strcmp(phase, "Reward")) {
        return 6.0f;
    }
    return -1.0f;
}

const char* ceoClip(const net::BossState& state, uint32_t nowMs) {
    if (!std::strcmp(state.phase, "Victory")) {
        return "Ff_speech";
    }
    if (state.attack == 16) {
        return "Fb_neutral";
    }
    if (state.attack == 17 || state.attack == 18) {
        return "golf_swing";
    }
    return vpClip(state, nowMs);
}

} // namespace tt::world
