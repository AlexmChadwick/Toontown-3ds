// Photo minigame pure logic (no 3DS headers). Camera targeting, assignments, and star scoring.
#pragma once

#include <cstdint>

namespace tt::photogame {

constexpr int kNumAssignments = 5;
constexpr int kMaxStars = 5;
constexpr int kDefaultFilm = 32;

struct Assignment {
    int subjectIndex = 0;
    int poseIndex = 0;
    float playerScores[4] = {};
    int highScorer = -1;
};

struct Simulation {
    int numPlayers = 1;
    int maxFilm = kDefaultFilm;
    int filmUsed[4] = {};
    Assignment assignments[kNumAssignments];
    int scores[4] = {};
    bool isDone = false;

    void init(int players, int film = kDefaultFilm);
    void setAssignment(int slot, int subject, int pose);
    bool submitPhoto(int playerIdx, int subject, int pose, float stars);
    void calculateScores();
};

} // namespace tt::photogame
