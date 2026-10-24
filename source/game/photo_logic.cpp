#include "game/photo_logic.h"

#include <cmath>

namespace tt::photogame {

void Simulation::init(int players, int film) {
    numPlayers = (players >= 1 && players <= 4) ? players : 1;
    maxFilm = (film > 0) ? film : kDefaultFilm;
    isDone = false;
    for (int i = 0; i < 4; ++i) {
        filmUsed[i] = 0;
        scores[i] = 0;
    }
    for (int a = 0; a < kNumAssignments; ++a) {
        assignments[a].subjectIndex = a;
        assignments[a].poseIndex = 0;
        assignments[a].highScorer = -1;
        for (int i = 0; i < 4; ++i) {
            assignments[a].playerScores[i] = 0.0f;
        }
    }
}

void Simulation::setAssignment(int slot, int subject, int pose) {
    if (slot >= 0 && slot < kNumAssignments) {
        assignments[slot].subjectIndex = subject;
        assignments[slot].poseIndex = pose;
    }
}

bool Simulation::submitPhoto(int playerIdx, int subject, int pose, float stars) {
    if (playerIdx < 0 || playerIdx >= numPlayers || isDone) {
        return false;
    }

    filmUsed[playerIdx]++;
    if (stars > (float)kMaxStars) stars = (float)kMaxStars;
    if (stars < 0.0f) stars = 0.0f;

    bool matched = false;
    for (int a = 0; a < kNumAssignments; ++a) {
        if (assignments[a].subjectIndex == subject && assignments[a].poseIndex == pose) {
            matched = true;
            if (stars > assignments[a].playerScores[playerIdx]) {
                assignments[a].playerScores[playerIdx] = stars;
                int high = assignments[a].highScorer;
                if (high == -1 || assignments[a].playerScores[playerIdx] > assignments[a].playerScores[high]) {
                    assignments[a].highScorer = playerIdx;
                }
            }
            break;
        }
    }

    // Check if all players out of film
    bool allOut = true;
    for (int i = 0; i < numPlayers; ++i) {
        if (filmUsed[i] < maxFilm) {
            allOut = false;
            break;
        }
    }

    if (allOut) {
        calculateScores();
        isDone = true;
    }

    return matched;
}

void Simulation::calculateScores() {
    float playerBonus[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float teamScore = 0.0f;

    for (int a = 0; a < kNumAssignments; ++a) {
        int high = assignments[a].highScorer;
        if (high != -1) {
            float highestScore = assignments[a].playerScores[high];
            teamScore += highestScore;
            playerBonus[high] += 1.0f;
        }
    }

    for (int i = 0; i < numPlayers; ++i) {
        scores[i] = (int)std::round(teamScore + playerBonus[i]);
    }
}

} // namespace tt::photogame
