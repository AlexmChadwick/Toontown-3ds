#pragma once

#include <cstdint>

#include "game/maze_tables.h"
#include "game/panda_rng.h"

namespace tt::game::maze {

constexpr float kWallOffset = 1.0f;
constexpr int kMaxTreasures = 670; // maze_4player
// Against GravityWalker's event sphere (r 1.456, centre 1.3 up), in the ground plane: MazeTreasure
// (r 0.7 at z 1) and MazeSuit (r 2.0 at z 0.1).
constexpr float kTreasureGrabRadius = 2.14f;
constexpr float kSuitHitRadius = 3.24f;
constexpr float kToonSpeed = 8.0f;   // DistributedMazeGame.TOON_SPEED
constexpr float kSuitZ = 0.1f;       // MazeSuit.SUIT_Z
constexpr float kDefaultSuitSpeed = 4.0f;

struct TreasurePos {
    int8_t x;
    int8_t y;
};

// The maze in play: MazeData.mazeNames[numPlayers - 1] (DistributedMazeGame.load). 1 by default.
void setMaze(int numPlayers);
const MazeInfo& maze();

// MazeBase
void worldToTile(float x, float y, int& tx, int& ty);
void tileToWorld(int tx, int ty, float& x, float& y);
struct Tile {
    int x, y;
};
bool isWalkable(int tx, int ty, const Tile* reject = nullptr, int numReject = 0);
// MazeBase.doOrthoCollisions
void doOrthoCollisions(float oldX, float oldY, float newX, float newY, float& outX, float& outY);

// Score formula matching DistributedMazeGameAI.py
int calculateScore(int treasuresTaken, bool allTaken);

enum Direction : uint8_t {
    DirUp = 0,
    DirDown = 1,
    DirLeft = 2,
    DirRight = 3,
};

// MazeSuit: walks cell to cell on SUIT_TIC_FREQ tics, each choice from its own RandomNumGen, so
// every client sees the same Cogs from (doId, numPlayers, hood).
class Suit {
public:
    void init(int serialNum, RandomNumGen& parent, int cellWalkPeriod, double difficulty);
    void onstage();
    void gameStart();
    // getThinkTimestampTics: tics in [nextThinkTic, curTic] every ticPeriod; count, last in *last.
    int thinkTics(int curTic, int* first) const;
    void prepareToThink() {
        occupied_[0] = {nextTX_, nextTY_};
        numOccupied_ = 1;
    }
    void think(int curTic, float curT, const Tile* unwalkables, int numUnwalkables, bool render);
    void update(float curT); // the move / turn lerps
    const Tile* occupied(int* n) const {
        *n = numOccupied_;
        return occupied_;
    }
    int ticPeriod() const { return ticPeriod_; }
    int tileX() const { return TX_; }
    int tileY() const { return TY_; }
    int direction() const { return direction_; }

    float x = 0, y = 0, h = 180;
    bool walking = false;
    float playRate = 1.0f; // walk cycle rate (cellWidth / cellWalkDuration / DEFAULT_SPEED)

private:
    Direction chooseNewWalkDirection(const Tile* unwalkables, int n);
    RandomNumGen rng_;
    int serial_ = 0;
    int ticPeriod_ = 64;
    float cellWalkDuration_ = 0.25f;
    float turnDuration_ = 0.15f;
    double difficulty_ = 0;
    int startTX_ = 0, startTY_ = 0;
    int TX_ = 0, TY_ = 0, nextTX_ = 0, nextTY_ = 0;
    Direction direction_ = DirDown, lastDirection_ = DirDown;
    int nextThinkTic_ = 0;
    Tile occupied_[2] = {};
    int numOccupied_ = 0;
    // The rendered LerpPosInterval (and LerpHprInterval when the heading changes).
    bool moving_ = false;
    float moveStart_ = 0;
    float fromX_ = 0, fromY_ = 0, toX_ = 0, toY_ = 0;
    bool turning_ = false;
    float fromH_ = 0, toH_ = 0;
};

// DistributedMazeGame.__updateSuitsTask over every suit.
void thinkSuits(Suit* suits, int numSuits, float curT);

} // namespace tt::game::maze
