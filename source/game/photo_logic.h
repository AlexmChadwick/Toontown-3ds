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

// Upstream toon_cannon.bam: the "cannon" swivel node sits at (0, 0, 1.89703) above the tripod root,
// and the camera is parented to it (DistributedPhotoGame.__putCameraOnTripod).
constexpr float kTripodPivotZ = 1.89703f;

// Upstream DistributedPhotoGame.__localPhotoMoveTask: the tripod swivel's heading and pitch.
// Yaw is never clamped upstream (PHOTO_ROTATION_MIN/MAX are unused there); pitch is clamped.
struct Swivel {
    float h = 0.0f;
    float p = 0.0f;
};

// turn > 0 turns left (LEFT adds PHOTO_ROTATION_VEL), tilt > 0 looks up (UP adds PHOTO_ANGLE_VEL).
// Both are analog in [-1, 1].
void stepSwivel(Swivel& sw, float turn, float tilt, float dt);

// Test autoplay: steers the swivel at a subject whose pose matches an open assignment and
// returns true once the swivel is on it (inside evaluateShot's cone), so the shot scores.
bool planAutoShot(const Simulation& sim, float eyeX, float eyeY, float eyeZ, Swivel& sw, float gameTime, float dt);

} // namespace tt::photogame
