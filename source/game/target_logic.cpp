#include "game/target_logic.h"

#include <cmath>
#include <cstring>

namespace tt::targetgame {

namespace {

constexpr int N = 624;
constexpr int M = 397;
constexpr uint32_t MATRIX_A = 0x9908b0dfU;
constexpr uint32_t UPPER_MASK = 0x80000000U;
constexpr uint32_t LOWER_MASK = 0x7fffffffU;

// TTC default difficulty pattern
// TargetGameGlobals.difficultyPatterns
struct Pattern {
    int counts[4];
    int values[4];
    float sizes[4];
    int subParts[4];
};

Pattern getPattern(uint32_t safezone) {
    switch (safezone) {
    case 1000: // DD
        return {{7, 4, 2, 0}, {11, 18, 25, 32}, {29.0f, 13.0f, 6.5f, 3.2f}, {2, 2, 2, 1}};
    case 5000: // DG
        return {{6, 4, 2, 0}, {11, 18, 25, 34}, {29.0f, 13.0f, 6.5f, 3.1f}, {2, 2, 2, 1}};
    case 4000: // MM
        return {{6, 4, 2, 0}, {12, 19, 27, 37}, {28.0f, 12.0f, 6.0f, 3.0f}, {2, 2, 2, 1}};
    case 3000: // BR
        return {{5, 4, 2, 0}, {12, 20, 29, 40}, {25.0f, 12.0f, 5.5f, 2.5f}, {2, 2, 2, 1}};
    case 9000: // DL
        return {{4, 3, 1, 0}, {12, 21, 31, 42}, {20.0f, 10.0f, 4.5f, 2.0f}, {2, 2, 2, 1}};
    case 2000: // TTC
    default:
        return {{8, 4, 2, 0}, {10, 16, 21, 28}, {31.0f, 15.0f, 7.0f, 3.5f}, {2, 2, 2, 1}};
    }
}

} // namespace

void Random::seed(uint32_t s) {
    state[0] = 19650218U & 0xffffffffU;
    for (int i = 1; i < N; i++) {
        state[i] = (1812433253U * (state[i - 1] ^ (state[i - 1] >> 30)) + i) & 0xffffffffU;
    }
    uint32_t init_key[1] = {s};
    size_t i = 1, j = 0;
    for (size_t k = N; k > 0; k--) {
        state[i] = ((state[i] ^ ((state[i - 1] ^ (state[i - 1] >> 30)) * 1664525U)) + init_key[j] + (uint32_t)j) & 0xffffffffU;
        i++; j++;
        if (i >= N) { state[0] = state[N - 1]; i = 1; }
        if (j >= 1) j = 0;
    }
    for (size_t k = N - 1; k > 0; k--) {
        state[i] = ((state[i] ^ ((state[i - 1] ^ (state[i - 1] >> 30)) * 1566083941U)) - (uint32_t)i) & 0xffffffffU;
        i++;
        if (i >= N) { state[0] = state[N - 1]; i = 1; }
    }
    state[0] = 0x80000000U;
    index = N;
}

uint32_t Random::gen32() {
    if (index >= N) {
        static const uint32_t mag01[2] = {0x0U, MATRIX_A};
        int kk;
        for (kk = 0; kk < N - M; kk++) {
            uint32_t y = (state[kk] & UPPER_MASK) | (state[kk + 1] & LOWER_MASK);
            state[kk] = state[kk + M] ^ (y >> 1) ^ mag01[y & 0x1U];
        }
        for (; kk < N - 1; kk++) {
            uint32_t y = (state[kk] & UPPER_MASK) | (state[kk + 1] & LOWER_MASK);
            state[kk] = state[kk + (M - N)] ^ (y >> 1) ^ mag01[y & 0x1U];
        }
        uint32_t y = (state[N - 1] & UPPER_MASK) | (state[0] & LOWER_MASK);
        state[N - 1] = state[M - 1] ^ (y >> 1) ^ mag01[y & 0x1U];
        index = 0;
    }
    uint32_t y = state[index++];
    y ^= (y >> 11);
    y ^= (y << 7) & 0x9d2c5680U;
    y ^= (y << 15) & 0xefc60000U;
    y ^= (y >> 18);
    return y;
}

double Random::random() {
    uint32_t a = gen32() >> 5;
    uint32_t b = gen32() >> 6;
    return (a * 67108864.0 + b) * (1.0 / 9007199254740992.0);
}

int setupTargets(uint32_t seed, uint32_t safezoneId, TargetRing* outTargets, int maxTargets) {
    Random rng;
    rng.seed(seed);

    Pattern pat = getPattern(safezoneId);
    float fieldWidth = kFieldWidth;
    float fieldLength = kFieldLength;

    int highestValue = 0;
    for (int i = 0; i < 4; i++) {
        if (pat.values[i] > highestValue) highestValue = pat.values[i];
    }
    float placeValue = highestValue * 0.5f;

    struct Place {
        float x, y, size;
    };
    Place placeList[32];
    int numPlaced = 0;
    int targetCount = 0;

    for (int typeIndex = 0; typeIndex < 4; typeIndex++) {
        for (int tIdx = 0; tIdx < pat.counts[typeIndex]; tIdx++) {
            bool good = false;
            float px = 0, py = 0, fsize = pat.sizes[typeIndex];
            int attempts = 0;
            while (!good && attempts++ < 1000) {
                px = static_cast<float>(rng.random() * (fieldWidth * 0.6) - (fieldWidth * 0.6 * 0.5));
                py = static_cast<float>((rng.random() * 0.6 + (0.4 * (placeValue / float(highestValue)))) * fieldLength);
                good = true;
                for (int p = 0; p < numPlaced; p++) {
                    float dx = placeList[p].x - px;
                    float dy = placeList[p].y - py;
                    float d = std::sqrt(dx * dx + dy * dy);
                    if (d - (fsize + placeList[p].size) <= 0.0f) {
                        good = false;
                        break;
                    }
                }
            }
            if (numPlaced < 32) {
                placeList[numPlaced++] = {px, py, fsize};
            }

            int subIndex = pat.subParts[typeIndex];
            while (subIndex > 0) {
                int combinedIndex = typeIndex + subIndex - 1;
                if (targetCount < maxTargets) {
                    outTargets[targetCount].x = px;
                    outTargets[targetCount].y = py;
                    outTargets[targetCount].radius = pat.sizes[combinedIndex];
                    outTargets[targetCount].value = pat.values[combinedIndex];
                    targetCount++;
                }
                subIndex--;
            }
        }
    }
    return targetCount;
}

int evaluateScore(const TargetRing* targets, int numTargets, float scoreX, float scoreY) {
    int topValue = 0;
    for (int i = 0; i < numTargets; i++) {
        float dx = targets[i].x - scoreX;
        float dy = targets[i].y - scoreY;
        float d = std::sqrt(dx * dx + dy * dy);
        if (d < targets[i].radius && topValue < targets[i].value) {
            topValue = targets[i].value;
        }
    }
    return topValue > 0 ? topValue : 1;
}

void FlightState::launch() {
    mode = FlightMode::Fly;
    speedForward = kSpeedLaunch * (power / 60.0f);
    lateral = -aimLateral * 7.0f;
    zVel = speedForward * 0.5f;
    z = kFlyZStart;
}

void FlightState::openUmbrella() {
    if (mode == FlightMode::Fly) {
        mode = FlightMode::Fall;
        speedForward *= 0.5f;
    }
}

void FlightState::step(float inputSteerX, float inputSteerY, float dt) {
    if (mode == FlightMode::Aim) {
        power += inputSteerY * 20.0f * dt;
        if (power < 40.0f) power = 40.0f;
        if (power > 120.0f) power = 120.0f;
        aimLateral += inputSteerX * 5.0f * dt;
        if (aimLateral < -kMaxLateral) aimLateral = -kMaxLateral;
        if (aimLateral > kMaxLateral) aimLateral = kMaxLateral;
        return;
    }

    if (mode == FlightMode::Fly) {
        speedForward -= speedForward * kAirResistance * dt;
        lateral -= lateral * kAirResistance * dt;
        y += dt * speedForward;
        x += dt * lateral;
        float lift = (speedForward - kSpeedStall) / kSpeedStall;
        if (lift < 0.0f) lift = 0.0f;
        zVel += (lift * 2.0f - kGravityFly) * dt;
        z += zVel * dt;
        if (z <= 0.0f) {
            z = 0.0f;
            mode = FlightMode::Landed;
        }
    } else if (mode == FlightMode::Fall) {
        speedForward -= speedForward * kUmbrellaResistance * dt;
        lateral -= lateral * kUmbrellaResistance * dt;
        driftX += inputSteerX * kMaxToonXZ * dt;
        driftY += inputSteerY * kMaxToonXZ * dt;
        if (driftX > kMaxToonXZ) driftX = kMaxToonXZ;
        if (driftX < -kMaxToonXZ) driftX = -kMaxToonXZ;
        if (driftY > kMaxToonXZ) driftY = kMaxToonXZ;
        if (driftY < -kMaxToonXZ) driftY = -kMaxToonXZ;

        x += (lateral + driftX) * dt;
        y += (speedForward + driftY) * dt;
        float lift = (speedForward - kSpeedStall) / kSpeedStall;
        if (lift < 0.0f) lift = 0.0f;
        zVel += (lift - kGravityFall) * dt;
        z += zVel * dt;
        if (z <= 0.0f) {
            z = 0.0f;
            mode = FlightMode::Landed;
        }
    }
}

} // namespace tt::targetgame
