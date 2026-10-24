#include "world/cfo_logic.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace tt::world {
namespace {
constexpr float kDegrees = 0.01745329252f;
// DistributedCashbotBossCrane: cableLength=20, numLinks=3, hinge=(0,-17.6,38.5).
constexpr float kLinkLength = 20.0f / 3;
}

// ToontownGlobals.CashbotBossCranePosHprs / CashbotBossSafePosHprs.
const CfoPlacement kCfoCranes[4] = {
    {{97.4f, -337.6f, 0}, -45}, {{97.4f, -292.4f, 0}, -135},
    {{142.6f, -292.4f, 0}, 135}, {{142.6f, -337.6f, 0}, 45}
};
const CfoPlacement kCfoSafes[9] = {
    {{120, -315, 30}, 0}, {{77.2f, -329.3f, 0}, -90}, {{77.1f, -302.7f, 0}, -90},
    {{165.7f, -326.4f, 0}, 90}, {{165.5f, -302.4f, 0}, 90},
    {{107.8f, -359.1f, 0}, 0}, {{133.9f, -359.1f, 0}, 0},
    {{107, -274.7f, 0}, 180}, {{134.2f, -274.7f, 0}, 180}
};

Vec3 cfoToWorld(Vec3 local, CfoPlacement placement) {
    const float sine = std::sin(placement.heading * kDegrees);
    const float cosine = std::cos(placement.heading * kDegrees);
    return {placement.position.x + cosine * local.x - sine * local.y,
            placement.position.y + sine * local.x + cosine * local.y,
            placement.position.z + local.z};
}

float cfoImpact(Vec3 velocity, float craneHeading) {
    // DistributedCashbotBossObject.__hitBoss normalizes in the crane root frame.
    const float length = std::sqrt(velocity.x * velocity.x + velocity.y * velocity.y + velocity.z * velocity.z);
    if (!std::isfinite(length) || length < .001f) return 0;
    return std::clamp((-velocity.x * std::sin(craneHeading * kDegrees) +
                        velocity.y * std::cos(craneHeading * kDegrees)) / length, 0.0f, 1.0f);
}

bool cfoGoonStun(const net::CfoChild& goon, Vec3 position, Vec3 toon) {
    const float dx = toon.x - position.x;
    const float dy = toon.y - position.y;
    const float dz = toon.z - position.z;
    // Goon.createHead: headHeight=3, toon sphere at headHeight+0.8.
    const float height = 3.8f * goon.scale;
    const float radius = goon.scale + .5f;
    return dx * dx + dy * dy < radius * radius && dz > height - .6f && dz < height + 1;
}

bool cfoGoonDetect(const net::CfoChild& goon, Vec3 position, Vec3 toon) {
    const float dx = toon.x - position.x;
    const float dy = toon.y - position.y;
    const float distance = std::sqrt(dx * dx + dy * dy);
    return distance > .01f && distance < goon.radius && toon.z - position.z > -3.5f &&
           toon.z - position.z < 5 * goon.scale &&
           (-dx * std::sin(goon.h * kDegrees) + dy * std::cos(goon.h * kDegrees)) / distance >
               std::cos(goon.fov * .5f * kDegrees);
}

void CfoCable::reset() {
    y = 20;
    heading = 0;
    for (int index = 0; index < 3; ++index) {
        links[index] = {0, y, -(index + 1) * kLinkLength};
        previous[index] = links[index];
    }
}

void CfoCable::update(float slide, float turn, float seconds, bool safe) {
    // Fixed small substeps and length constraints approximate the stock chained
    // inverse-sphere links. No allocation and a strict maximum of four substeps.
    const float dt = std::clamp(seconds, 0.0f, 1.0f / 15);
    const int steps = std::max(1, static_cast<int>(std::ceil(dt * 60)));
    const float step = dt / steps;
    for (int substep = 0; substep < steps; ++substep) {
        y = std::clamp(y + std::clamp(slide, -1.0f, 1.0f) * (safe ? 11 : 10) * step, 8.0f, 25.0f);
        heading = std::clamp(heading + std::clamp(turn, -1.0f, 1.0f) * (safe ? 16 : 20) * step, -45.0f, 45.0f);
        for (int index = 0; index < 3; ++index) {
            const Vec3 before = links[index];
            links[index] = {before.x + (before.x - previous[index].x) * .995f,
                            before.y + (before.y - previous[index].y) * .995f,
                            before.z + (before.z - previous[index].z) * .995f - kCfoGravity * step * step};
            previous[index] = before;
        }
        const Vec3 anchor{-std::sin(heading * kDegrees) * y, std::cos(heading * kDegrees) * y, -1};
        for (int iteration = 0; iteration < 8; ++iteration) {
            for (int index = 0; index < 3; ++index) {
                const Vec3 parent = index ? links[index - 1] : anchor;
                const Vec3 delta{links[index].x - parent.x, links[index].y - parent.y, links[index].z - parent.z};
                const float length = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
                if (length < .001f) continue;
                const float correction = (length - kLinkLength) / length;
                const float weight = index ? .5f : 1.0f;
                links[index].x -= delta.x * correction * weight;
                links[index].y -= delta.y * correction * weight;
                links[index].z -= delta.z * correction * weight;
                if (index) {
                    links[index - 1].x += delta.x * correction * weight;
                    links[index - 1].y += delta.y * correction * weight;
                    links[index - 1].z += delta.z * correction * weight;
                }
            }
        }
    }
}

Vec3 CfoCable::magnet(int index) const {
    const Vec3 bottom = links[2];
    return cfoToWorld({bottom.x, bottom.y - 17.6f, bottom.z + 38.5f}, kCfoCranes[index % 4]);
}

const char* cfoClip(const net::BossState& state, uint32_t nowMs) {
    if (!std::strcmp(state.phase, "Victory")) {
        const float age = (nowMs - state.phaseMs) / 1000.0f;
        return age < 2 ? "Fb_firstHit" : age < 5.5f ? "Fb_down2Up" : "Ff_neutral";
    }
    if (!std::strcmp(state.phase, "BattleThree") && state.attack == net::kBossDizzy) return "Fb_downNeutral";
    return vpClip(state, nowMs);
}
} // namespace tt::world
