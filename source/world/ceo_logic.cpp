#include "world/ceo_logic.h"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace tt::world {
namespace { constexpr float kDegrees = .01745329252f; }
Vec3 ceoRotate(Vec3 point, float heading) {
    const float sine = std::sin(heading * kDegrees);
    const float cosine = std::cos(heading * kDegrees);
    return {point.x * cosine - point.y * sine, point.x * sine + point.y * cosine, point.z};
}
Vec3 ceoSeat(const CeoLayout& layout, int table, int chair) {
    const CeoPose& root = layout.tables[table];
    const Vec3 local = ceoRotate(layout.chairs[chair].position, root.heading);
    return {root.position.x + local.x, root.position.y + local.y, root.position.z + local.z};
}
Vec3 ceoGolfPosition(int index) {
    // DistributedGolfSpot.positions; GolfGlobals.GOLF_BALL_RADIUS is .25.
    return {-45.0f + 30 * index, 100, .25f};
}
Vec3 ceoBallPosition(Vec3 origin, uint8_t power, float heading, float seconds) {
    // DistributedGolfSpot.startBallPlayback: end distance 100..300, time 2..1.5.
    const float fraction = std::clamp(power / 100.0f, 0.0f, 1.0f);
    const float duration = 2 - .5f * fraction;
    const float distance = 100 + 200 * fraction;
    const Vec3 offset = ceoRotate({0, distance / duration * seconds,
        16.087f * seconds * (duration - seconds)}, heading);
    return {origin.x + offset.x, origin.y + offset.y, origin.z + offset.z};
}
float ceoSpeedDamage(const net::BossState& state, uint32_t nowMs) {
    const float elapsed = std::max(0.0f, static_cast<int32_t>(nowMs - state.speedMs) / 1000.0f);
    return std::clamp(state.speedDamage - state.speedRecover * elapsed / 60, 0.0f, kCeoMaxSpeedDamage);
}
float ceoRollSpeed(const net::BossState& state, uint32_t nowMs) {
    // ToontownGlobals.BossbotRollSpeedMax=22, Min=7.5.
    return 22 - (22 - 7.5f) * ceoSpeedDamage(state, nowMs) / kCeoMaxSpeedDamage;
}
float ceoBarrierDelay(const char* phase) {
    if (!std::strcmp(phase, "WaitForToons")) return .5f;
    if (!std::strcmp(phase, "Elevator")) return 6;
    if (!std::strcmp(phase, "Introduction")) return 20;
    if (!std::strcmp(phase, "PrepareBattleTwo")) return 15;
    if (!std::strcmp(phase, "BattleTwo")) return 300.5f;
    if (!std::strcmp(phase, "PrepareBattleThree")) return 20;
    if (!std::strcmp(phase, "PrepareBattleFour")) return 15;
    if (!std::strcmp(phase, "Victory")) return 20;
    if (!std::strcmp(phase, "Reward")) return 6;
    return -1;
}
const char* ceoClip(const net::BossState& state, uint32_t nowMs) {
    if (!std::strcmp(state.phase, "Victory")) return "Ff_speech";
    if (state.attack == 16) return "Fb_neutral";
    if (state.attack == 17 || state.attack == 18) return "golf_swing";
    return vpClip(state, nowMs);
}
} // namespace tt::world
