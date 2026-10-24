#include "game/maze_logic.h"
#include <cmath>
#include <algorithm>

namespace tt::game::maze {

const uint8_t kCollisionTable[kHeight][kWidth] = {
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
    {1, 1, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 1, 1},
    {1, 1, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 1, 1},
    {1, 1, 1, 0, 0, 1, 0, 0, 1, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 1, 0, 0, 1, 0, 0, 1, 1, 1},
    {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1},
    {1, 0, 0, 1, 1, 1, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 0, 0, 1, 1, 1, 0, 0, 1},
    {1, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 1},
    {1, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 1},
    {1, 0, 0, 1, 0, 0, 1, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 1, 0, 0, 1, 0, 0, 1},
    {1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1},
    {1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1},
    {1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 1, 1, 1, 0, 0, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 0, 0, 1, 1, 1, 1},
    {1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
};

const TreasurePos kTreasures[kNumTreasures] = {
    {-20, -18}, {-18, -18}, {-16, -18}, {-14, -18}, {-8, -18}, {-6, -18}, {-4, -18}, {4, -18},
    {6, -18}, {8, -18}, {14, -18}, {16, -18}, {18, -18}, {20, -18}, {-20, -16}, {-14, -16},
    {-4, -16}, {4, -16}, {14, -16}, {20, -16}, {-20, -14}, {-14, -14}, {-4, -14}, {4, -14},
    {14, -14}, {20, -14}, {-24, -12}, {-22, -12}, {-20, -12}, {-14, -12}, {-12, -12}, {-10, -12},
    {-8, -12}, {-6, -12}, {-4, -12}, {-2, -12}, {0, -12}, {2, -12}, {4, -12}, {6, -12},
    {8, -12}, {10, -12}, {12, -12}, {14, -12}, {20, -12}, {22, -12}, {24, -12}, {-24, -10},
    {-14, -10}, {-8, -10}, {0, -10}, {8, -10}, {14, -10}, {24, -10}, {-24, -8}, {-14, -8},
    {-8, -8}, {0, -8}, {8, -8}, {14, -8}, {24, -8}, {-24, -6}, {-18, -6}, {-16, -6},
    {-14, -6}, {-8, -6}, {0, -6}, {8, -6}, {14, -6}, {16, -6}, {18, -6}, {24, -6},
    {-24, -4}, {-18, -4}, {-8, -4}, {8, -4}, {18, -4}, {24, -4}, {-24, -2}, {-18, -2},
    {-8, -2}, {8, -2}, {18, -2}, {24, -2}, {-24, 0}, {-18, 0}, {-16, 0}, {-14, 0},
    {-12, 0}, {-10, 0}, {-8, 0}, {-6, 0}, {6, 0}, {8, 0}, {10, 0}, {12, 0},
    {14, 0}, {16, 0}, {18, 0}, {24, 0}, {-24, 2}, {-12, 2}, {-6, 2}, {6, 2},
    {12, 2}, {24, 2}, {-24, 4}, {-12, 4}, {-6, 4}, {6, 4}, {12, 4}, {24, 4},
    {-24, 6}, {-22, 6}, {-20, 6}, {-18, 6}, {-16, 6}, {-14, 6}, {-12, 6}, {-6, 6},
    {-4, 6}, {-2, 6}, {0, 6}, {2, 6}, {4, 6}, {6, 6}, {12, 6}, {14, 6},
    {16, 6}, {18, 6}, {20, 6}, {22, 6}, {24, 6}, {-18, 8}, {-12, 8}, {0, 8},
    {12, 8}, {18, 8}, {-18, 10}, {-12, 10}, {0, 10}, {12, 10}, {18, 10}, {-24, 12},
    {-22, 12}, {-20, 12}, {-18, 12}, {-12, 12}, {-10, 12}, {-8, 12}, {-6, 12}, {-4, 12},
    {-2, 12}, {0, 12}, {2, 12}, {4, 12}, {6, 12}, {8, 12}, {10, 12}, {12, 12},
    {18, 12}, {20, 12}, {22, 12}, {24, 12}, {-24, 14}, {-8, 14}, {8, 14}, {24, 14},
    {-24, 16}, {-8, 16}, {8, 16}, {24, 16}, {-24, 18}, {-22, 18}, {-20, 18}, {-18, 18},
    {-16, 18}, {-14, 18}, {-12, 18}, {-10, 18}, {-8, 18}, {-6, 18}, {-4, 18}, {-2, 18},
    {0, 18}, {2, 18}, {4, 18}, {6, 18}, {8, 18}, {10, 18}, {12, 18}, {14, 18},
    {16, 18}, {18, 18}, {20, 18}, {22, 18}, {24, 18},
};

void worldToTile(float x, float y, int& tx, int& ty) {
    tx = int(std::floor(x / kCellWidth + kOriginTX));
    ty = int(std::floor(y / kCellWidth + kOriginTY));
}

void tileToWorld(int tx, int ty, float& x, float& y) {
    x = (tx - kOriginTX) * kCellWidth;
    y = (ty - kOriginTY) * kCellWidth;
}

bool isWalkable(int tx, int ty) {
    if (tx <= 0 || ty <= 0 || tx >= kWidth || ty >= kHeight) return false;
    return !kCollisionTable[ty][tx] &&
           !kCollisionTable[ty - 1][tx] &&
           !kCollisionTable[ty][tx - 1] &&
           !kCollisionTable[ty - 1][tx - 1];
}

static float calcFlushCoord(int curTile, int newTile, int centerTile) {
    constexpr float kEpsilon = 0.01f;
    if (newTile > curTile) {
        return (newTile - centerTile) * kCellWidth - kEpsilon - kWallOffset;
    } else {
        return (curTile - centerTile) * kCellWidth + kWallOffset;
    }
}

void doOrthoCollisions(float oldX, float oldY, float newX, float newY, float& outX, float& outY) {
    float offsetX = newX - oldX;
    float offsetY = newY - oldY;
    const float curX = oldX;
    const float curY = oldY;
    int curTX, curTY;
    worldToTile(curX, curY, curTX, curTY);

    const float wallOffX = (offsetX < 0) ? -kWallOffset : kWallOffset;
    const float wallOffY = (offsetY < 0) ? -kWallOffset : kWallOffset;

    // Test X motion
    float testX = curX + offsetX + wallOffX;
    float testY = curY;
    int nTX, nTY;
    worldToTile(testX, testY, nTX, nTY);
    if (nTX != curTX && nTY >= 0 && nTY < kHeight && nTX >= 0 && nTX < kWidth) {
        if (kCollisionTable[nTY][nTX] == 1) {
            offsetX = calcFlushCoord(curTX, nTX, kOriginTX) - curX;
        }
    }

    // Test Y motion
    testX = curX;
    testY = curY + offsetY + wallOffY;
    worldToTile(testX, testY, nTX, nTY);
    if (nTY != curTY && nTY >= 0 && nTY < kHeight && nTX >= 0 && nTX < kWidth) {
        if (kCollisionTable[nTY][nTX] == 1) {
            offsetY = calcFlushCoord(curTY, nTY, kOriginTY) - curY;
        }
    }

    // Test corner / diagonal motion
    testX = curX + offsetX + wallOffX;
    testY = curY + offsetY + wallOffY;
    worldToTile(testX, testY, nTX, nTY);
    if (nTY >= 0 && nTY < kHeight && nTX >= 0 && nTX < kWidth && kCollisionTable[nTY][nTX] == 1) {
        const float cX = calcFlushCoord(curTX, nTX, kOriginTX);
        const float cY = calcFlushCoord(curTY, nTY, kOriginTY);
        if (std::abs(cX - curX) < std::abs(cY - curY)) {
            offsetX = cX - curX;
        } else {
            offsetY = cY - curY;
        }
    }

    outX = curX + offsetX;
    outY = curY + offsetY;
}

int calculateScore(int treasuresTaken, bool allTaken) {
    int score = treasuresTaken / 12;
    if (score < 1) score = 1;
    if (allTaken) score += 8;
    return score;
}

static const Direction kOpposite[4] = {DirDown, DirUp, DirRight, DirLeft};
static const float kDirectionHs[4] = {0.0f, 180.0f, 90.0f, 270.0f};
static const int kDirDX[4] = {0, 0, -1, 1};
static const int kDirDY[4] = {1, -1, 0, 0};

void initSuits(SuitState suits[kNumSuits]) {
    const int startTiles[kNumSuits][2] = {
        {7, 5}, {21, 16}, {7, 16}, {21, 5}
    };
    for (int i = 0; i < kNumSuits; ++i) {
        suits[i].tileX = startTiles[i][0];
        suits[i].tileY = startTiles[i][1];
        tileToWorld(suits[i].tileX, suits[i].tileY, suits[i].x, suits[i].y);
        suits[i].dir = (Direction)(i % 4);
        suits[i].heading = kDirectionHs[suits[i].dir];
        tileToWorld(suits[i].tileX + kDirDX[suits[i].dir], suits[i].tileY + kDirDY[suits[i].dir],
                    suits[i].targetX, suits[i].targetY);
    }
}

void updateSuit(SuitState& suit, float dt, float speed) {
    const float dx = suit.targetX - suit.x;
    const float dy = suit.targetY - suit.y;
    const float dist = std::sqrt(dx * dx + dy * dy);
    const float step = speed * dt;

    if (dist <= step || dist < 0.001f) {
        suit.x = suit.targetX;
        suit.y = suit.targetY;
        worldToTile(suit.x, suit.y, suit.tileX, suit.tileY);

        // Pick next direction
        Direction forward = suit.dir;
        Direction reverse = kOpposite[suit.dir];
        Direction candidates[4];
        int numCand = 0;

        for (int d = 0; d < 4; ++d) {
            const int ntx = suit.tileX + kDirDX[d];
            const int nty = suit.tileY + kDirDY[d];
            if (isWalkable(ntx, nty)) {
                candidates[numCand++] = (Direction)d;
            }
        }

        Direction nextDir = reverse;
        if (numCand > 0) {
            bool canGoForward = false;
            for (int i = 0; i < numCand; ++i) {
                if (candidates[i] == forward) { canGoForward = true; break; }
            }
            if (canGoForward && numCand > 1 && (suit.tileX + suit.tileY) % 3 != 0) {
                nextDir = forward;
            } else {
                nextDir = candidates[(suit.tileX * 7 + suit.tileY * 13) % numCand];
            }
        }

        suit.dir = nextDir;
        suit.heading = kDirectionHs[suit.dir];
        tileToWorld(suit.tileX + kDirDX[suit.dir], suit.tileY + kDirDY[suit.dir],
                    suit.targetX, suit.targetY);
    } else {
        suit.x += (dx / dist) * step;
        suit.y += (dy / dist) * step;
    }
}

} // namespace tt::game::maze

