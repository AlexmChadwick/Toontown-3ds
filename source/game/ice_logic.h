// Ice minigame pure logic (no 3DS headers). Puck/tire sliding physics and distance scoring.
#pragma once

#include <cmath>
#include <cstdint>

namespace tt::icegame {

constexpr float kMinWallX = -20.0f;
constexpr float kMaxWallX = 20.0f;
constexpr float kMinWallY = -15.0f;
constexpr float kMaxWallY = 15.0f;
constexpr float kTireRadius = 1.5f;
constexpr float kBoundX = 18.5f; // 20 - 1.5
constexpr float kBoundY = 13.5f; // 15 - 1.5
constexpr float kFarthestLength = 22.90196498f; // sqrt(18.5^2 + 13.5^2)
constexpr int kPointsInCorner = 1;
constexpr int kNumMatches = 3;
constexpr int kNumRounds = 2;

struct Tire {
    float x = -17.5f;
    float y = -12.5f;
    float vx = 0.0f;
    float vy = 0.0f;
    bool moving = false;

    void launch(float force0to100, float headingDeg);
    void step(float dt);
    float distanceToCenter() const {
        return std::sqrt(x * x + y * y);
    }
};

int calculatePoints(float dist, int numPlayers, int place);

struct Simulation {
    Tire tires[4];
    int numPlayers = 1;
    int curMatch = 0;
    int curRound = 0;
    int scores[4] = {};
    bool isDone = false;

    void init(int players);
    void resetRound();
    void setChoice(int playerIndex, float force, float headingDeg);
    void stepPhysics(float dt);
    bool allTiresStopped() const;
    void scoreMatch();
};

} // namespace tt::icegame
