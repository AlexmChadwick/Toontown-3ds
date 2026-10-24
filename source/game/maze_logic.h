#pragma once

#include <cstdint>

namespace tt::game::maze {

constexpr int kWidth = 28;
constexpr int kHeight = 22;
constexpr int kOriginTX = 14;
constexpr int kOriginTY = 11;
constexpr float kCellWidth = 2.0f;
constexpr float kWallOffset = 1.0f;
constexpr int kNumTreasures = 197;
constexpr int kNumSuits = 4;
constexpr float kGameDuration = 60.0f;
constexpr float kTreasureGrabRadius = 1.2f;
constexpr float kSuitHitRadius = 1.6f;

struct TreasurePos {
    int8_t x;
    int8_t y;
};

// Collision table (1 = wall, 0 = open)
extern const uint8_t kCollisionTable[kHeight][kWidth];
// Treasures in 1-player maze
extern const TreasurePos kTreasures[kNumTreasures];

// Ortho collision sliding matching MazeBase.py:doOrthoCollisions
void doOrthoCollisions(float oldX, float oldY, float newX, float newY, float& outX, float& outY);

// Tile conversions
void worldToTile(float x, float y, int& tx, int& ty);
void tileToWorld(int tx, int ty, float& x, float& y);
bool isWalkable(int tx, int ty);

// Score formula matching DistributedMazeGameAI.py
int calculateScore(int treasuresTaken, bool allTaken);

enum Direction : uint8_t {
    DirUp = 0,
    DirDown = 1,
    DirLeft = 2,
    DirRight = 3,
};

struct SuitState {
    float x;
    float y;
    float heading;
    Direction dir;
    int tileX;
    int tileY;
    float targetX;
    float targetY;
};

void initSuits(SuitState suits[kNumSuits]);
void updateSuit(SuitState& suit, float dt, float speed = 4.0f);

} // namespace tt::game::maze
