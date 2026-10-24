#include "game/ring_logic.h"

namespace tt::game::ringgame {

namespace {

struct PatternList {
    int count;
    const int patterns[5][16];
};

const PatternList kPatternsTTC = {
    3,
    {
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        {0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1},
        {0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1},
    }
};

const PatternList kPatternsDD = {
    4,
    {
        {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1},
        {0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1},
        {0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1},
        {0, 0, 1, 1, 2, 2, 2, 2, 0, 0, 1, 1, 2, 2, 2, 2},
    }
};

const PatternList kPatternsDG = {
    4,
    {
        {0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
        {0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1},
        {0, 1, 1, 2, 0, 1, 1, 2, 0, 1, 1, 2, 0, 1, 1, 2},
        {0, 1, 2, 1, 0, 1, 2, 1, 0, 1, 2, 1, 0, 1, 2, 1},
    }
};

const PatternList kPatternsMM = {
    4,
    {
        {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
        {1, 1, 1, 2, 1, 1, 1, 2, 1, 1, 1, 2, 1, 1, 1, 2},
        {1, 1, 2, 2, 1, 1, 2, 2, 1, 1, 2, 2, 1, 1, 2, 2},
        {1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2},
    }
};

const PatternList kPatternsTB = {
    5,
    {
        {1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2},
        {1, 1, 1, 1, 1, 2, 2, 2, 1, 1, 1, 1, 1, 2, 2, 2},
        {0, 1, 1, 1, 0, 2, 2, 2, 0, 1, 1, 1, 0, 2, 2, 2},
        {0, 1, 1, 2, 0, 1, 2, 2, 0, 1, 1, 2, 0, 1, 2, 2},
        {0, 1, 2, 1, 0, 1, 2, 2, 0, 1, 2, 1, 0, 1, 2, 2},
    }
};

const PatternList kPatternsDDL = {
    4,
    {
        {0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2},
        {0, 1, 1, 1, 2, 2, 2, 2, 0, 1, 1, 1, 2, 2, 2, 2},
        {0, 1, 1, 2, 2, 1, 2, 2, 0, 1, 1, 2, 2, 1, 2, 2},
        {0, 1, 2, 1, 2, 1, 2, 2, 0, 1, 2, 1, 2, 1, 2, 2},
    }
};

const PatternList& getPatternList(uint32_t hood) {
    switch (hood) {
    case 1000: return kPatternsDD;
    case 5000: return kPatternsDG;
    case 4000: return kPatternsMM;
    case 3000: return kPatternsTB;
    case 9000: return kPatternsDDL;
    default:   return kPatternsTTC;
    }
}

} // namespace

void getDifficultyPattern(uint32_t hood, RandomNumGen& rng, int* outPattern) {
    const PatternList& pl = getPatternList(hood);
    const int choice = rng.choice(pl.count);
    for (int i = 0; i < kNumRingGroups; ++i) {
        outPattern[i] = pl.patterns[choice][i];
    }
}

bool checkCollision(float toonX, float toonZ, float ringX, float ringZ) {
    const double dx = double(toonX) - double(ringX);
    const double dz = double(toonZ) - double(ringZ);
    const double d2 = dx * dx + dz * dz;
    return d2 <= double(kRingRadiusSqrd);
}

int calcScore(int ringsCollected, bool perfect, int numPlayers) {
    int score = ringsCollected;
    if (perfect) {
        int bonus = 5;
        if (numPlayers == 3) bonus = 10;
        else if (numPlayers >= 4) bonus = 18;
        score += bonus;
    }
    return score > 1 ? score : 1;
}

void generateRings(uint32_t hood, RandomNumGen& rng, int colorIndex, RingGroupData* outGroups) {
    int pattern[kNumRingGroups];
    getDifficultyPattern(hood, rng, pattern);

    // Standard positions in normalized keypad space:
    // (center, up, down, left, right, ul, ur, lr, ll)
    static const float kKeypad[9][2] = {
        {0.0f, 0.0f},
        {0.0f, 0.6f},
        {0.0f, -0.6f},
        {-0.6f, 0.0f},
        {0.6f, 0.0f},
        {-0.5f, 0.5f},
        {0.5f, 0.5f},
        {0.5f, -0.5f},
        {-0.5f, -0.5f},
    };

    for (int i = 0; i < kNumRingGroups; ++i) {
        const int pIdx = rng.randint(0, 8);
        const float posScale = 0.7f + float(rng.random()) * 0.2f;
        outGroups[i].x = kKeypad[pIdx][0] * posScale * kMaxToonXZ;
        outGroups[i].z = kKeypad[pIdx][1] * posScale * kMaxToonXZ;
        outGroups[i].colorIndex = colorIndex >= 0 ? colorIndex : (i % 7);
    }
}

} // namespace tt::game::ringgame
