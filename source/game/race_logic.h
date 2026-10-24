// Race minigame pure logic (no 3DS headers). Board game movement, chance squares, and choice cancellation.
#pragma once

#include <cmath>
#include <cstdint>

namespace tt::racegame {

constexpr int kNumberToWin = 14;
constexpr int kNumRewards = 30;

struct RewardDef {
    int deltaSelf;
    int deltaOthers;
    int beanBonus;
};

// Defined according to RaceGameGlobals.ChanceRewards
extern const RewardDef kChanceRewards[kNumRewards];

struct Simulation {
    int numPlayers = 1;
    int positions[4] = {};
    int chancePositions[4] = {};
    int chanceRewardIdx[4] = {};
    int scores[4] = {};
    bool isDone = false;

    void init(int players);
    void setChance(int player, int pos, int rewardIdx);
    // Returns true if game is over
    bool processChoices(const int choices[4], int outRewards[4]);
    void finalizeScores();
};

} // namespace tt::racegame
