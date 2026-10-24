#include "game/race_logic.h"

#include <cmath>

namespace tt::racegame {

const RewardDef kChanceRewards[kNumRewards] = {
    {1, 0, 0},
    {1, 0, 0},
    {1, 0, 0},
    {2, 0, 0},
    {2, 0, 0},
    {2, 0, 0},
    {3, 0, 0},
    {3, 0, 0},
    {3, 0, 0},
    {0, -3, 0},
    {0, -3, 0},
    {-1, 0, 0},
    {-1, 0, 0},
    {-2, 0, 0},
    {-2, 0, 0},
    {-3, 0, 0},
    {-3, 0, 0},
    {0, 3, 0},
    {0, 3, 0},
    {0, 0, 2},
    {0, 0, 2},
    {0, 0, 2},
    {0, 0, 2},
    {0, 0, 4},
    {0, 0, 4},
    {0, 0, 4},
    {0, 0, 4},
    {0, 0, 10},
    {0, 0, 0},
    {kNumberToWin, 0, 0}
};

void Simulation::init(int players) {
    numPlayers = (players >= 1 && players <= 4) ? players : 1;
    isDone = false;
    for (int i = 0; i < 4; ++i) {
        positions[i] = 0;
        chancePositions[i] = -1;
        chanceRewardIdx[i] = -1;
        scores[i] = 0;
    }
}

void Simulation::setChance(int player, int pos, int rewardIdx) {
    if (player >= 0 && player < 4) {
        chancePositions[player] = pos;
        chanceRewardIdx[player] = rewardIdx;
    }
}

bool Simulation::processChoices(const int choices[4], int outRewards[4]) {
    if (outRewards) {
        for (int i = 0; i < 4; ++i) outRewards[i] = -1;
    }

    // Check choice frequency (matching choices cancel out if > 0)
    for (int i = 0; i < numPlayers; ++i) {
        int c = choices[i];
        if (c <= 0) continue;

        int freq = 0;
        for (int j = 0; j < numPlayers; ++j) {
            if (choices[j] == c) freq++;
        }

        if (freq == 1) {
            positions[i] += c;
            if (positions[i] < 0) positions[i] = 0;

            if (positions[i] == chancePositions[i] && chancePositions[i] != -1) {
                int rIdx = chanceRewardIdx[i];
                if (outRewards) outRewards[i] = rIdx;
                scores[i] += kChanceRewards[rIdx].beanBonus;
                chancePositions[i] = -1;

                // Apply chance effect
                positions[i] += kChanceRewards[rIdx].deltaSelf;
                if (positions[i] < 0) positions[i] = 0;
                for (int o = 0; o < numPlayers; ++o) {
                    if (o != i) {
                        positions[o] += kChanceRewards[rIdx].deltaOthers;
                        if (positions[o] < 0) positions[o] = 0;
                    }
                }
            }
        }
    }

    // Check winners
    bool won = false;
    for (int i = 0; i < numPlayers; ++i) {
        if (positions[i] >= kNumberToWin) {
            won = true;
            break;
        }
    }

    if (won) {
        finalizeScores();
        isDone = true;
    }

    return won;
}

void Simulation::finalizeScores() {
    int numWinners = 0;
    for (int i = 0; i < numPlayers; ++i) {
        if (positions[i] >= kNumberToWin) numWinners++;
    }

    for (int i = 0; i < numPlayers; ++i) {
        int beans = 0;
        if (positions[i] >= kNumberToWin) {
            beans = kNumberToWin;
            if (numWinners > 1) {
                beans -= 3;
            }
        } else {
            beans = (int)std::ceil(positions[i] * 0.5f);
        }
        scores[i] += beans;
    }
}

} // namespace tt::racegame
