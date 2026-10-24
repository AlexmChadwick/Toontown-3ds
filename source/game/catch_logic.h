// Catching minigame (DistributedCatchGame) rules and schedule, with no libctru dependency so
// tools/catch_test can compare it with the upstream Python on the host.
//
// Everything here is a port of toontown/minigame: DistributedCatchGame.py
// (calcDifficultyConstants, scheduleDrops, startSuitWalkTask), DropScheduler.py,
// DropPlacer.py (RegionDropPlacer), CatchGameGlobals.py and Trajectory.py, plus Panda's
// RandomNumGen (a MT19937 `Mersenne`). The drop schedule and the suit walks are a pure
// function of (doId, hood, number of players, difficulty), so a multiplayer game on the 3DS
// and one on a PC client agree on every object.
#pragma once

#include <cstdint>

namespace tt::game::catchgame {

// Panda3D RandomNumGen: Mersenne(seed).getUint31() == mt19937(seed)() >> 1 (checked against
// the stock wheel in tools/catch_test).
class RandomNumGen {
public:
    explicit RandomNumGen(uint32_t seed) { seedMt(seed); }
    // RandomNumGen(otherRng): seeded with other.randint(0, 1 << 16).
    explicit RandomNumGen(RandomNumGen& parent) { seedMt(parent.randint(0, 1 << 16)); }
    uint32_t getUint31();
    uint32_t rand(uint32_t n) { return uint32_t((uint64_t(getUint31()) * n) >> 31); } // [0, n)
    int choice(int n) { return int(rand(uint32_t(n))); }                              // index into a list of n
    int randrange(int start, int stop) { return start + int(rand(uint32_t(stop - start))); }
    int randint(int a, int b) { return a + int(rand(uint32_t(b - a + 1))); }          // [a, b]
    double random() { return double(getUint31()) / double(1u << 31); }                // [0, 1)
    template <typename T> void shuffle(T* x, int n) {
        for (int i = n - 1; i > 0; --i) {
            const int j = int(rand(uint32_t(i + 1)));
            const T tmp = x[i];
            x[i] = x[j];
            x[j] = tmp;
        }
    }

private:
    void seedMt(uint32_t seed);
    void generate();
    uint32_t mt_[624];
    int idx_ = 624;
};

// CatchGameGlobals.DropObjectTypes (name order = Name2DOTypeId order, which is alphabetical).
enum DropType : uint8_t { kAnvil = 0, kApple, kCoconut, kOrange, kPear, kPineapple, kWatermelon, kNumDropTypes };
const char* dropTypeName(int type);
bool dropTypeGood(int type);      // everything but the anvil
// The fruit of a hood (defineConstants fruits table); apple for unknown hoods.
DropType fruitOfHood(uint32_t hood);

constexpr double kGameDuration = 55.0;   // CatchGameGlobals.GameDuration
constexpr int kMaxDrops = 160;           // 4 players in Donald's Dreamland drop about 123
constexpr int kMaxSuitWalks = 24;        // 55 s / 3 s period, plus one
constexpr double kObjRadius = 1.0;       // DistributedCatchGame.ObjRadius

struct Drop {
    double t;        // game time at which the object starts to fall (DropPlacer drop[0])
    uint8_t type;    // DropType: the fruit of the hood or the anvil
    uint8_t gx, gy;  // dropCoords as returned ([row, col]); upstream passes them to
                     // grid2world(column, row) unswapped, which grid2world() mirrors
    double spin;     // the randomNumGen.random() getDropIval draws when the object spawns
};

struct SuitWalk {
    double delay;    // game time the cog starts walking
    double sx, sy;   // start and end on the stage plane
    double ex, ey;
    double duration; // |end - start| / SuitSpeed
    int8_t kind;     // which of the four cog models was picked (rng.choice), -1 when none was free
};

class Schedule {
public:
    // doId seeds the RandomNumGen (DistributedMinigame.__createRandomNumGen). difficulty is
    // MinigameGlobals.getDifficulty(hood) unless the DO carries an override.
    void init(uint32_t doId, uint32_t hood, int numPlayers, double difficulty);

    uint32_t hood = 0;
    int numPlayers = 1;
    double difficulty = 0;
    // calcDifficultyConstants
    double toonSpeed = 0, suitSpeed = 0;
    double suitPeriod0 = 0;                 // SuitPeriodRange[0] (the walk period; [1] is unused upstream)
    double stageHalfWidth = 0, stageHalfHeight = 0;
    double minOffscreenHeight = 0;
    double baselineOnscreen = 0;            // BaselineOnscreenDropDuration
    double offscreenTime = 0;
    double baselineDropDuration = 0;
    double dropPeriod = 0;
    int totalDrops = 0, numFruits = 0, numAnvils = 0;
    int dropRows = 5, dropCols = 5;
    int serverFruits = 0;                   // CatchGameGlobals.NumFruits: what the AI calls a perfect game
    DropType fruit = kApple;

    int numDrops = 0;
    Drop drops[kMaxDrops];
    int numSuitWalks = 0;
    SuitWalk suitWalks[kMaxSuitWalks];

    // grid2world(column, row): drop grid cell to stage coordinates.
    void grid2world(int column, int row, double* x, double* y) const;
    // Trajectory: height of an object of `type` `elapsed` seconds after it was spawned
    // (offscreenTime of hovering at minOffscreenHeight, then a parabola down to 0 over the
    // type's onscreen duration). < 0 once it has landed.
    double objectZ(int type, double elapsed) const;
    double onscreenDuration(int type) const { return type == kAnvil ? 0.4 * baselineOnscreen : baselineOnscreen; }
    double fallDuration(int type) const { return onscreenDuration(type) + offscreenTime; }
};

// What the AI will pay for fruitsCaught fruits (DistributedCatchGameAI.gameOver, before the
// trolley's hood multiplier): max(1, int(score / 2)) with score = 1 + fruits, +round(numFruits
// / 4) for a perfect game. The client only uses this for the result banner.
int expectedScore(int fruitsCaught, int serverFruits);

} // namespace tt::game::catchgame
