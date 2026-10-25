#include "world/cj_logic.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace tt::world {
namespace {
constexpr float kDegrees = .01745329252f;
}
float cjScaleTilt(float damage) {
    return (std::clamp(damage, 0.0f, kCjMaxDamage) - kCjInitialDamage) / kCjInitialDamage * 40;
}
Vec3 cjJuryOffset(float seconds) {
    const float remaining = 1 - std::clamp(seconds / kCjJurySeconds, 0.0f, 1.0f);
    return {-30 * remaining, 0, -12.645f * remaining};
}
Vec3 cjChairPosition(int index, float seconds) {
    // DistributedLawbotChair.computePos: six seats per interpolated row.
    const Vec3 offset = cjJuryOffset(seconds);
    const bool back = index >= 6;
    return {(back ? 75.1f : 59.3f) + offset.x,
            -18.2f + 66.2f * (index % 6) / 5, (back ? 28.2f : 14.05f) + offset.z};
}
Vec3 cjRotate(Vec3 point, float heading, float pitch) {
    const float sineP = std::sin(pitch * kDegrees);
    const float cosineP = std::cos(pitch * kDegrees);
    const float y = point.y * cosineP - point.z * sineP;
    const float z = point.y * sineP + point.z * cosineP;
    const float sineH = std::sin(heading * kDegrees);
    const float cosineH = std::cos(heading * kDegrees);
    return {point.x * cosineH - y * sineH, point.x * sineH + y * cosineH, z};
}
CjFlight cjCannonFlight(Vec3 position, float heading, float rotation, float elevation, Vec3 barrelPivot) {
    // Cannon has root H, turntable rotation, then barrel P; juror head at (0,6,0).
    const Vec3 muzzle = cjRotate({0, 6, 0}, 0, elevation);
    const Vec3 offset = cjRotate({barrelPivot.x + muzzle.x, barrelPivot.y + muzzle.y,
                                 barrelPivot.z + muzzle.z}, heading + rotation);
    const Vec3 velocity = cjRotate({0, kCjCannonSpeed, 0}, heading + rotation, elevation);
    return {{position.x + offset.x, position.y + offset.y, position.z + .025f + offset.z}, velocity};
}
Vec3 cjFlightPosition(CjFlight flight, float seconds) {
    return {flight.origin.x + flight.velocity.x * seconds,
            flight.origin.y + flight.velocity.y * seconds,
            flight.origin.z + flight.velocity.z * seconds - .5f * kCjGravity * seconds * seconds};
}
bool cjAimCannon(Vec3 position, float heading, Vec3 pivot, Vec3 target, float* rotation, float* elevation) {
    float angle = 25;
    float turn = 0;
    for (int iteration = 0; iteration < 5; ++iteration) {
        const CjFlight flight = cjCannonFlight(position, heading, turn, angle, pivot);
        const float dx = target.x - flight.origin.x;
        const float dy = target.y - flight.origin.y;
        const float dz = target.z - flight.origin.z;
        const float distance = std::sqrt(dx * dx + dy * dy);
        if (distance < .1f) return false;
        const float speedSquared = kCjCannonSpeed * kCjCannonSpeed;
        const float discriminant = speedSquared * speedSquared - kCjGravity *
            (kCjGravity * distance * distance + 2 * dz * speedSquared);
        if (discriminant < 0) return false;
        angle = std::atan((speedSquared - std::sqrt(discriminant)) / (kCjGravity * distance)) / kDegrees;
        turn = std::atan2(-dx, dy) / kDegrees - heading;
        while (turn > 180) turn -= 360;
        while (turn < -180) turn += 360;
    }
    if (turn < -55 || turn > 50 || angle < 10 || angle > 85) return false;
    *rotation = turn;
    *elevation = angle;
    return true;
}
CjGavelPose cjGavelPose(int index, float seconds) {
    const CjGavelLayout& layout = kCjGavels[index % 8];
    const float period = layout.down + layout.stay + layout.up;
    const float cycle = std::max(0.0f, seconds) / period;
    const int current = static_cast<int>(cycle) % layout.count;
    const int next = (current + 1) % layout.count;
    const float time = std::fmod(std::max(0.0f, seconds), period);
    float pitch = -80;
    float heading = layout.headings[current];
    if (time < layout.down) pitch = -80 * time / layout.down;
    else if (time >= layout.down + layout.stay) {
        const float fraction = (time - layout.down - layout.stay) / layout.up;
        pitch = -80 * (1 - fraction);
        heading += (layout.headings[next] - heading) * fraction;
    }
    return {layout.heading + heading, pitch};
}
Vec3 cjScalePan(Vec3 beamPivot, Vec3 pan, float damage) {
    const Vec3 moving = cjRotate(pan, 0, cjScaleTilt(damage));
    // Scale root H=90, S=(1.75,1.75,1.5), with a counter-rotated level pan.
    return {kCjScalePosition.x - (beamPivot.y + moving.y) * 1.75f,
            kCjScalePosition.y + (beamPivot.x + moving.x) * 1.75f,
            kCjScalePosition.z + (beamPivot.z + moving.z) * 1.5f};
}
float cjBarrierDelay(const char* phase) {
    if (!std::strcmp(phase, "WaitForToons")) return .5f;
    if (!std::strcmp(phase, "Elevator")) return 6;
    if (!std::strcmp(phase, "Introduction")) return 20;
    if (!std::strcmp(phase, "RollToBattleTwo")) return 10;
    if (!std::strcmp(phase, "PrepareBattleTwo")) return 10;
    if (!std::strcmp(phase, "BattleTwo")) return 70.5f;
    if (!std::strcmp(phase, "RollToBattleThree")) return 8;
    if (!std::strcmp(phase, "PrepareBattleThree")) return 10;
    if (!std::strcmp(phase, "Victory")) return 12;
    if (!std::strcmp(phase, "Defeat")) return 8;
    if (!std::strcmp(phase, "Reward")) return 5;
    return -1;
}
const char* cjClip(const net::BossState& state, uint32_t nowMs) {
    if (!std::strcmp(state.phase, "Defeat")) return "Ff_speech";
    if (!std::strcmp(state.phase, "Victory")) return "Fb_fall";
    return vpClip(state, nowMs);
}
} // namespace tt::world
