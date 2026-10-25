#include "world/cogdo_crane.h"
#include <algorithm>
#include <cmath>
namespace tt::world {
namespace {
constexpr float kDegrees = 0.01745329252f;
constexpr float kLinkLength = 20.0f / 3;
} // namespace
// CogdoCraneGameConsts.CranePositions / MoneyBagPositions, not the CFO arena.
const CogdoPlacement kCogdoCranes[4] = {{{13.4f, -136.6f, 6}, -45},
                                        {{13.4f, -91.4f, 6}, -135},
                                        {{58.6f, -91.4f, 6}, 135},
                                        {{58.6f, -136.6f, 6}, 45}};
const CogdoPlacement kCogdoBags[8] = {{{-6.8f, -128.3f, 6}, -90}, {{-6.9f, -101.7f, 6}, -90},
                                      {{81.7f, -125.4f, 6}, 90},  {{81.5f, -101.4f, 6}, 90},
                                      {{23.8f, -158.1f, 6}, 0},   {{49.9f, -158.1f, 6}, 0},
                                      {{23, -73.7f, 6}, 180},     {{50.2f, -73.7f, 6}, 180}};
Vec3 cogdoToWorld(Vec3 local, CogdoPlacement placement) {
    const float sine = std::sin(placement.heading * kDegrees);
    const float cosine = std::cos(placement.heading * kDegrees);
    return {placement.position.x + cosine * local.x - sine * local.y,
            placement.position.y + sine * local.x + cosine * local.y, placement.position.z + local.z};
}
void CogdoCable::reset() {
    y = 20;
    heading = 0;
    for (int index = 0; index < 3; ++index) {
        links[index] = {0, y, -(index + 1) * kLinkLength};
        previous[index] = links[index];
    }
}

void CogdoCable::update(float slide, float turn, float seconds, bool safe) {
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
                            before.z + (before.z - previous[index].z) * .995f - kCogdoGravity * step * step};
            previous[index] = before;
        }
        const Vec3 anchor{-std::sin(heading * kDegrees) * y, std::cos(heading * kDegrees) * y, -1};
        for (int iteration = 0; iteration < 8; ++iteration) {
            for (int index = 0; index < 3; ++index) {
                const Vec3 parent = index ? links[index - 1] : anchor;
                const Vec3 delta{links[index].x - parent.x, links[index].y - parent.y,
                                 links[index].z - parent.z};
                const float length = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
                if (length < .001f)
                    continue;
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

Vec3 CogdoCable::magnet(int index) const {
    const Vec3 bottom = links[2];
    return cogdoToWorld({bottom.x, bottom.y - 17.6f, bottom.z + 38.5f}, kCogdoCranes[index % 4]);
}

} // namespace tt::world
