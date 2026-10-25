#pragma once
#include "game/catch_logic.h"
#include "world/collision.h"
namespace tt::world {
// CogdoMazeGameGlobals: 3×3 quadrants, 16 cells + one-cell separators, 3-unit cells.
constexpr int kCogdoMazeSize = 52;
constexpr int kCogdoMazeSuits = 64;
// MinigameGlobals.NoDifficultyOverride and DifficultyOverrideMult.
constexpr int32_t kCogdoNoDifficultyOverride = 2147483647;
float cogdoDifficulty(int32_t overrideValue, uint32_t exterior, int32_t playground);
struct CogdoMazeCooler {
    Vec3 position;
    Vec3 trigger;
    float heading;
};
struct CogdoMazeSuit {
    game::catchgame::RandomNumGen random{0};
    int x = 0, y = 0, nextX = 0, nextY = 0;
    int nextTick = 0;
    int period = 192;
    int dropTimer = 1; // gameStart emits the first ThinkEvent.
    int lastTick = 0;
    uint8_t direction = 1;
    uint8_t type = 0;
    bool alive = false;
    bool previousOccupied = false;
    Vec3 position{};
    float heading = 180;
};
struct CogdoMazeDrop {
    Vec3 position{};
    int tick = -10000;
};
class CogdoMaze {
public:
    bool load(uint32_t seed, const uint8_t counts[3], float difficulty,
              const char *directory = "sdmc:/3ds/toontown3ds/data/cogdo/");
    void update(float seconds, const uint8_t hp[kCogdoMazeSuits]);
    bool accessible(int x, int y) const;
    bool walkable(int x, int y) const;
    void slide(Vec3 previous, Vec3 *current) const;
    bool route(Vec3 from, Vec3 destination, Vec3 *next) const;
    static Vec3 tile(int x, int y);
    static void cell(Vec3 point, int *x, int *y);
    uint8_t quadrant[9] = {};
    uint8_t angle[9] = {};
    uint8_t barriers[12] = {};
    uint8_t cells[kCogdoMazeSize][kCogdoMazeSize] = {};
    CogdoMazeSuit suits[kCogdoMazeSuits];
    int numSuits = 0;
    CogdoMazeCooler coolers[64] = {};
    int numCoolers = 0;
    CogdoMazeDrop drops[16];
    int nextDrop = 0;

private:
    bool occupied(int x, int y, int except) const;
    bool initializeSuits(game::catchgame::RandomNumGen &parent, const uint8_t counts[3]);
    void think(int index, int tick);
    // Fixed scratch stays off the small ARM11 main-thread stack. Loading and
    // diagnostic routing are main-thread operations, never concurrent.
    uint8_t tables_[9][4][16][16] = {};
    mutable int16_t routeParent_[kCogdoMazeSize * kCogdoMazeSize] = {};
    mutable uint16_t routeQueue_[kCogdoMazeSize * kCogdoMazeSize] = {};
    float difficulty_ = 0;
};
} // namespace tt::world
