#include "game/pairing_logic.h"

#include <cmath>
#include <algorithm>

namespace tt::game::pairing {

namespace {

struct CPythonRNG {
    static constexpr int N = 624;
    static constexpr int M = 397;
    static constexpr uint32_t MATRIX_A = 0x9908b0dfUL;
    static constexpr uint32_t UPPER_MASK = 0x80000000UL;
    static constexpr uint32_t LOWER_MASK = 0x7fffffffUL;

    uint32_t mt[N] = {};
    int mti = N + 1;

    void seed(uint32_t s) {
        mt[0] = 19650218UL;
        for (int i = 1; i < N; ++i) {
            mt[i] = (1812433253UL * (mt[i - 1] ^ (mt[i - 1] >> 30)) + i);
        }

        uint32_t key[1] = { s };
        int i = 1, j = 0;
        for (int k = N; k > 0; --k) {
            mt[i] = ((mt[i] ^ ((mt[i - 1] ^ (mt[i - 1] >> 30)) * 1664525UL)) + key[j] + j);
            i++; j++;
            if (i >= N) { mt[0] = mt[N - 1]; i = 1; }
            if (j >= 1) j = 0;
        }
        for (int k = N - 1; k > 0; --k) {
            mt[i] = ((mt[i] ^ ((mt[i - 1] ^ (mt[i - 1] >> 30)) * 1566083941UL)) - i);
            i++;
            if (i >= N) { mt[0] = mt[N - 1]; i = 1; }
        }
        mt[0] = 0x80000000UL;
        mti = N;
    }

    uint32_t genrand_int32() {
        uint32_t y;
        static const uint32_t mag01[2] = { 0x0UL, MATRIX_A };

        if (mti >= N) {
            int kk;
            for (kk = 0; kk < N - M; kk++) {
                y = (mt[kk] & UPPER_MASK) | (mt[kk + 1] & LOWER_MASK);
                mt[kk] = mt[kk + M] ^ (y >> 1) ^ mag01[y & 0x1UL];
            }
            for (; kk < N - 1; kk++) {
                y = (mt[kk] & UPPER_MASK) | (mt[kk + 1] & LOWER_MASK);
                mt[kk] = mt[kk + (M - N)] ^ (y >> 1) ^ mag01[y & 0x1UL];
            }
            y = (mt[N - 1] & UPPER_MASK) | (mt[0] & LOWER_MASK);
            mt[N - 1] = mt[M - 1] ^ (y >> 1) ^ mag01[y & 0x1UL];
            mti = 0;
        }

        y = mt[mti++];
        y ^= (y >> 11);
        y ^= (y << 7) & 0x9d2c5680UL;
        y ^= (y << 15) & 0xefc60000UL;
        y ^= (y >> 18);
        return y;
    }

    uint32_t getrandbits(int k) {
        if (k <= 0) return 0;
        return genrand_int32() >> (32 - k);
    }

    uint32_t randbelow(uint32_t n) {
        if (n <= 1) return 0;
        int k = 0;
        while ((n >> k) > 0) {
            k++;
        }
        uint32_t r = getrandbits(k);
        while (r >= n) {
            r = getrandbits(k);
        }
        return r;
    }

    void shuffleCards(uint8_t cards[kTotalDeckCards]) {
        for (int i = kTotalDeckCards - 1; i > 0; --i) {
            int j = randbelow(i + 1);
            std::swap(cards[i], cards[j]);
        }
    }
};

} // namespace

void generateDeck(uint32_t seed, int numPlayers, Card outCards[kSoloDeckCards], int& outCount) {
    uint8_t deck[kTotalDeckCards];
    for (int i = 0; i < kTotalDeckCards; ++i) {
        deck[i] = (uint8_t)i;
    }

    CPythonRNG rng;
    rng.seed(seed);
    rng.shuffleCards(deck);

    // Max rank for 1 or 2 players is 7 (ranks 0..7)
    int maxRank = (numPlayers >= 4) ? 9 : (numPlayers == 3) ? 8 : kMaxRankUsedSolo;
    outCount = 0;
    for (int i = 0; i < kTotalDeckCards; ++i) {
        uint8_t val = deck[i];
        uint8_t rank = val % kMaxRank;
        uint8_t suit = val / kMaxRank;
        if (rank <= maxRank) {
            if (outCount < kSoloDeckCards) {
                outCards[outCount].value = val;
                outCards[outCount].rank = rank;
                outCards[outCount].suit = suit;
                outCount++;
            }
        }
    }
}

bool isMatch(const Card& a, const Card& b) {
    return a.rank == b.rank;
}

float calcLowFlipModifier(int matches, int flips) {
    int idealFlips = int(std::round(matches * 2 * 1.6f));
    if (idealFlips < 2) idealFlips = 2;
    int maxFlipsForBonus = idealFlips * 2;

    if (flips < idealFlips) return 1.0f;
    if (flips > maxFlipsForBonus) return 0.0f;

    const float divisor = float(maxFlipsForBonus - idealFlips);
    const float diff = float(maxFlipsForBonus - flips);
    return diff / divisor;
}

int calcLowFlipBonus(int matches, int flips) {
    float mod = calcLowFlipModifier(matches, flips);
    return int(mod * matches);
}

int calculateScore(int points, int matches, int flips, int totalCards) {
    int score = (points > 1) ? points : 1;
    score += calcLowFlipBonus(matches, flips);
    if (matches == totalCards / 2) {
        score += int(std::round(totalCards / 4.0f));
    }
    return score;
}

} // namespace tt::game::pairing
