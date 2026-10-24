#include "game/cannon_logic.h"

#include <cmath>

namespace tt::game::cannongame {

namespace {
constexpr float kPi = 3.14159265358979323846f;
inline float toRadians(float deg) { return deg * kPi / 180.0f; }
} // namespace

int calcScore(float landTime) {
    const float range = float(kMaxScore - kMinScore);
    const float score = float(kMaxScore) - range * (landTime / kGameTime);
    return int(score + 0.5f);
}

Vec3 getTowerPosition(RandomNumGen& rng, float difficulty) {
    const int yRange = int(kTowerYRange);
    const int yMin = int(yRange * 0.3f);
    const int yMax = yRange;
    const int yInt = rng.randint(yMin, yMax);

    const int xRange = int(kTowerXRange);
    int xInt = rng.randint(0, xRange);
    xInt -= int(xRange / 2.0f);

    float x = float(xInt);
    float y = float(yInt);

    const float scale = 0.5f + 0.5f * difficulty;
    x *= scale;
    const float yCenter = (yMin + yMax) / 2.0f;
    y = (y - yCenter) * scale + yCenter;

    x = x * (y / float(yRange));
    y = y - float(int(yRange / 2.0f));
    return Vec3{x, y, 0.0f};
}

float cannonX(int slot, int numPlayers) {
    if (numPlayers < 1) numPlayers = 1;
    return slot * kCannonXSpacing - (numPlayers - 1) * kCannonXSpacing / 2.0f;
}

Vec3 trajectoryPos(const Vec3& startPos, const Vec3& startVel, float t) {
    return Vec3{
        startPos.x + startVel.x * t,
        startPos.y + startVel.y * t,
        startPos.z + startVel.z * t - 0.5f * kGravity * t * t
    };
}

Vec3 trajectoryVel(const Vec3& startVel, float t) {
    return Vec3{
        startVel.x,
        startVel.y,
        startVel.z - kGravity * t
    };
}

FlightResult calcFlight(float cx, float zRot, float angle, const Vec3& towerPos) {
    const float rotRad = toRadians(zRot);
    const float angRad = toRadians(angle);

    const float cosAng = std::cos(angRad);
    const float sinAng = std::sin(angRad);
    const float cosRot = std::cos(rotRad);
    const float sinRot = std::sin(rotRad);

    const float horizVel = kInitialVelocity * cosAng;
    const Vec3 startVel{
        horizVel * -sinRot,
        horizVel * cosRot,
        kInitialVelocity * sinAng
    };

    const Vec3 startPos{
        cx - kBarrelOffset * cosAng * sinRot,
        kCannonY + kBarrelOffset * cosAng * cosRot,
        kCannonZ + kBarrelPivotZ + kBarrelOffset * sinAng
    };

    FlightResult res;
    res.startPos = startPos;
    res.startVel = startVel;

    auto reachesHeight = [&](float h) {
        float t = startVel.z / kGravity;
        if (t < 0.0f) t = 0.0f;
        const float peakZ = startPos.z + startVel.z * t - 0.5f * kGravity * t * t;
        return peakZ >= h;
    };

    auto impactOnPlane = [&](float height) -> float {
        const float a = -0.5f * kGravity;
        const float b = startVel.z;
        const float c = startPos.z - height;
        const float D = b * b - 4.0f * a * c;
        if (D < 0.0f) return -1.0f;
        const float t = (-b - std::sqrt(D)) / (2.0f * a);
        return t >= 0.0f ? t : -1.0f;
    };

    // 1. Check collision with water disc at height = towerPos.z + kTowerHeight (43.85)
    const float waterZ = towerPos.z + kTowerHeight;
    if (reachesHeight(waterZ)) {
        const float tWater = impactOnPlane(waterZ);
        if (tWater > 0.0f) {
            const Vec3 p = trajectoryPos(startPos, startVel, tWater);
            const float dx = p.x - towerPos.x;
            const float dy = p.y - towerPos.y;
            if (dx * dx + dy * dy < kTowerRadius * kTowerRadius) {
                res.timeOfImpact = tWater;
                res.hitWhat = HitType::Water;
                res.score = calcScore(tWater);
                return res;
            }
        }
    }

    // 2. Check collision with cylinder sides:
    const float cylBottomZ = towerPos.z + kBucketHeight;
    const float cylTopZ = towerPos.z + kTowerHeight;
    if (reachesHeight(cylBottomZ)) {
        const float vx = startVel.x;
        const float vy = startVel.y;
        const float mag = std::sqrt(vx * vx + vy * vy);
        if (mag > 1e-4f) {
            const float dx = vx / mag;
            const float dy = vy / mag;
            const float ox = startPos.x - towerPos.x;
            const float oy = startPos.y - towerPos.y;
            const float b = dx * ox + dy * oy;
            const float c = (ox * ox + oy * oy) - kTowerRadius * kTowerRadius;
            const float bsmc = b * b - c;
            if (bsmc > 0.0f) {
                const float sqrt_bsmc = std::sqrt(bsmc);
                const float t1 = (-b - sqrt_bsmc) / mag;
                const float t2 = (-b + sqrt_bsmc) / mag;
                const Vec3 p1 = trajectoryPos(startPos, startVel, t1);
                const Vec3 p2 = trajectoryPos(startPos, startVel, t2);
                if (!(p1.z > cylTopZ && p2.z > cylTopZ)) {
                    if (p1.z < cylTopZ && p1.z > cylBottomZ && t1 > 0.0f) {
                        res.timeOfImpact = t1;
                        res.hitWhat = HitType::Tower;
                        res.score = calcScore(t1);
                        return res;
                    }
                }
            }
        }
    }

    // 3. Ground impact at z = 0.0
    const float tGround = impactOnPlane(0.0f);
    res.timeOfImpact = tGround >= 0.0f ? tGround : 0.0f;
    res.hitWhat = HitType::Ground;
    res.score = calcScore(res.timeOfImpact);
    return res;
}

} // namespace tt::game::cannongame
