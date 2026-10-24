#include "game/pairing_logic.h"
#include <cstdio>

int main() {
    using namespace tt::game::pairing;

    const uint32_t seeds[6] = {0, 5, 42, 123456, 3999999, 1000000};
    for (int s = 0; s < 6; ++s) {
        Card cards[kSoloDeckCards];
        int count = 0;
        generateDeck(seeds[s], 1, cards, count);

        std::printf("seed %u: count=%d\n", seeds[s], count);
        std::printf("  cards:");
        for (int i = 0; i < count; ++i) {
            std::printf(" %02d", cards[i].value);
        }
        std::printf("\n  ranks:");
        for (int i = 0; i < count; ++i) {
            std::printf(" %d", cards[i].rank);
        }
        std::printf("\n  suits:");
        for (int i = 0; i < count; ++i) {
            std::printf(" %d", cards[i].suit);
        }
        std::printf("\n");
    }

    const int match_counts[5] = {1, 4, 8, 12, 16};
    for (int m = 0; m < 5; ++m) {
        int matches = match_counts[m];
        int flips_test[4] = {
            matches * 2,
            int(matches * 2 * 1.6f),
            matches * 4,
            matches * 5
        };
        for (int f = 0; f < 4; ++f) {
            int flips = flips_test[f];
            int bonus = calcLowFlipBonus(matches, flips);
            int score = calculateScore(matches, matches, flips, kSoloDeckCards);
            std::printf("eval: matches=%02d flips=%02d -> bonus=%02d score=%02d\n",
                        matches, flips, bonus, score);
        }
    }

    return 0;
}
