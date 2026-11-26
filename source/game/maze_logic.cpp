#include "game/maze_logic.h"

#include <algorithm>
#include <cmath>

namespace tt::game::maze {

namespace {
const MazeInfo* gMaze = &kMazes[0];

uint8_t cell(int tx, int ty) {
    const MazeInfo& m = *gMaze;
    if (tx < 0 || ty < 0 || tx >= m.width || ty >= m.height) return 1;
    return m.collision[ty * m.width + tx];
}

float calcFlushCoord(int curTile, int newTile, int centerTile) {
    constexpr float kEpsilon = 0.01f;
    if (newTile > curTile) return (newTile - centerTile) * kCellWidth - kEpsilon - kWallOffset;
    return (curTile - centerTile) * kCellWidth + kWallOffset;
}

const Direction kOpposite[4] = {DirDown, DirUp, DirRight, DirLeft};
const float kDirectionHs[4] = {0.0f, 180.0f, 90.0f, 270.0f};

void applyDirection(Direction dir, int& tx, int& ty) {
    if (dir == DirUp) ++ty;
    else if (dir == DirDown) --ty;
    else if (dir == DirLeft) --tx;
    else ++tx;
}
} // namespace

void setMaze(int numPlayers) {
    gMaze = &kMazes[std::min(std::max(numPlayers, 1), 4) - 1];
}

const MazeInfo& maze() { return *gMaze; }

void worldToTile(float x, float y, int& tx, int& ty) {
    tx = int(std::floor(x / kCellWidth + gMaze->originTX));
    ty = int(std::floor(y / kCellWidth + gMaze->originTY));
}

void tileToWorld(int tx, int ty, float& x, float& y) {
    x = (tx - gMaze->originTX) * kCellWidth;
    y = (ty - gMaze->originTY) * kCellWidth;
}

bool isWalkable(int tx, int ty, const Tile* reject, int numReject) {
    if (tx <= 0 || ty <= 0 || tx >= gMaze->width || ty >= gMaze->height) return false;
    if (cell(tx, ty) || cell(tx, ty - 1) || cell(tx - 1, ty) || cell(tx - 1, ty - 1)) return false;
    for (int i = 0; i < numReject; ++i)
        if (reject[i].x == tx && reject[i].y == ty) return false;
    return true;
}

void doOrthoCollisions(float oldX, float oldY, float newX, float newY, float& outX, float& outY) {
    float offsetX = newX - oldX;
    float offsetY = newY - oldY;
    const float curX = oldX;
    const float curY = oldY;
    int curTX, curTY;
    worldToTile(curX, curY, curTX, curTY);
    const int ox = gMaze->originTX, oy = gMaze->originTY;

    const float wallOffX = (offsetX < 0) ? -kWallOffset : kWallOffset;
    const float wallOffY = (offsetY < 0) ? -kWallOffset : kWallOffset;

    int nTX, nTY;
    worldToTile(curX + offsetX + wallOffX, curY, nTX, nTY);
    if (nTX != curTX && cell(nTX, nTY) == 1) offsetX = calcFlushCoord(curTX, nTX, ox) - curX;

    worldToTile(curX, curY + offsetY + wallOffY, nTX, nTY);
    if (nTY != curTY && cell(nTX, nTY) == 1) offsetY = calcFlushCoord(curTY, nTY, oy) - curY;

    worldToTile(curX + offsetX + wallOffX, curY + offsetY + wallOffY, nTX, nTY);
    if (cell(nTX, nTY) == 1) {
        const float cX = calcFlushCoord(curTX, nTX, ox);
        const float cY = calcFlushCoord(curTY, nTY, oy);
        if (std::abs(cX - curX) < std::abs(cY - curY)) offsetX = cX - curX;
        else offsetY = cY - curY;
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

// ---- MazeSuit ----

void Suit::init(int serialNum, RandomNumGen& parent, int cellWalkPeriod, double difficulty) {
    serial_ = serialNum;
    rng_.reseedFrom(parent); // uniqueRandomNumGen: RandomNumGen(randomNumGen)
    difficulty_ = difficulty;
    const float* start = kSuitStartPositions[serialNum % kMaxSuits];
    startTX_ = int(double(start[0]) * gMaze->width);
    startTY_ = int(double(start[1]) * gMaze->height);
    ticPeriod_ = cellWalkPeriod;
    cellWalkDuration_ = float(ticPeriod_) / float(kSuitTicFreq);
    turnDuration_ = 0.6f * cellWalkDuration_;
}

void Suit::onstage() {
    // Spiral out from the start tile to the first walkable one.
    int sTX = startTX_, sTY = startTY_;
    int c = 0, lim = 0, toggle = 0, direction = 0;
    while (!isWalkable(sTX, sTY)) {
        if (direction == 0) --sTX;
        else if (direction == 1) --sTY;
        else if (direction == 2) ++sTX;
        else ++sTY;
        if (++c > lim) {
            c = 0;
            direction = (direction + 1) % 4;
            if (!(++toggle & 1)) ++lim;
        }
    }
    TX_ = nextTX_ = sTX;
    TY_ = nextTY_ = sTY;
    direction_ = lastDirection_ = DirDown;
    tileToWorld(TX_, TY_, x, y);
    h = kDirectionHs[direction_];
    walking = false; // loop('neutral')
    moving_ = turning_ = false;
}

void Suit::gameStart() {
    walking = true;
    playRate = (kCellWidth / cellWalkDuration_) / kDefaultSuitSpeed;
    occupied_[0] = {nextTX_, nextTY_};
    numOccupied_ = 1;
    nextThinkTic_ = serial_ * kSuitTicFreq / 20;
    moving_ = turning_ = false;
}

int Suit::thinkTics(int curTic, int* first) const {
    *first = nextThinkTic_;
    if (curTic < nextThinkTic_) return 0;
    return (curTic - nextThinkTic_) / ticPeriod_ + 1;
}

Direction Suit::chooseNewWalkDirection(const Tile* unwalkables, int n) {
    if (!rng_.randrange(0, kWalkSameDirectionProb)) {
        int tx = TX_, ty = TY_;
        applyDirection(direction_, tx, ty);
        if (isWalkable(tx, ty, unwalkables, n)) return direction_;
    }
    if (difficulty_ >= 0.5 && !rng_.randrange(0, kWalkTurnAroundProb)) {
        const Direction opposite = kOpposite[direction_];
        int tx = TX_, ty = TY_;
        applyDirection(opposite, tx, ty);
        if (isWalkable(tx, ty, unwalkables, n)) return opposite;
    }
    Direction candidates[4] = {DirUp, DirDown, DirLeft, DirRight};
    int count = 0;
    for (Direction d : {DirUp, DirDown, DirLeft, DirRight})
        if (d != kOpposite[direction_]) candidates[count++] = d;
    while (count) {
        const int k = rng_.choice(count);
        const Direction dir = candidates[k];
        int tx = TX_, ty = TY_;
        applyDirection(dir, tx, ty);
        if (isWalkable(tx, ty, unwalkables, n)) return dir;
        for (int i = k; i + 1 < count; ++i) candidates[i] = candidates[i + 1]; // list.remove
        --count;
    }
    return kOpposite[direction_];
}

void Suit::think(int curTic, float curT, const Tile* unwalkables, int numUnwalkables, bool render) {
    (void)curTic;
    TX_ = nextTX_;
    TY_ = nextTY_;
    lastDirection_ = direction_;
    direction_ = chooseNewWalkDirection(unwalkables, numUnwalkables);
    nextTX_ = TX_;
    nextTY_ = TY_;
    applyDirection(direction_, nextTX_, nextTY_);
    occupied_[0] = {TX_, TY_};
    occupied_[1] = {nextTX_, nextTY_};
    numOccupied_ = 2;
    if (render) {
        // The interval starts at this frame: CInterval clamps think's negative start offset to 0.
        tileToWorld(TX_, TY_, fromX_, fromY_);
        tileToWorld(nextTX_, nextTY_, toX_, toY_);
        moving_ = true;
        moveStart_ = curT;
        if (direction_ != lastDirection_) {
            fromH_ = kDirectionHs[lastDirection_];
            toH_ = kDirectionHs[direction_];
            if (fromH_ == 270.0f && toH_ == 0.0f) fromH_ = -90.0f;
            else if (fromH_ == 0.0f && toH_ == 270.0f) fromH_ = 360.0f;
            turning_ = true;
        } else {
            h = kDirectionHs[direction_];
            turning_ = false;
        }
    }
    nextThinkTic_ += ticPeriod_;
}

void Suit::update(float curT) {
    if (moving_) {
        const float u = std::min(std::max((curT - moveStart_) / cellWalkDuration_, 0.0f), 1.0f);
        x = fromX_ + (toX_ - fromX_) * u;
        y = fromY_ + (toY_ - fromY_) * u;
    }
    if (turning_) {
        const float v = std::min(std::max((curT - moveStart_) / turnDuration_, 0.0f), 1.0f);
        h = fromH_ + (toH_ - fromH_) * v;
        if (v >= 1.0f) turning_ = false;
    }
}

void thinkSuits(Suit* suits, int numSuits, float curT) {
    struct Update {
        int tic, suit;
        bool render;
    };
    constexpr int kMaxUpdates = 512;
    static Update updates[kMaxUpdates];
    const int curTic = int(curT * float(kSuitTicFreq));
    int count = 0;
    for (int i = 0; i < numSuits; ++i) {
        int first;
        const int n = suits[i].thinkTics(curTic, &first);
        for (int k = 0; k < n && count < kMaxUpdates; ++k)
            updates[count++] = {first + k * suits[i].ticPeriod(), i, k == n - 1}; // lastTicBeforeRender
    }
    // A stable sort by tic, as suitUpdates.sort.
    for (int i = 1; i < count; ++i) {
        const Update u = updates[i];
        int j = i;
        while (j > 0 && updates[j - 1].tic > u.tic) {
            updates[j] = updates[j - 1];
            --j;
        }
        updates[j] = u;
    }
    int lastTic = 0;
    Tile unwalkables[2 * kMaxSuits];
    for (int i = 0; i < count; ++i) {
        const Update& u = updates[i];
        if (u.tic > lastTic) {
            lastTic = u.tic;
            for (int j = i + 1; j < count && updates[j].tic <= u.tic; ++j) suits[updates[j].suit].prepareToThink();
        }
        int n = 0;
        for (int s = 0; s < numSuits; ++s) {
            if (s == u.suit) continue;
            int m;
            const Tile* t = suits[s].occupied(&m);
            for (int k = 0; k < m; ++k) unwalkables[n++] = t[k];
        }
        suits[u.suit].think(u.tic, curT, unwalkables, n, u.render);
    }
    for (int i = 0; i < numSuits; ++i) suits[i].update(curT);
}

} // namespace tt::game::maze
