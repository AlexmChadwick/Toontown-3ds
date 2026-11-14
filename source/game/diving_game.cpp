#include "game/diving_game.h"

#include <cmath>
#include <cstdio>
#include "core/log.h"

namespace tt::game {

static constexpr const char* kDir = "sdmc:/3ds/toontown3ds/data/minigames/";

namespace {
constexpr float kPi = 3.14159265358979323846f;
// DistributedDivingGame.LAG_COMP (SPEEDMULT comes from the hood's difficulty pattern).
constexpr float kLagComp = 1.25f;
// CLerpInterval blend types.
float easeIn(float t) { return (3.0f * t * t - t * t * t) * 0.5f; }
float easeOut(float t) { return (3.0f * t - t * t * t) * 0.5f; }
float easeInOut(float t) { return 3.0f * t * t - 2.0f * t * t * t; }
constexpr float kCrabZ = -40.0f;
constexpr float kCrabHitRadius = 1.3f; // CollisionSphere(0, 0, 1, 1.3) on the crab
// DistributedDivingGame.spawnX = 24 * LAG_COMP; spawners sit at -spawnX (even ids) or +spawnX.
constexpr float kSpawnX = 24.0f * kLagComp;
constexpr float kFishSpawnY = 0.6f;   // spawnY
constexpr float kTreasureY = 0.25f;   // DivingTreasure treasureNode y
constexpr float kTreasureScale = 0.04f;
constexpr float kFishCrossDist = 50.0f;   // LerpPosInterval pos = position + 50 * direction
constexpr float kFishWaveAmplitude = 3.0f; // DistributedDivingGame.pbjMove
constexpr float kFishWavePeriod = 5.0f;    // pbj specialLerp duration, before LAG_COMP
constexpr float kReachMargin = 0.8f;       // swimmer reach around a fish's hit radius

struct FishType {
    const char* model; // baked name, see tools/pipeline/bake_minigame.py
    float duration;    // DistributedDivingGame.fishSpawn base seconds across the tank
    float radius;      // rough hit radius (DivingFishSpawn.createFish collision spheres)
    float scale[3];    // createFish setScale
};

// DivingFishSpawn.createFish order: clown 0, pbj 1, bear 2, balloon 3, nurse 4, piano 5.
constexpr FishType kFishTypes[6] = {
    {"diving_fish_clown", 8.0f, 1.2f, {1.0f, 1.0f, 1.0f}},
    {"diving_fish_pbj", 12.0f, 1.0f, {1.0f, 1.0f, 1.0f}},
    {"diving_fish_bear", 20.0f, 3.0f, {0.4f, 1.7f, 1.7f}},
    {"diving_fish_balloon", 10.0f, 0.2f, {2.0f, 2.0f, 2.0f}},
    {"diving_fish_nurse", 20.0f, 1.0f, {0.5f, 1.7f, 1.7f}},
    {"diving_fish_piano", 5.0f, 1.0f, {1.4f, 1.4f, 1.4f}},
};

// createFish setH: clown, bear and nurse face 90 * direction, PBJ 15 * direction, the balloon
// faces 0, and the piano faces 0 going left and 180 going right.
float fishHeading(int type, int dir) {
    switch (type) {
    case 0:
    case 2:
    case 4:
        return 90.0f * float(dir);
    case 1:
        return 15.0f * float(dir);
    case 3:
        return 0.0f;
    default:
        return dir < 0 ? 0.0f : 180.0f;
    }
}
} // namespace

void DivingGame::spawnFish(uint32_t fishCode, uint32_t spawnerId, uint32_t offset) {
    // createFish returns nothing for an unknown code, so no fish id is used up.
    if (fishCode >= uint32_t(kFishTypeCount) || spawnerId >= uint32_t(diving::kNumSpawners)) return;
    const uint16_t id = spawnCount_[spawnerId]++;
    for (Fish& f : fish_) {
        if (f.active) continue;
        f = Fish{};
        f.active = true;
        f.type = uint8_t(fishCode);
        f.spawner = uint8_t(spawnerId);
        f.id = id;
        f.dir = diving::kSpawnerDir[spawnerId];
        f.offset = float(offset);
        f.x = (spawnerId % 2 == 0) ? -kSpawnX : kSpawnX;
        f.z = diving::kSpawnerZ[spawnerId];
        return;
    }
    TT_LOG("diving: fish pool full, spawn %u dropped", unsigned(id));
}

void DivingGame::init(net::Client& client, uint32_t nowMs, bool autoPlay) {
    done_ = false;
    autoPlay_ = autoPlay;
    startMs_ = nowMs;
    timeRemaining_ = diving::kGameDuration;

    swimmer_ = {};
    swimmer_.x = 0.0f;
    swimmer_.z = 34.0f;

    recoveries_ = 0;
    score_ = 5;

    // DivingTreasure: treasureNode at (-15 + 10 * i, 0.25, -36).
    for (int i = 0; i < 3; ++i) {
        chests_[i] = {-15.0f + 10.0f * float(i), diving::kSeabedZ, true};
    }

    for (Fish& f : fish_) f = Fish{};
    for (uint16_t& c : spawnCount_) c = 0;

    // setGameReady's difficultyPatterns[hood]: NUMCRABS, SPEEDMULT.
    const net::Minigame* mg = client.minigame();
    const uint32_t hood = mg ? mg->trolleyZone - mg->trolleyZone % 1000 : 2000;
    numCrabs_ = 1;
    speedMult_ = 1.0f;
    switch (hood) {
    case 2000: numCrabs_ = 1, speedMult_ = 1.5f; break;
    case 1000: numCrabs_ = 1, speedMult_ = 1.3f; break;
    case 5000: numCrabs_ = 2, speedMult_ = 1.2f; break;
    case 4000: numCrabs_ = 2; break;
    case 3000: case 9000: numCrabs_ = 3; break;
    default: break;
    }
    for (int i = 0; i < kMaxCrabs; ++i) {
        crabs_[i] = Crab{};
        crabs_[i].x = i % 2 == 0 ? 20.0f : -20.0f;
        crabs_[i].direction = i % 2 == 0 ? -1 : 1;
    }
    animTime_ = 0.0f;

    char path[96];
    std::snprintf(path, sizeof(path), "%sdiving_game.ttm", kDir);
    assetBatch_.load(environModel_, path);
    std::snprintf(path, sizeof(path), "%sdiving_boat.ttm", kDir);
    assetBatch_.load(boatModel_, path);
    std::snprintf(path, sizeof(path), "%sdiving_chest.ttm", kDir);
    assetBatch_.load(chestModel_, path);
    for (int i = 0; i < kFishTypeCount; ++i) {
        std::snprintf(path, sizeof(path), "%s%s.ttm", kDir, kFishTypes[i].model);
        assetBatch_.load(fishModel_[i], path);
    }
    std::snprintf(path, sizeof(path), "%sdiving_crab_full.ttm", kDir);
    assetBatch_.load(crabModel_, path);
    std::snprintf(path, sizeof(path), "%sdiving_crab_full_anim.tta", kDir);
    assetBatch_.load(crabAnim_, path);

    std::snprintf(lines_[0], sizeof(lines_[0]), "Treasures: 0");
    std::snprintf(lines_[1], sizeof(lines_[1]), "Depth: 0.0 m  (Time: 65 s)");
    std::snprintf(lines_[2], sizeof(lines_[2]), "Jellybeans: 5");
}

void DivingGame::shutdown() {
    assetBatch_.clear();
    environModel_.unload();
    boatModel_.unload();
    chestModel_.unload();
    for (render::Model& m : fishModel_) m.unload();
    crabModel_.unload();
    crabAnim_.unload();
    done_ = true;
}

void DivingGame::update(net::Client& client, const circlePosition& pad, u32 kDown, float dt, uint32_t nowMs) {
    (void)kDown;
    (void)nowMs;
    if (done_) return;

    timeRemaining_ -= dt;
    if (timeRemaining_ <= 0.0f) {
        timeRemaining_ = 0.0f;
        done_ = true;
    }

    // Input
    float inputX = 0.0f;
    float inputZ = 0.0f;
    constexpr int kDeadzone = 20;
    if (std::abs(pad.dx) > kDeadzone || std::abs(pad.dy) > kDeadzone) {
        inputX = pad.dx / 150.0f;
        inputZ = pad.dy / 150.0f;
    }

    if (autoPlay_) {
        // If no treasure, dive down to nearest chest
        if (!swimmer_.hasTreasure) {
            inputZ = -1.0f;
            int targetChest = 1;
            for (int i = 0; i < 3; ++i) {
                if (chests_[i].available) { targetChest = i; break; }
            }
            float dx = chests_[targetChest].x - swimmer_.x;
            inputX = (dx > 1.0f) ? 0.8f : (dx < -1.0f) ? -0.8f : 0.0f;
        } else {
            // Swim up to boat
            inputZ = 1.0f;
            inputX = (swimmer_.x > 1.0f) ? -0.8f : (swimmer_.x < -1.0f) ? 0.8f : 0.0f;
        }
    }

    // Update swimmer physics
    diving::updateSwimmer(swimmer_, inputX, inputZ, dt);

    // AI fish: advance each crossing fish and hit-test the swimmer (the server counts the hit by
    // the fish id and spawner, as DistributedDivingGame.fishCollision does).
    for (Fish& f : fish_) {
        if (!f.active) continue;
        // fishSpawn: bear and nurse cross in 20 * LAG_COMP, the rest scale with SPEEDMULT.
        const float duration = kFishTypes[f.type].duration * kLagComp * (f.type == 2 || f.type == 4 ? 1.0f : speedMult_);
        f.elapsed += dt;
        if (f.elapsed >= duration) {
            f.active = false;
            continue;
        }
        const float t = f.elapsed / duration;
        const float startX = (f.spawner % 2 == 0) ? -kSpawnX : kSpawnX;
        f.x = startX + float(f.dir) * kFishCrossDist * t;
        const float z0 = diving::kSpawnerZ[f.spawner];
        if (f.type == 1) {
            const float wave = kFishWavePeriod * kLagComp * speedMult_;
            const float phase = 2.0f * kPi * std::fmod(f.elapsed / wave, 1.0f) + f.offset * 3.0f;
            f.z = z0 + std::sin(phase) * kFishWaveAmplitude;
        } else if (f.type == 0 || f.type == 3 || f.type == 5) {
            f.z = z0 + (f.offset - 4.0f) / 2.0f * t;
        } else {
            f.z = z0;
        }

        if (swimmer_.freezeTimer <= 0.0f) {
            const float dx = swimmer_.x - f.x;
            const float dz = swimmer_.z - f.z;
            const float reach = kFishTypes[f.type].radius + kReachMargin;
            if (dx * dx + dz * dz < reach * reach) {
                diving::applyObstacleHit(swimmer_);
                client.sendDivingFishCollision(client.avatarId(), f.id, f.spawner, "normal");
            }
        }
    }

    updateCrabs(client, nowMs);
    animTime_ += dt;

    // Check chest pickup
    for (int i = 0; i < 3; ++i) {
        if (diving::checkChestPickup(swimmer_, chests_[i])) {
            client.sendDivingPickupTreasure(i);
            break;
        }
    }

    // Check boat recovery
    if (diving::checkBoatRecovery(swimmer_)) {
        recoveries_++;
        score_ = diving::calculateScore(recoveries_);
        client.sendDivingTreasureRecovered();
        // Respawn chests
        for (int i = 0; i < 3; ++i) {
            chests_[i].available = true;
        }
    }

    // Server score sync if minigame active
    if (client.minigame() && client.minigame()->divingScoreAv != 0) {
        score_ = diving::calculateScore(recoveries_);
    }

    // Depth in meters (surface at 36 = 0m, bottom at -38 = 74m)
    const float depthM = std::max(0.0f, (36.0f - swimmer_.z));

    std::snprintf(lines_[0], sizeof(lines_[0]), "Treasures: %d", recoveries_);
    std::snprintf(lines_[1], sizeof(lines_[1]), "Depth: %.1f m  (Time: %d s)", depthM, int(std::ceil(timeRemaining_)));
    std::snprintf(lines_[2], sizeof(lines_[2]), "Jellybeans: %d", score_);
}

void DivingGame::moveCrab(int crabId, uint32_t startMs, int rand1, int rand2, int crabX, int dir) {
    if (crabId < 0 || crabId >= numCrabs_) return;
    Crab& c = crabs_[crabId];
    // setCrabMoving: aim past the treasures' mean x, then walk, leap and fall back.
    float meanX = 0.0f;
    for (const auto& chest : chests_) meanX += chest.x;
    meanX /= 3.0f;
    int goalX = int(meanX + float(dir) * (float(rand1) / 10.0f) * 12.0f + 4.0f);
    if (goalX > 20) goalX = 20;
    else if (goalX < -20) goalX = 20; // as stock
    c.goalX = float(goalX);
    c.goalZ = kCrabZ + 5.0f + 5.0f * (float(rand2) / 10.0f);
    c.xTime = 1.0f + float(rand1) / 10.0f * 2.0f;
    c.zTime = 0.5f + float(rand2) / 10.0f;
    c.wait = float(rand1) / 10.0f + float(rand2) / 10.0f + 1.0f;
    c.fromX = float(crabX);
    c.direction = -c.direction;
    c.startMs = startMs;
    c.playing = true;
    c.requested = false;
}

void DivingGame::updateCrabs(net::Client& client, uint32_t nowMs) {
    for (int i = 0; i < numCrabs_; ++i) {
        Crab& c = crabs_[i];
        if (!c.playing) continue;
        float t = float(int32_t(nowMs - c.startMs)) / 1000.0f;
        if (t < 0.0f) t = 0.0f;
        if (t < c.wait) {
            c.x = c.fromX, c.z = kCrabZ;
        } else if ((t -= c.wait) < c.xTime) {
            c.x = c.fromX + (c.goalX - c.fromX) * easeIn(t / c.xTime), c.z = kCrabZ;
        } else if ((t -= c.xTime) < c.zTime) {
            c.x = c.goalX, c.z = kCrabZ + (c.goalZ - kCrabZ) * easeOut(t / c.zTime);
        } else if ((t -= c.zTime) < c.zTime) {
            c.x = c.goalX, c.z = c.goalZ + (kCrabZ - c.goalZ) * easeInOut(t / c.zTime);
        } else {
            c.x = c.goalX, c.z = kCrabZ;
            c.playing = false;
        }
    }
    // __crabTask: the first idle crab asks for its next move, one a frame.
    for (int i = 0; i < numCrabs_; ++i) {
        Crab& c = crabs_[i];
        if (c.playing || c.requested) continue;
        c.requested = client.sendDivingGetCrabMoving(uint32_t(i), int16_t(c.x), int8_t(c.direction));
        break;
    }
    if (swimmer_.freezeTimer > 0.0f) return;
    for (int i = 0; i < numCrabs_; ++i) {
        const float dx = swimmer_.x - crabs_[i].x, dz = swimmer_.z - (crabs_[i].z + 1.0f);
        const float reach = kCrabHitRadius + kReachMargin;
        if (dx * dx + dz * dz < reach * reach) {
            diving::applyObstacleHit(swimmer_);
            client.sendDivingCrabCollision(client.avatarId(), "normal");
            break;
        }
    }
}

void DivingGame::draw(render::Renderer& renderer) {
    C3D_Mtx m;
    if (environModel_.loaded()) {
        Mtx_Identity(&m);
        // The baker already applies DistributedDivingGame environ placement/scale.
        renderer.drawModel(environModel_, m);
    }
    if (boatModel_.loaded()) {
        Mtx_Identity(&m);
        // Boat placement/scale is baked as well.
        renderer.drawModel(boatModel_, m);
    }
    if (chestModel_.loaded()) {
        for (int i = 0; i < 3; ++i) {
            if (chests_[i].available) {
                // treasureNode: position, then scale 0.04; the chest-local offset is baked.
                Mtx_Identity(&m);
                Mtx_Translate(&m, chests_[i].x, kTreasureY, chests_[i].z, true);
                Mtx_Scale(&m, kTreasureScale, kTreasureScale, kTreasureScale);
                renderer.drawModel(chestModel_, m);
            }
        }
    }
    if (crabModel_.loaded()) {
        for (int i = 0; i < numCrabs_; ++i) {
            // setGameReady: crab.setScale(1, 0.3, 1), loop('anim'); no heading is set.
            Mtx_Identity(&m);
            Mtx_Translate(&m, crabs_[i].x, 0.0f, crabs_[i].z, true);
            Mtx_Scale(&m, 1.0f, 0.3f, 1.0f);
            if (crabAnim_.loaded()) {
                const float pos = std::fmod(animTime_ * crabAnim_.fps(), float(crabAnim_.numFrames()));
                const u32 a = u32(pos), b = (a + 1) % crabAnim_.numFrames();
                renderer.drawMorph(crabModel_, crabAnim_, a, b, pos - float(a), m);
            } else {
                renderer.drawModel(crabModel_, m);
            }
        }
    }
    for (const Fish& f : fish_) {
        if (!f.active || !fishModel_[f.type].loaded()) continue;
        const FishType& type = kFishTypes[f.type];
        Mtx_Identity(&m);
        Mtx_Translate(&m, f.x, kFishSpawnY, f.z, true);
        Mtx_RotateZ(&m, fishHeading(f.type, f.dir) * kPi / 180.0f, true);
        Mtx_Scale(&m, type.scale[0], type.scale[1], type.scale[2]);
        renderer.drawModel(fishModel_[f.type], m);
    }
}

void DivingGame::toonPose(float* x, float* y, float* z, float* h) const {
    if (x) *x = swimmer_.x;
    if (y) *y = -1.0f;
    if (z) *z = swimmer_.z;
    if (h) *h = 0.0f; // the orientation is the body roll (toonRoll)
}

// __placeToon: hpr (180, 180, 0), i.e. head down with the back to the camera, then the arrow
// keys roll it; the Toon swims head first. Here the head follows the swim velocity.
float DivingGame::toonRoll() const {
    return 180.0f + std::atan2(-swimmer_.xVel, -swimmer_.zVel) * 180.0f / 3.14159265f;
}

// DistributedDivingGame: the camera stays at x 0, y -54, level, with camLens.setFov(45)
// (MinigameSession::applyWorld); __updateLocalToonTask sets its z to the Toon's z + 3, at
// least -25.
void DivingGame::camera(C3D_FVec* eye, C3D_FVec* target) const {
    float camZ = swimmer_.z + 3.0f;
    if (camZ < -25.0f) camZ = -25.0f;
    if (target) *target = FVec4_New(0.0f, 0.0f, camZ, 1.0f);
    if (eye) *eye = FVec4_New(0.0f, -54.0f, camZ, 1.0f);
}

} // namespace tt::game
