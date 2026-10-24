#include "game/tug_logic.h"

#include <cmath>

namespace tt::game::tuggame {

float computeForce(int keyRate, float idealRate, float advantage, float rateMatchAward) {
    if (idealRate <= 0.0f) return 0.0f;
    const float stdDev = 0.25f * idealRate;
    const float diff = float(keyRate) - idealRate;
    const float exponent = -(diff * diff) / (2.0f * stdDev * stdDev);
    const float f = advantage * (rateMatchAward + 4.0f + 0.4f * idealRate) * std::exp(exponent);
    return f;
}

float targetRateAt(float elapsedSec) {
    float acc = 0.0f;
    for (int i = 0; i < kNumTargetRates; ++i) {
        acc += kTargetRates[i].duration;
        if (elapsedSec < acc) {
            return kTargetRates[i].idealRate;
        }
    }
    return kTargetRates[kNumTargetRates - 1].idealRate;
}

bool isAllOut(float elapsedSec) {
    float acc = 0.0f;
    for (int i = 0; i < kNumTargetRates - 1; ++i) {
        acc += kTargetRates[i].duration;
    }
    return elapsedSec >= acc;
}

} // namespace tt::game::tuggame
