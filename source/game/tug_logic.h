#pragma once

#include <cstdint>

namespace tt::game::tuggame {

constexpr float kGameDuration = 40.0f;
constexpr float kSendUpdatePeriod = 0.2f;
constexpr int kWinBeans = 15;
constexpr int kLossBeans = 4;

constexpr float kDockY = 18.0f;
constexpr float kLeftDockX = -6.0f;  // solo toon base X
constexpr float kRightDockX = 6.0f;  // solo cog base X
constexpr float kWaterMinX = -2.0f;
constexpr float kWaterMaxX = 2.0f;

struct TargetRate {
    float duration;
    float idealRate;
};

constexpr int kNumTargetRates = 6;
constexpr TargetRate kTargetRates[kNumTargetRates] = {
    {8.0f, 6.0f},
    {5.0f, 7.0f},
    {6.0f, 8.0f},
    {6.0f, 10.0f},
    {7.0f, 11.0f},
    {8.0f, 12.0f},
};

float computeForce(int keyRate, float idealRate, float advantage = 1.0f, float rateMatchAward = 0.0f);
float targetRateAt(float elapsedSec);
bool isAllOut(float elapsedSec);
inline int suitJellybeans(int suitType) {
    return (suitType >= 1 && suitType <= 4) ? (1 << (suitType - 1)) : 1;
}

inline bool checkToonFallen(float toonX) {
    return toonX > kWaterMinX;
}

inline bool checkCogFallen(float cogX) {
    return cogX < kWaterMaxX;
}

class KeyTracker {
public:
    static constexpr int kMaxKeys = 32;

    void reset() {
        count_ = 0;
        lastButton_ = -1;
        rateMatchAward_ = 0.0f;
    }

    void press(int button) {
        if (button != lastButton_) {
            lastButton_ = button;
            if (count_ < kMaxKeys) {
                // insert at head
                for (int i = count_; i > 0; --i) ttl_[i] = ttl_[i - 1];
                ttl_[0] = 1.0f;
                count_++;
            }
        }
    }

    void update(float dt, float idealRate) {
        // Decay active keys
        int newCount = 0;
        for (int i = 0; i < count_; ++i) {
            ttl_[i] -= dt;
            if (ttl_[i] > 0.0f) {
                ttl_[newCount++] = ttl_[i];
            }
        }
        count_ = newCount;

        const int rate = count_;
        const int idealInt = static_cast<int>(idealRate);
        if (rate == idealInt || rate == idealInt + 1) {
            rateMatchAward_ += 0.3f * (dt / 0.1f);
        } else {
            rateMatchAward_ = 0.0f;
        }
    }

    int rate() const { return count_; }
    float award() const { return rateMatchAward_; }

private:
    float ttl_[kMaxKeys] = {};
    int count_ = 0;
    int lastButton_ = -1;
    float rateMatchAward_ = 0.0f;
};

} // namespace tt::game::tuggame
