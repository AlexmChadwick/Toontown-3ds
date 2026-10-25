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

float ceoBarrierDelay(const char* phase) {
    if (!std::strcmp(phase, "WaitForToons")) {
        return 0.5f;
    }
    if (!std::strcmp(phase, "Elevator")) {
        return 6.0f;
    }
    if (!std::strcmp(phase, "Introduction")) {
        return 20.0f;
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
