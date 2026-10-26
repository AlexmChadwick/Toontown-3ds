// Photo minigame pure logic (no 3DS headers).
// Camera targeting, upstream PhotoGameBase assignment generation, subject motion tracks, and star scoring.
#pragma once

#include <cstdint>
#include "game/photo_tables.h"

namespace tt::photogame {

constexpr int kMaxPlayers = 4;

struct Assignment {
    int subjectIndex = 0;
    char poseName[32] = {};
    float playerScores[kMaxPlayers] = {};
    int highScorer = -1;
};

struct SubjectPose {
    float x = 0, y = 0, z = 0;
    float h = 0;
    char poseName[32] = {};
    float interest = 1.0f; // 1.0 for walk/run, 2.0 for emotes
};

struct PhotoShotResult {
    bool hitSubject = false;
    int subjectIndex = -1;
    char poseName[32] = {};
    float stars = 0.0f;
    int assignmentIndex = -1;
    bool newHighScore = false;
};

class Simulation {
public:
    int numPlayers = 1;
    uint32_t safezoneId = 2000;
    int maxFilm = 32;
    int filmUsed[kMaxPlayers] = {};
    Assignment assignments[kOnscreenAssignments];
    int scores[kMaxPlayers] = {};
    bool isDone = false;

    void init(int players, uint32_t hoodId, uint32_t seed);
    void generateAssignments(uint32_t seed);
    void getSubjectPose(int subjectIndex, float gameTime, SubjectPose& outPose) const;
    PhotoShotResult evaluateShot(int playerIdx, float camX, float camY, float camZ, float camH, float camP, float gameTime);
    bool submitAIScore(uint32_t avId, int playerIdx, int assignmentIdx, float stars);
    void calculateScores();
};

} // namespace tt::photogame
