#include "game/photo_logic.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace tt::photogame {

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
        uint32_t temp = n;
        while (temp > 0) {
            k++;
            temp >>= 1;
        }
        uint32_t r = getrandbits(k);
        while (r >= n) {
            r = getrandbits(k);
        }
        return r;
    }
};

} // namespace

void Simulation::init(int players, uint32_t hoodId, uint32_t seed) {
    numPlayers = (players >= 1 && players <= kMaxPlayers) ? players : 1;
    safezoneId = hoodId;
    const auto* data = getHoodPhotoData(hoodId);
    maxFilm = data ? data->filmCount : 32;
    isDone = false;
    for (int i = 0; i < kMaxPlayers; ++i) {
        filmUsed[i] = 0;
        scores[i] = 0;
    }
    for (int a = 0; a < kOnscreenAssignments; ++a) {
        assignments[a].subjectIndex = a;
        assignments[a].poseName[0] = '\0';
        assignments[a].highScorer = -1;
        for (int p = 0; p < kMaxPlayers; ++p) {
            assignments[a].playerScores[p] = 0.0f;
        }
    }
    generateAssignments(seed);
}

void Simulation::generateAssignments(uint32_t seed) {
    const auto* data = getHoodPhotoData(safezoneId);
    if (!data || data->numPaths == 0) return;

    CPythonRNG rng;
    rng.seed(seed);

    int count = 0;
    while (count < kOnscreenAssignments) {
        int subIdx = rng.randbelow(data->numPaths);
        int animSetIdx = data->paths[subIdx].animSetIndex;
        const auto& pSet = data->poseSets[animSetIdx];

        const char* poseName = nullptr;
        while (!poseName) {
            int poseIdx = rng.randbelow(pSet.numPoses);
            poseName = pSet.poses[poseIdx].name;
        }

        // Check duplicate
        bool dup = false;
        for (int i = 0; i < count; ++i) {
            if (assignments[i].subjectIndex == subIdx &&
                std::strcmp(assignments[i].poseName, poseName) == 0) {
                dup = true;
                break;
            }
        }
        if (!dup) {
            assignments[count].subjectIndex = subIdx;
            std::strncpy(assignments[count].poseName, poseName, sizeof(assignments[count].poseName) - 1);
            assignments[count].poseName[sizeof(assignments[count].poseName) - 1] = '\0';
            count++;
        }
    }
}

void Simulation::getSubjectPose(int subjectIndex, float gameTime, SubjectPose& outPose) const {
    const auto* data = getHoodPhotoData(safezoneId);
    if (!data || subjectIndex < 0 || subjectIndex >= data->numPaths) {
        outPose = SubjectPose{};
        return;
    }

    const auto& pathInfo = data->paths[subjectIndex];
    int nPoints = pathInfo.numPoints;
    if (nPoints <= 1) {
        outPose.x = pathInfo.points[0].x;
        outPose.y = pathInfo.points[0].y;
        outPose.z = pathInfo.points[0].z;
        outPose.h = 0.0f;
        std::strcpy(outPose.poseName, "neutral");
        outPose.interest = 1.0f;
        return;
    }

    // Upstream generateToonTrack:
    // For each point:
    // 1. turn (0.2s)
    // 2. move (pointTime * speedMult, where pointTime = distance * 0.25)
    // 3. optional emote (2.0s)
    // We compute total path loop time
    float totalLoopTime = 0.0f;
    float segStartTimes[16] = {};
    float segMoveTimes[16] = {};
    int numSegs = std::min(nPoints, 16);

    for (int i = 0; i < numSegs; ++i) {
        segStartTimes[i] = totalLoopTime;
        int next = (i + 1) % nPoints;
        float dx = pathInfo.points[next].x - pathInfo.points[i].x;
        float dy = pathInfo.points[next].y - pathInfo.points[i].y;
        float dz = pathInfo.points[next].z - pathInfo.points[i].z;
        float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        float moveT = dist * 0.25f;
        if (moveT < 1.0f) moveT = 1.0f;
        segMoveTimes[i] = moveT;
        // turn 0.2s + move + 2.0s emote
        totalLoopTime += 0.2f + moveT + 2.0f;
    }

    if (totalLoopTime <= 0.0f) totalLoopTime = 10.0f;
    float t = std::fmod(gameTime, totalLoopTime);
    if (t < 0.0f) t += totalLoopTime;

    // Find active segment
    int curSeg = 0;
    for (int i = numSegs - 1; i >= 0; --i) {
        if (t >= segStartTimes[i]) {
            curSeg = i;
            break;
        }
    }

    int nextSeg = (curSeg + 1) % nPoints;
    const auto& pA = pathInfo.points[curSeg];
    const auto& pB = pathInfo.points[nextSeg];
    float segT = t - segStartTimes[curSeg];
    float moveDuration = segMoveTimes[curSeg];

    // Heading from A to B:
    float dx = pB.x - pA.x;
    float dy = pB.y - pA.y;
    float heading = std::atan2(-dx, dy) * 57.2957795f;
    if (heading < 0.0f) heading += 360.0f;
    outPose.h = heading;

    // Default animation options for this anim set
    int animSet = pathInfo.animSetIndex;
    const auto& pSet = data->poseSets[animSet];
    const char* emoteName = (pSet.numPoses > 0 && pSet.poses[0].name) ? pSet.poses[0].name : "wave";

    if (segT < 0.2f) {
        // Turning
        outPose.x = pA.x;
        outPose.y = pA.y;
        outPose.z = pA.z;
        std::strcpy(outPose.poseName, "walk");
        outPose.interest = 1.0f;
    } else if (segT < 0.2f + moveDuration) {
        // Moving along path
        float frac = (segT - 0.2f) / moveDuration;
        outPose.x = pA.x + dx * frac;
        outPose.y = pA.y + dy * frac;
        outPose.z = pA.z + (pB.z - pA.z) * frac;
        std::strcpy(outPose.poseName, (curSeg % 2 == 0) ? "walk" : "run");
        outPose.interest = 1.0f;
    } else {
        // Playing emote at waypoint
        outPose.x = pB.x;
        outPose.y = pB.y;
        outPose.z = pB.z;
        std::strncpy(outPose.poseName, emoteName, sizeof(outPose.poseName) - 1);
        outPose.poseName[sizeof(outPose.poseName) - 1] = '\0';
        outPose.interest = 2.0f;
    }
}

PhotoShotResult Simulation::evaluateShot(int playerIdx, float camX, float camY, float camZ, float camH, float camP, float gameTime) {
    PhotoShotResult res;
    if (playerIdx < 0 || playerIdx >= numPlayers || isDone) {
        return res;
    }

    filmUsed[playerIdx]++;
    if (filmUsed[playerIdx] >= maxFilm) {
        calculateScores();
        isDone = true;
    }

    const auto* data = getHoodPhotoData(safezoneId);
    if (!data || data->numPaths == 0) return res;

    float radH = camH * 0.0174532925f;
    float radP = camP * 0.0174532925f;
    float camFwdX = -std::sin(radH) * std::cos(radP);
    float camFwdY =  std::cos(radH) * std::cos(radP);
    float camFwdZ =  std::sin(radP);

    int bestSubject = -1;
    float bestQualityDist = 999.0f;
    SubjectPose bestPose;
    int bestScore = 0;

    for (int s = 0; s < data->numPaths; ++s) {
        SubjectPose pose;
        getSubjectPose(s, gameTime, pose);

        float dx = pose.x - camX;
        float dy = pose.y - camY;
        float dz = (pose.z + 2.5f) - camZ;
        float dotFwd = dx * camFwdX + dy * camFwdY + dz * camFwdZ;

        if (dotFwd > 5.0f && dotFwd < 150.0f) {
            float distSq = dx * dx + dy * dy + dz * dz;
            float perpSq = distSq - dotFwd * dotFwd;
            if (perpSq < 0.0f) perpSq = 0.0f;
            float perp = std::sqrt(perpSq);

            // Bounding cone check (within viewfinder angle ~15 degrees)
            float maxPerp = dotFwd * 0.35f;
            if (perp < maxPerp && perp < bestQualityDist) {
                bestQualityDist = perp;
                bestSubject = s;
                bestPose = pose;

                // Upstream quality
                int quality = 1;
                if (perp < 2.0f) quality = 15;
                else if (perp < 4.5f) quality = 10;
                else if (perp < 8.0f) quality = 5;

                // Upstream angle:
                float a1 = std::fmod(camH, 360.0f);
                if (a1 < 0.0f) a1 += 360.0f;
                float a2 = std::fmod(pose.h, 360.0f);
                if (a2 < 0.0f) a2 += 360.0f;
                float angle = std::fabs(std::fmod(a1 + 180.0f - a2, 360.0f) - 180.0f);

                int score = 0;
                if (angle >= 135.0f) score += 2;
                else if (angle >= 90.0f) score += 1;
                else if (angle <= 60.0f) score -= 1;

                score += static_cast<int>(pose.interest);

                if (quality >= 5) {
                    score += 1;
                    if (quality >= 10) {
                        score += 1;
                        if (quality >= 15) {
                            score += 1;
                        }
                    }
                }
                score -= 2;
                if (score > kNumStars) score = kNumStars;
                if (score <= 0) score = 1;

                bestScore = score;
            }
        }
    }

    if (bestSubject >= 0) {
        res.hitSubject = true;
        res.subjectIndex = bestSubject;
        const size_t poseLength = std::strlen(bestPose.poseName);
        const size_t copyLength = std::min(poseLength, sizeof(res.poseName) - 1);
        std::memcpy(res.poseName, bestPose.poseName, copyLength);
        res.poseName[copyLength] = '\0';
        res.stars = static_cast<float>(bestScore);

        // Check matching assignment
        for (int a = 0; a < kOnscreenAssignments; ++a) {
            if (assignments[a].subjectIndex == bestSubject &&
                std::strcmp(assignments[a].poseName, bestPose.poseName) == 0) {
                res.assignmentIndex = a;
                if (res.stars > assignments[a].playerScores[playerIdx]) {
                    assignments[a].playerScores[playerIdx] = res.stars;
                    int high = assignments[a].highScorer;
                    if (high == -1 || res.stars > assignments[a].playerScores[high]) {
                        assignments[a].highScorer = playerIdx;
                    }
                    res.newHighScore = true;
                }
                break;
            }
        }
    }

    return res;
}

bool Simulation::submitAIScore(uint32_t avId, int playerIdx, int assignmentIdx, float stars) {
    (void)avId;
    if (assignmentIdx < 0 || assignmentIdx >= kOnscreenAssignments) return false;
    if (playerIdx < 0 || playerIdx >= numPlayers) playerIdx = 0;

    if (stars > assignments[assignmentIdx].playerScores[playerIdx]) {
        assignments[assignmentIdx].playerScores[playerIdx] = stars;
        int high = assignments[assignmentIdx].highScorer;
        if (high == -1 || stars > assignments[assignmentIdx].playerScores[high]) {
            assignments[assignmentIdx].highScorer = playerIdx;
        }
        calculateScores();
        return true;
    }
    return false;
}

void Simulation::calculateScores() {
    float playerBonus[kMaxPlayers] = {};
    float teamScore = 0.0f;

    for (int a = 0; a < kOnscreenAssignments; ++a) {
        int high = assignments[a].highScorer;
        if (high != -1) {
            float highestScore = assignments[a].playerScores[high];
            teamScore += highestScore;
            playerBonus[high] += 1.0f;
        }
    }

    for (int i = 0; i < numPlayers; ++i) {
        scores[i] = static_cast<int>(std::round(teamScore + playerBonus[i]));
    }
}

} // namespace tt::photogame
