#include "game/catch_logic.h"

#include <cmath>

#include "game/catch_tables.h"

namespace tt::game::catchgame {

// ---------------------------------------------------------------- RandomNumGen (MT19937)
void RandomNumGen::seedMt(uint32_t seed) {
    mt_[0] = seed;
    for (int i = 1; i < 624; ++i) mt_[i] = 1812433253u * (mt_[i - 1] ^ (mt_[i - 1] >> 30)) + uint32_t(i);
    idx_ = 624;
}

void RandomNumGen::generate() {
    for (int i = 0; i < 624; ++i) {
        const uint32_t y = (mt_[i] & 0x80000000u) | (mt_[(i + 1) % 624] & 0x7fffffffu);
        uint32_t v = mt_[(i + 397) % 624] ^ (y >> 1);
        if (y & 1u) v ^= 0x9908b0dfu;
        mt_[i] = v;
    }
    idx_ = 0;
}

uint32_t RandomNumGen::getUint31() {
    if (idx_ >= 624) generate();
    uint32_t y = mt_[idx_++];
    y ^= y >> 11;
    y ^= (y << 7) & 0x9d2c5680u;
    y ^= (y << 15) & 0xefc60000u;
    y ^= y >> 18;
    return y >> 1;
}

// ---------------------------------------------------------------- drop types
const char* dropTypeName(int type) {
    static const char* const names[kNumDropTypes] = {"anvil", "apple", "coconut", "orange",
                                                     "pear",  "pineapple", "watermelon"};
    return type >= 0 && type < kNumDropTypes ? names[type] : "?";
}

bool dropTypeGood(int type) { return type != kAnvil; }

DropType fruitOfHood(uint32_t hood) {
    switch (hood) {
    case 1000: return kOrange;     // Donald's Dock
    case 5000: return kPear;       // Daisy Gardens
    case 4000: return kCoconut;    // Minnie's Melodyland
    case 3000: return kWatermelon; // The Brrrgh
    case 9000: return kPineapple;  // Donald's Dreamland
    default: return kApple;        // Toontown Central
    }
}

namespace {

double lerp(double a, double b, double t) { return a + (b - a) * t; }

// DropScheduler.
struct Scheduler {
    double firstDropDelay, dropPeriod, maxDropDuration, fasterDropDelay, fasterDropPeriodMult;
    double curT;
    double period() const {
        double d = dropPeriod;
        if (curT >= fasterDropDelay) d *= fasterDropPeriodMult;
        return d;
    }
    bool doneDropping() const {
        const double landTime = curT + maxDropDuration;
        const double maxTime = kGameDuration + period();
        return landTime >= maxTime;
    }
    void step() { curT += period(); }
};

// DropPlacer.RegionDropPlacer for one game.
struct RegionPlacer {
    const catchtab::DropTable* table;
    int numRegions;                                  // DropRegions = sorted ids 0..numRegions-1
    uint8_t cells[catchtab::kMaxRegions][catchtab::kMaxTableDim * catchtab::kMaxTableDim][2];
    int cellCount[catchtab::kMaxRegions];
    int empty[catchtab::kMaxRegions];                // emptyDropRegions, in list order
    int numEmpty;
    struct Falling { double landTime; int region; } falling[kMaxDrops];
    int fallHead, fallTail;
    int dtIndex;

    void init(int numPlayers) {
        int ti = numPlayers < catchtab::kNumPlayers2Table ? numPlayers : catchtab::kNumPlayers2Table - 1;
        table = &catchtab::kDropTables[catchtab::kPlayers2Table[ti]];
        int maxRegion = -1;
        for (int i = 0; i < catchtab::kMaxRegions; ++i) cellCount[i] = 0;
        for (int row = 0; row < table->rows; ++row) {
            for (int col = 0; col < table->cols; ++col) {
                const int region = table->region[row][col];
                if (region > maxRegion) maxRegion = region;
                const int n = cellCount[region]++;
                cells[region][n][0] = uint8_t(row);
                cells[region][n][1] = uint8_t(col);
            }
        }
        // DropRegions = keys of DropRegion2GridCoordList, sorted. The tables use consecutive ids.
        numRegions = maxRegion + 1;
        numEmpty = 0;
        for (int i = 0; i < numRegions; ++i) {
            if (cellCount[i] > 0) empty[numEmpty++] = i;
        }
        fallHead = fallTail = 0;
        dtIndex = 0;
    }

    bool isEmpty(int region) const {
        for (int i = 0; i < numEmpty; ++i) {
            if (empty[i] == region) return true;
        }
        return false;
    }

    // getNextDrop (without stepping the scheduler, which the caller does); returns the cell.
    void next(Scheduler& s, RandomNumGen& rng, double baselineDropDuration, int* row, int* col) {
        const double t = s.curT;
        while (fallHead < fallTail) {
            const Falling f = falling[fallHead];
            if (f.landTime > t) break;
            ++fallHead;
            if (!isEmpty(f.region)) empty[numEmpty++] = f.region;
        }
        int all[catchtab::kMaxRegions];
        int nAll = 0;
        for (int i = 0; i < numRegions; ++i) {
            if (cellCount[i] > 0) all[nAll++] = i;
        }
        const int* candidates = empty;
        int nCand = numEmpty;
        if (nCand == 0) {
            candidates = all;
            nCand = nAll;
        }
        const int region = candidates[rng.choice(nCand)];
        const int pick = rng.choice(cellCount[region]);
        *row = cells[region][pick][0];
        *col = cells[region][pick][1];
        falling[fallTail++] = {t + baselineDropDuration, region};
        for (int i = 0; i < numEmpty; ++i) {
            if (empty[i] == region) {
                for (int j = i; j + 1 < numEmpty; ++j) empty[j] = empty[j + 1];
                --numEmpty;
                break;
            }
        }
    }
};

} // namespace

void Schedule::init(uint32_t doId, uint32_t hoodId, int players, double diff) {
    hood = hoodId;
    numPlayers = players < 1 ? 1 : (players > 4 ? 4 : players);
    difficulty = diff;
    fruit = fruitOfHood(hood);
    int hoodIdx = 0;
    for (int i = 0; i < catchtab::kNumHoods; ++i) {
        if (catchtab::kHoods[i] == hood) hoodIdx = i;
    }
    serverFruits = catchtab::kNumFruits[numPlayers - 1][hoodIdx];

    // ---- calcDifficultyConstants ----
    toonSpeed = lerp(16.0, 25.0, difficulty);
    suitSpeed = toonSpeed / 2.0;
    suitPeriod0 = lerp(5.0, 3.0, difficulty);
    static const double areaScales[4] = {1.0, 1.0, 3.0 / 2, 4.0 / 2};
    const double areaScale = areaScales[numPlayers - 1];
    const double linearScale = std::sqrt(areaScale);
    const double dimW = std::sqrt(areaScale * 20.0 * 20.0), dimH = std::sqrt(areaScale * 15.0 * 15.0);
    stageHalfWidth = dimW / 2.0;
    stageHalfHeight = dimH / 2.0;
    static const double mohs[4] = {24, 24, 26, 28};
    minOffscreenHeight = mohs[numPlayers - 1];
    double distance = std::sqrt(dimW * dimW + dimH * dimH);
    distance /= linearScale; // RegionDropPlacer (PathDropPlacer would divide by 1.5)
    const double toonRunDuration = distance / toonSpeed;
    const double offScreenOnScreenRatio = 1.0;
    const double fraction = 1.0 / 3 * 0.85;
    baselineOnscreen = toonRunDuration / (fraction * (1.0 + offScreenOnScreenRatio));
    offscreenTime = offScreenOnScreenRatio * baselineOnscreen;
    baselineDropDuration = baselineOnscreen + offscreenTime;
    const double maxDropDuration = baselineDropDuration;
    dropPeriod = baselineDropDuration / 2.0;
    const double scaledNumPlayers = (numPlayers - 1.0) * 0.75 + 1.0;
    dropPeriod /= scaledNumPlayers;
    static const int grid[4] = {5, 5, 6, 7};
    dropRows = dropCols = grid[numPlayers - 1];

    Scheduler sched{0.5, dropPeriod, maxDropDuration, double(int(2.0 / 3 * kGameDuration)), 0.5, 0.0};
    sched.curT = sched.firstDropDelay;
    totalDrops = 0;
    while (!sched.doneDropping()) {
        sched.step();
        ++totalDrops;
    }
    // typeProbs: fruit 3, anvil 1 (float(3) / 4 == 0.75 exactly)
    numFruits = int(totalDrops * 0.75);
    numAnvils = totalDrops - numFruits;

    // ---- scheduleDrops ----
    RandomNumGen rng(doId);
    uint8_t names[kMaxDrops];
    const int total = totalDrops < kMaxDrops ? totalDrops : kMaxDrops;
    for (int i = 0; i < total; ++i) names[i] = i < numFruits ? uint8_t(fruit) : uint8_t(kAnvil);
    rng.shuffle(names, total);
    static RegionPlacer placer; // ~7 KB: keep it off the main thread's small stack
    placer.init(numPlayers);
    Scheduler ps{0.5, dropPeriod, maxDropDuration, double(int(2.0 / 3 * kGameDuration)), 0.5, 0.0};
    ps.curT = ps.firstDropDelay;
    numDrops = 0;
    while (!ps.doneDropping() && numDrops < kMaxDrops) {
        int row, col;
        placer.next(ps, rng, baselineDropDuration, &row, &col);
        Drop& d = drops[numDrops];
        d.t = ps.curT;
        // getNextDropTypeName: past the end of the list it defaults to an anvil.
        d.type = placer.dtIndex < total ? names[placer.dtIndex] : uint8_t(kAnvil);
        ++placer.dtIndex;
        d.gx = uint8_t(row);
        d.gy = uint8_t(col);
        d.spin = 0;
        ps.step();
        ++numDrops;
    }

    // ---- startSuitWalkTask: the walk sub-generator is seeded from the game's, right after
    // scheduleDrops and before any object spawns (so the spawn draws follow it) ----
    RandomNumGen suitRng(rng);
    // getDropIval draws one random() per object, in drop order, when it spawns.
    for (int i = 0; i < numDrops; ++i) drops[i].spin = rng.random();

    numSuitWalks = 0;
    double delay = 0.0;
    const double m = numPlayers == 1 ? 2.5 : (numPlayers == 2 ? 2.5 : (numPlayers == 3 ? 2.3 : 2.1));
    while (delay < kGameDuration && numSuitWalks < kMaxSuitWalks) {
        delay += lerp(suitPeriod0, suitPeriod0, suitRng.random());
        SuitWalk& w = suitWalks[numSuitWalks];
        w.delay = delay;
        w.sx = -(stageHalfWidth * m);
        w.sy = lerp(-stageHalfHeight, stageHalfHeight, suitRng.random());
        w.ex = stageHalfWidth * m;
        w.ey = lerp(-stageHalfHeight, stageHalfHeight, suitRng.random());
        if (suitRng.choice(2)) {
            const double tx = w.sx, ty = w.sy;
            w.sx = w.ex;
            w.sy = w.ey;
            w.ex = tx;
            w.ey = ty;
        }
        const double dx = w.ex - w.sx, dy = w.ey - w.sy;
        w.duration = std::sqrt(dx * dx + dy * dy) / suitSpeed;
        w.kind = -1;
        ++numSuitWalks;
    }
    // getSuitWalkIval.setup runs when each walk starts: rng.choice(self.suits) from the list of
    // cogs not currently walking; cleanup puts the cog back at the end of the list.
    uint8_t freeSuits[4] = {0, 1, 2, 3};
    int numFree = 4;
    bool started[kMaxSuitWalks] = {};
    bool finished[kMaxSuitWalks] = {};
    for (;;) {
        // next event: the earliest unstarted setup or unfinished cleanup (setup wins ties by index)
        int best = -1;
        bool isSetup = true;
        double bestT = 0;
        for (int i = 0; i < numSuitWalks; ++i) {
            if (!started[i] && (best < 0 || suitWalks[i].delay < bestT)) {
                best = i;
                isSetup = true;
                bestT = suitWalks[i].delay;
            }
            if (started[i] && !finished[i]) {
                const double te = suitWalks[i].delay + suitWalks[i].duration;
                if (best < 0 || te < bestT) {
                    best = i;
                    isSetup = false;
                    bestT = te;
                }
            }
        }
        if (best < 0) break;
        SuitWalk& w = suitWalks[best];
        if (isSetup) {
            started[best] = true;
            if (numFree == 0) {
                finished[best] = true; // no cog left: the lerp still runs, nobody walks
                continue;
            }
            const int pick = suitRng.choice(numFree);
            w.kind = int8_t(freeSuits[pick]);
            for (int j = pick; j + 1 < numFree; ++j) freeSuits[j] = freeSuits[j + 1];
            --numFree;
        } else {
            finished[best] = true;
            if (w.kind >= 0) freeSuits[numFree++] = uint8_t(w.kind);
        }
    }
}

void Schedule::grid2world(int column, int row, double* x, double* y) const {
    double fx = column / double(dropCols - 1);
    double fy = row / double(dropRows - 1);
    fx = fx * 2.0 - 1.0;
    fy = fy * 2.0 - 1.0;
    *x = fx * stageHalfWidth;
    *y = fy * stageHalfHeight;
}

double Schedule::objectZ(int type, double elapsed) const {
    // objType.trajectory = Trajectory(0, (0, 0, x_0), (0, 0, 0), gravMult = |g / 32|) with
    // g = 2 (0 - x_0) / t^2, t = onscreenDuration; setObjPos(0) holds x_0 during OffscreenTime.
    const double t = onscreenDuration(type);
    const double x0 = minOffscreenHeight;
    const double g = 2.0 * (0.0 - x0) / (t * t);
    const double gravMult = std::fabs(g / 32.0);
    const double zAcc = gravMult * -32.0;
    if (elapsed <= offscreenTime) return x0;
    const double tt = elapsed - offscreenTime;
    return x0 + 0.5 * zAcc * tt * tt;
}

int expectedScore(int fruitsCaught, int serverFruits) {
    int score = int((1 + fruitsCaught) / 2); // int(score / 2)
    if (score < 1) score = 1;
    if (fruitsCaught >= serverFruits) {
        // round(numFruits / 4.0): Python rounds half to even.
        const int q = serverFruits / 4, rem = serverFruits % 4;
        score += rem < 2 ? q : (rem > 2 ? q + 1 : (q % 2 == 0 ? q : q + 1));
    }
    return score;
}

} // namespace tt::game::catchgame
