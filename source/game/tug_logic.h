#pragma once

#include <cstdint>

namespace tt::game::tuggame {

constexpr float kGameDuration = 40.0f;
constexpr float kSendUpdatePeriod = 0.2f;
constexpr int kWinBeans = 15;
constexpr int kLossBeans = 4;

constexpr float kDockY = 18.0f;
constexpr float kDockZ = 0.1f;      // dockPositions z
constexpr float kLeftDockX = -6.0f;  // solo toon base X (dock index 2)
constexpr float kRightDockX = 6.0f;  // solo cog base X (dock index 7)
constexpr float kWaterMinX = -2.0f;
constexpr float kWaterMaxX = 2.0f;

// DistributedTugOfWarGame.calculatePositions: dockPositions k<5 at -9.0+1.5k and k>=5 at 3+1.5(k-5),
// all at y = kDockY (the dock's water is under them). Toon-vs-Cog toons sit at the listed indices
// and the Cog at index 7 (6 with four toons); toons face hpr 240, the Cog hpr 120 (createSuits).
constexpr int kNumDockSlots = 10;
constexpr float kToonHeading = 240.0f;
constexpr float kCogHeading = 120.0f;
inline float dockX(int k) {
    return k < 5 ? -9.0f + 1.5f * k : 3.0f + 1.5f * (k - 5);
}
// DistributedTugOfWarGame.handycap (= 2.0, set in __init__): moveToons places a toon at origPos.x + offset / handycap.
constexpr float kHandycap = 2.0f;
constexpr int kTugOfWarToonVsCog = 1; // TugOfWarGameGlobals.TOON_VS_COG (TOON_VS_TOON = 0)
// Index of the toon at arrangement slot 'slot' in a toon-vs-cog game with 'numPlayers' toons.
inline int toonDockIndex(int numPlayers, int slot) {
    static constexpr int kLayout[5][4] = {{0, 0, 0, 0}, {2, 0, 0, 0}, {1, 2, 0, 0}, {0, 1, 2, 0}, {0, 1, 2, 3}};
    if (numPlayers < 1 || numPlayers > 4) return 2;
    if (slot < 0) slot = 0;
    if (slot >= numPlayers) slot = numPlayers - 1;
    return kLayout[numPlayers][slot];
}
// A toon's x in its dock slot plus the server's offset (DistributedTugOfWarGame.moveToons).
inline float toonX(int numPlayers, int index, float serverOffset) {
    return dockX(toonDockIndex(numPlayers, index)) + serverOffset / kHandycap;
}
inline int suitDockIndex(int numPlayers) {
    return numPlayers == 4 ? 6 : 7;
}
// DistributedTugOfWarGame.sendGameType: suit level 1..4 is the Cog head code (default Flunky 'f').
inline const char* suitCode(int suitLevel) {
    switch (suitLevel) {
    case 1: return "pp";
    case 2: return "dt";
    case 3: return "gh";
    case 4: return "cr";
    default: return "f";
    }
}

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
