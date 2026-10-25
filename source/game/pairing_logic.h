#pragma once

#include <cstdint>

namespace tt::game::pairing {

constexpr int kMaxSuit = 4;
constexpr int kMaxRank = 13;
constexpr int kTotalDeckCards = 52;
constexpr int kMaxDeckCards = 40; // PairingGameGlobals: 10 ranks for four players
constexpr int kSoloDeckCards = 32; // 8 ranks (0..7) * 4 suits
constexpr int kMaxRankUsedSolo = 7;

struct Card {
    uint8_t value; // 0..51
    uint8_t rank;  // 0..12 (for solo, 0..7)
    uint8_t suit;  // 0..3 (Hearts, Diamonds, Clubs, Spades)
};

// CPython MT19937 RNG matching Python 3's random.Random(seed).shuffle(cards)
void generateDeck(uint32_t seed, int numPlayers, Card outCards[kSoloDeckCards], int& outCount);

// Match check: two cards match if their ranks are equal
bool isMatch(const Card& a, const Card& b);

// Low-flip bonus formula matching PairingGameGlobals.py:calcLowFlipModifier
float calcLowFlipModifier(int matches, int flips);
int calcLowFlipBonus(int matches, int flips);

// Final score calculation matching DistributedPairingGameAI.py:gameOver
int calculateScore(int points, int matches, int flips, int totalCards);

} // namespace tt::game::pairing
