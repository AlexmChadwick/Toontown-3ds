#include "game/maze_game.h"

#include <cmath>
#include <cstdio>
#include "core/log.h"

namespace tt::game {

static constexpr const char* kDir = "sdmc:/3ds/toontown3ds/data/minigames/";

namespace {
constexpr float kFlyVz = 50.0f;     // Trajectory(0, (0, 0, 0), (0, 0, 50), gravMult=1)
constexpr float kGravity = 32.0f;   // Trajectory.gravity
constexpr float kFlyDur = 2.0f * kFlyVz / kGravity; // calcTimeOfImpactOnPlane(0)
constexpr float kZenith = kFlyVz * kFlyVz / (2.0f * kGravity);
constexpr float kCamY = -19.0f, kCamZ = 45.0f; // camOffset
constexpr float kMaxFrameMove = maze::kCellWidth / 2.0f; // MAX_FRAME_MOVE

float flyZ(float t) { return kFlyVz * t - 0.5f * kGravity * t * t; }
} // namespace

void MazeGame::init(net::Client& client, uint32_t nowMs, bool autoPlay) {
    (void)nowMs;
    done_ = false;
    autoPlay_ = autoPlay;
    isRunning_ = false;
    started_ = false;
    flying_ = false;
    spinH_ = 0.0f;
    avId_ = client.avatarId();

    const net::Minigame* mg = client.minigame();
    const int numPlayers = mg && mg->numParticipants > 0 ? (mg->numParticipants < 4 ? mg->numParticipants : 4) : 1;
    maze::setMaze(numPlayers); // load: MazeData.mazeNames[numPlayers - 1]
    const maze::MazeInfo& m = maze::maze();
    numTreasures_ = m.numTreasures < maze::kMaxTreasures ? m.numTreasures : maze::kMaxTreasures;
    for (int i = 0; i < maze::kMaxTreasures; ++i) treasureTaken_[i] = false;
    treasuresCollected_ = 0;
    treasuresGone_ = 0;
    score_ = 0;

    // onstage, in its RandomNumGen(doId) order: the start spots, one RNG per Toon, then the suits.
    rng_.reseed(mg ? mg->doId : 0);
    struct Start {
        float x, y, h;
    } starts[4] = {{0.0f, 3.0f, 0.0f}, {0.0f, -3.0f, 180.0f}, {3.0f, 0.0f, 270.0f}, {-3.0f, 0.0f, 90.0f}};
    rng_.shuffle(starts, 4);
    slot_ = 0;
    if (mg)
        for (int i = 0; i < numPlayers; ++i)
            if (mg->participants[i] == avId_) slot_ = i;
    toonX_ = starts[slot_].x;
    toonY_ = starts[slot_].y;
    toonZ_ = 0.0f; // TOON_Z
    toonH_ = starts[slot_].h;
    for (int i = 0; i < numPlayers; ++i) toonRng_[i].reseedFrom(rng_);

    // __loadSuits: 4 per player; half the periods from the slower table, half from the faster.
    const uint32_t hood = mg ? mg->trolleyZone - mg->trolleyZone % 1000 : 2000;
    static const uint32_t kSafeZones[6] = {2000, 1000, 5000, 4000, 3000, 9000}; // MinigameGlobals.SafeZones
    double difficulty = 0.0;
    for (int i = 0; i < 6; ++i)
        if (kSafeZones[i] == hood) difficulty = i / 5.0;
    if (mg && mg->difficultyOverride != net::kNoDifficultyOverride) difficulty = mg->difficultyOverride / 65536.0;
    int hoodIdx = 1; // 2000
    for (int i = 0; i < maze::kSuitHoods; ++i)
        if (maze::kSuitHood[i] == hood) hoodIdx = i;
    numSuits_ = 4 * numPlayers;
    const int half = numSuits_ / 2;
    const int row = numSuits_ / 4 - 1;
    const uint8_t* slower = maze::kSuitPeriods[difficulty < 0.5 ? 1 : 0][hoodIdx][row]; // SLOWER_SUIT_CURVE
    const uint8_t* faster = maze::kSuitPeriods[3][hoodIdx][row];                        // FASTER_SUIT_CURVE = 1
    int periods[maze::kMaxSuits];
    for (int i = 0; i < half; ++i) {
        periods[i] = slower[i];
        periods[half + i] = faster[i];
    }
    rng_.shuffle(periods, numSuits_);
    for (int i = 0; i < numSuits_; ++i) {
        suits_[i].init(i, rng_, periods[i], difficulty);
        suits_[i].onstage();
    }

    char path[96];
    std::snprintf(path, sizeof(path), "%s%s.ttm", kDir, m.model);
    assetBatch_.load(mazeModel_, path);
    std::snprintf(path, sizeof(path), "%smaze_treasure.ttm", kDir);
    assetBatch_.load(treasureModel_, path);

    TT_LOG("maze: %s doId=%lu players=%d slot=%d start=(%.0f, %.0f) suits=%d hood=%lu difficulty=%.2f",
           m.model, static_cast<unsigned long>(mg ? mg->doId : 0), numPlayers, slot_, double(toonX_),
           double(toonY_), numSuits_, static_cast<unsigned long>(hood), difficulty);
    std::snprintf(lines_[0], sizeof(lines_[0]), "Treasures: 0 / %d", numTreasures_);
    std::snprintf(lines_[1], sizeof(lines_[1]), "Time: 60 s");
    std::snprintf(lines_[2], sizeof(lines_[2]), "Score: 1 bean");
}

void MazeGame::shutdown() {
    assetBatch_.clear();
    mazeModel_.unload();
    treasureModel_.unload();
    done_ = true;
}

void MazeGame::onTreasureGrabbed(uint32_t avId, uint32_t index) {
    (void)avId;
    if (index >= uint32_t(numTreasures_)) return;
    treasureTaken_[index] = true; // showGrab (ours was hidden at the grab)
    ++treasuresGone_;
}

void MazeGame::startHit(net::Client& client, uint32_t nowMs) {
    // hitBySuit, then __showToonHitBySuit with this Toon's RNG (other clients replay the same draws).
    client.sendMazeHitBySuit(avId_, client.networkTime(nowMs));
    RandomNumGen& rng = toonRng_[slot_];
    const maze::MazeInfo& m = maze::maze();
    int tx, ty;
    do {
        tx = rng.randint(2, m.width - 1);
        ty = rng.randint(2, m.height - 1);
    } while (!maze::isWalkable(tx, ty));
    int hRot = rng.randrange(1, 8);
    if (rng.choice(2)) hRot = -hRot;
    int pRot = rng.randrange(1, 3); // the pitch spin: drawn for the RNG, the pose has no pitch
    if (rng.choice(2)) pRot = -pRot;
    (void)pRot;
    flyFromX_ = toonX_;
    flyFromY_ = toonY_;
    maze::tileToWorld(tx, ty, flyToX_, flyToY_);
    spinH_ = float(hRot) * 360.0f;
    flyT_ = 0.0f;
    flying_ = true;
}

void MazeGame::update(net::Client& client, const circlePosition& pad, u32 kDown, float dt, uint32_t nowMs) {
    (void)kDown;
    if (done_) return;
    const net::Minigame* mg = client.minigame();
    if (!started_) {
        if (!mg || !mg->gameStartMs) return; // suits stand in neutral until setGameStart
        started_ = true;
        for (int i = 0; i < numSuits_; ++i) suits_[i].gameStart();
    }
    const float curT = float(int32_t(nowMs - mg->gameStartMs)) / 1000.0f;
    float remaining = maze::kGameDuration - curT;
    if (remaining <= 0.0f) {
        remaining = 0.0f;
        done_ = true; // timerExpired
    }
    if (mg->mazeAllTaken) done_ = true;

    if (flying_) {
        flyT_ += dt;
        const float u = flyT_ >= kFlyDur ? 1.0f : flyT_ / kFlyDur;
        toonX_ = flyFromX_ + u * (flyToX_ - flyFromX_);
        toonY_ = flyFromY_ + u * (flyToY_ - flyFromY_);
        toonZ_ = flyT_ >= kFlyDur ? 0.0f : flyZ(flyT_);
        if (flyT_ >= kFlyDur) {
            flying_ = false;
            spinH_ = 0.0f;
        }
        isRunning_ = false;
    } else {
        float moveX = 0.0f, moveY = 0.0f;
        constexpr int kDeadzone = 20;
        if (std::abs(pad.dx) > kDeadzone || std::abs(pad.dy) > kDeadzone) {
            moveX = pad.dx / 150.0f;
            moveY = pad.dy / 150.0f;
        }
        if (autoPlay_) {
            float best = 1e9f, tx = 0.0f, ty = 0.0f;
            const maze::MazeInfo& m = maze::maze();
            for (int i = 0; i < numTreasures_; ++i) {
                if (treasureTaken_[i]) continue;
                const float dx = m.treasures[i][0] - toonX_, dy = m.treasures[i][1] - toonY_;
                if (dx * dx + dy * dy < best) best = dx * dx + dy * dy, tx = m.treasures[i][0], ty = m.treasures[i][1];
            }
            if (best < 1e8f) {
                const float dx = tx - toonX_, dy = ty - toonY_, d = std::sqrt(dx * dx + dy * dy);
                if (d > 0.1f) moveX = dx / d, moveY = dy / d;
            }
        }
        const float len = std::sqrt(moveX * moveX + moveY * moveY);
        if (len > 1.0f) moveX /= len, moveY /= len;
        if (std::abs(moveX) > 0.05f || std::abs(moveY) > 0.05f) {
            isRunning_ = true;
            toonH_ = std::atan2(-moveX, moveY) * 180.0f / 3.14159265f;
            float stepX = moveX * maze::kToonSpeed * dt, stepY = moveY * maze::kToonSpeed * dt;
            const float step = std::sqrt(stepX * stepX + stepY * stepY);
            if (step > kMaxFrameMove) stepX *= kMaxFrameMove / step, stepY *= kMaxFrameMove / step; // OrthoDrive
            maze::doOrthoCollisions(toonX_, toonY_, toonX_ + stepX, toonY_ + stepY, toonX_, toonY_);
        } else {
            isRunning_ = false;
        }

        const maze::MazeInfo& m = maze::maze();
        constexpr float kGrabSq = maze::kTreasureGrabRadius * maze::kTreasureGrabRadius;
        for (int i = 0; i < numTreasures_; ++i) {
            if (treasureTaken_[i]) continue;
            const float dx = toonX_ - m.treasures[i][0], dy = toonY_ - m.treasures[i][1];
            if (dx * dx + dy * dy < kGrabSq) {
                treasureTaken_[i] = true; // __treasureGrabbed: showGrab, claimTreasure
                ++treasuresCollected_;
                client.sendMazeClaimTreasure(i);
                score_ = maze::calculateScore(treasuresCollected_, false);
            }
        }
    }

    maze::thinkSuits(suits_, numSuits_, curT);
    if (!flying_) {
        constexpr float kHitSq = maze::kSuitHitRadius * maze::kSuitHitRadius;
        for (int i = 0; i < numSuits_; ++i) {
            const float dx = toonX_ - suits_[i].x, dy = toonY_ - suits_[i].y;
            if (dx * dx + dy * dy < kHitSq) {
                startHit(client, nowMs);
                break;
            }
        }
    }

    std::snprintf(lines_[0], sizeof(lines_[0]), "Treasures: %d / %d", treasuresGone_, numTreasures_);
    std::snprintf(lines_[1], sizeof(lines_[1]), "Time: %d s", int(std::ceil(remaining)));
    std::snprintf(lines_[2], sizeof(lines_[2]), "Jellybeans: %d", score_ > 0 ? score_ : 1);
}

void MazeGame::draw(render::Renderer& renderer) {
    C3D_Mtx m;
    if (mazeModel_.loaded()) {
        Mtx_Identity(&m);
        renderer.drawModel(mazeModel_, m);
    }
    if (!treasureModel_.loaded()) return;
    const maze::MazeInfo& info = maze::maze();
    for (int i = 0; i < numTreasures_; ++i) {
        if (treasureTaken_[i]) continue;
        const float tx = info.treasures[i][0], ty = info.treasures[i][1];
        // Only the ones near the follow camera's view (the maze is up to 100 x 80).
        if (std::abs(tx - toonX_) > 30.0f || std::abs(ty - toonY_) > 26.0f) continue;
        Mtx_Identity(&m);
        Mtx_Translate(&m, tx, ty, 1.0f, true); // MazeTreasure: setPos(x, y, 1.0)
        renderer.drawModel(treasureModel_, m);
    }
}

int MazeGame::cogs(MinigameCog* out, int max) const {
    // DistributedMazeGame.__loadSuits: every MazeSuit is a Flunky (MazeSuit suitDnaName 'f').
    int n = 0;
    for (int i = 0; i < numSuits_ && n < max; ++i) {
        MinigameCog& c = out[n++];
        c.x = suits_[i].x;
        c.y = suits_[i].y;
        c.z = maze::kSuitZ;
        c.h = suits_[i].h;
        c.code[0] = 'f';
        c.code[1] = 0;
        c.walking = suits_[i].walking;
    }
    return n;
}

void MazeGame::toonPose(float* x, float* y, float* z, float* h) const {
    if (x) *x = toonX_;
    if (y) *y = toonY_;
    if (z) *z = toonZ_;
    // hitBySuit-spinH: hRot whole turns over the flight.
    if (h) *h = toonH_ + (flying_ ? spinH_ * (flyT_ / kFlyDur) : 0.0f);
}

void MazeGame::camera(C3D_FVec* eye, C3D_FVec* target) const {
    if (target) *target = FVec4_New(toonX_, toonY_, toonZ_, 1.0f); // camera.lookAt(base.localAvatar)
    if (!eye) return;
    // camParent rides the Toon at (0, -19, 45); in a hit flight it rides the flying parent node and
    // rises toward (0, -19 * 0.3, zenith * 1.3) with the Toon's height (camTask).
    float camY = kCamY, camZ = kCamZ;
    if (flying_) {
        const float u = toonZ_ / kZenith;
        camY += (kCamY * 0.3f - kCamY) * u;
        camZ += (kZenith * 1.3f - kCamZ) * u;
    }
    *eye = FVec4_New(toonX_, toonY_ + camY, camZ, 1.0f);
}

} // namespace tt::game
