#include "game/two_d_logic.h"

#include <cmath>
#include <cstring>

#include "world/suit_tables.h"

namespace tt::twodgame {

namespace {

constexpr float kDegToRad = 3.14159265358979f / 180.0f;

// Panda LerpBlendHelpers easeInOut (TwoDEnemy up/down Cogs).
float easeInOut(float u) {
    return u * u * (3.0f - 2.0f * u);
}

// PythonUtil.reduceAngle / fitSrcAngle2Dest.
float reduceAngle(float deg) {
    float r = std::fmod(deg + 180.0f, 360.0f);
    if (r < 0.0f) r += 360.0f;
    return r - 180.0f;
}

float fitSrcAngle2Dest(float src, float dest) {
    return dest + reduceAngle(src - dest);
}

float positiveFmod(float a, float b) {
    float r = std::fmod(a, b);
    return r < 0.0f ? r + b : r;
}

// A LerpPosInterval pair looped forward then back (TwoDBlock / TwoDEnemy moveIval).
void pingPong(const float* init, const float* final, float duration, float t, bool ease, float* out) {
    if (duration <= 0.0f) {
        for (int i = 0; i < 3; ++i) out[i] = init[i];
        return;
    }
    const float p = positiveFmod(t, 2.0f * duration);
    const bool forward = p < duration;
    float u = forward ? p / duration : (p - duration) / duration;
    if (ease) u = easeInOut(u);
    const float* from = forward ? init : final;
    const float* to = forward ? final : init;
    for (int i = 0; i < 3; ++i) out[i] = from[i] + (to[i] - from[i]) * u;
}

float suitHeight(const char* code) {
    for (const auto& info : world::kSuitTypes)
        if (!std::strcmp(info.code, code)) return info.height;
    return 0.0f;
}

bool circlesOverlap(float ax, float az, float ar, float bx, float bz, float br) {
    const float dx = ax - bx, dz = az - bz, r = ar + br;
    return dx * dx + dz * dz < r * r;
}

// Every collision outline near x at game time t: blocks, the two walls, stompers and the
// stomper blocks. `fn(segment, offsetX, offsetZ, scale)`.
template <typename Fn>
void forEachSegment(const Course& c, const bool* stashed, float t, float x, Fn fn) {
    constexpr float kNear = 40.0f;
    for (int i = 0; i < c.numBlocks; ++i) {
        float bx, bz;
        c.blockOffset(i, t, &bx, &bz);
        const BlockTypeDef& type = kBlockTypes[c.blocks[i].def->type];
        if (bx - kNear > x || bx + 24.5f + kNear < x) continue;
        for (const Segment& s : type.segments) fn(s, bx, bz, 1.0f);
    }
    const float startWallX = kStartSectionX + kStartWallPos[0];
    const float endWallX = c.endSectionX + kEndWallPos[0];
    for (const Segment& s : kWallSegments) {
        fn(s, startWallX, kStartWallPos[2], kWallScale);
        fn(s, endWallX, kEndWallPos[2], kWallScale);
    }
    for (int i = 0; i < c.numStompers; ++i) {
        const CourseStomper& st = c.stompers[i];
        if (std::fabs(st.x - x) > kNear) continue;
        const StomperTypeDef& type = *st.type;
        const float scale = type.scale;
        for (const Segment& s : kStomperBlockSegments) fn(s, st.x, st.z + kStomperBlockZ * scale, 1.0f);
        if (stashed[i]) continue;
        // TwoDStomper.adjustCollSolidHeight: solid 0 stays, the last rides the head and the
        // ones between are spread over heightDiff.
        const float head = c.stomperHeadZ(i, t);
        const float heightDiff = head - type.headStartZ;
        for (int k = 0; k < type.numCollSolids; ++k) {
            float z = 0.0f;
            if (k == type.numCollSolids - 1) z = head;
            else if (k > 0) z = heightDiff * float(k) / float(type.numCollSolids - 1);
            const float z1 = st.z + z * scale;
            const float z2 = st.z + (z + kStomperCollHeight) * scale;
            const float w = kStomperCollHalfWidth * scale;
            fn(Segment{SegKind::Wall, w, z1, w, z2, 1.0f, 0.0f}, st.x, 0.0f, 1.0f);
            fn(Segment{SegKind::Wall, -w, z1, -w, z2, -1.0f, 0.0f}, st.x, 0.0f, 1.0f);
        }
    }
}

// Depth to push a circle out of a polygon outline along its plane normal (CollisionPolygon
// sphere test as CollisionHandlerPusher uses it); 0 when they don't touch.
float pushDepth(const Segment& s, float ox, float oz, float scale, float cx, float cz, float r) {
    const float ax = ox + s.x1 * scale, az = oz + s.z1 * scale;
    const float bx = ox + s.x2 * scale, bz = oz + s.z2 * scale;
    const float d = (cx - ax) * s.nx + (cz - az) * s.nz;
    if (d > r || d < -r) return 0.0f;
    const float ux = bx - ax, uz = bz - az;
    const float len = std::sqrt(ux * ux + uz * uz);
    const float along = len > 0.0f ? ((cx - ax) * ux + (cz - az) * uz) / len : 0.0f;
    float edge = 0.0f;
    if (along < 0.0f) edge = -along;
    else if (along > len) edge = along - len;
    if (edge * edge + d * d >= r * r) return 0.0f;
    return std::sqrt(r * r - edge * edge) - d;
}

bool circleTouchesSegment(const Segment& s, float ox, float oz, float scale, float cx, float cz, float r) {
    const float ax = ox + s.x1 * scale, az = oz + s.z1 * scale;
    const float bx = ox + s.x2 * scale, bz = oz + s.z2 * scale;
    const float ux = bx - ax, uz = bz - az;
    const float len2 = ux * ux + uz * uz;
    float u = len2 > 0.0f ? ((cx - ax) * ux + (cz - az) * uz) / len2 : 0.0f;
    u = u < 0.0f ? 0.0f : u > 1.0f ? 1.0f : u;
    const float dx = cx - (ax + ux * u), dz = cz - (az + uz * u);
    return dx * dx + dz * dz < r * r;
}

// TwoDGameToonSD's FSM transitions.
bool allowed(ToonState from, ToonState to) {
    switch (from) {
    case ToonState::Normal:
        return true;
    case ToonState::FallBack:
        return to == ToonState::Normal || to == ToonState::FallDown || to == ToonState::Squish;
    case ToonState::FallDown:
    case ToonState::Squish:
        return to == ToonState::Normal;
    case ToonState::Victory:
        return to == ToonState::Normal || to == ToonState::FallDown;
    }
    return false;
}

} // namespace

const HoodRules& hoodRules(uint32_t hood) {
    for (const HoodRules& rules : kHoodRules)
        if (rules.hood == hood) return rules;
    return kHoodRules[0];
}

int jellybeans(int rawScore) {
    const int score = rawScore > 0 ? rawScore : 0;
    const int beans = static_cast<int>(std::sqrt(double(score) * kScoreToJellyBeansMultiplier));
    return beans > 1 ? beans : 1;
}

int victoryBonus(uint32_t hood, float timeLeft) {
    const int bonus = static_cast<int>(hoodRules(hood).baseBonusOnCompletion + kBonusPerSecondLeft * timeLeft);
    return bonus > 0 ? bonus : 0;
}

// ---- Course ----

bool Course::build(const SectionSelection* selections, int count, int players) {
    numBlocks = numEnemies = numTreasures = numSpawns = numStompers = numSections = 0;
    numPlayers = players < 1 ? 1 : players;
    if (count < 0 || count + 1 > kMaxSections) return false;

    for (const BlockDef& b : kStartBlocks) {
        blocks[numBlocks++] = CourseBlock{&b, kStartSectionX, -1};
    }

    float incrementX = kFirstSectionX;
    for (int i = 0; i <= count; ++i) {
        const bool end = i == count;
        const SectionSelection* sel = end ? nullptr : &selections[i];
        if (sel && sel->sectionIndex >= kNumSectionTypes) return false;
        const SectionTypeDef& type = end ? kEndSection : kSectionTypes[sel->sectionIndex];
        CourseSection& section = sections[numSections++];
        section.originX = incrementX;
        section.length = type.length;
        section.sectionType = end ? -2 : sel->sectionIndex;
        section.firstSpawn = numSpawns;
        section.numSpawns = 0;
        section.numListTreasures = sel ? sel->numTreasures : 0;

        if (numBlocks + type.numBlocks > kMaxBlocks) return false;
        for (int b = 0; b < type.numBlocks; ++b) blocks[numBlocks++] = CourseBlock{&type.blocks[b], incrementX, i};

        if (sel) {
            if (sel->numEnemies > kMaxSectionEnemies || numEnemies + sel->numEnemies > kMaxEnemies) return false;
            for (int e = 0; e < sel->numEnemies; ++e) {
                if (sel->enemies[e] >= type.numEnemies) return false;
                CourseEnemy& enemy = enemies[numEnemies++];
                enemy.def = &type.enemies[sel->enemies[e]];
                enemy.suit = enemy.def->suit;
                enemy.originX = incrementX;
                enemy.height = suitHeight(enemy.suit);
                enemy.section = static_cast<uint8_t>(i);
                enemy.index = static_cast<uint8_t>(e);
                enemy.upDown = enemy.def->moving && std::fabs(enemy.def->final[2] - enemy.def->init[2]) > 0.0f;
            }
            if (sel->numTreasures > kMaxSectionTreasures ||
                numTreasures + sel->numTreasures + sel->numEnemies > kMaxTreasures)
                return false;
            for (int k = 0; k < sel->numTreasures; ++k) {
                if (sel->treasures[k] >= type.numTreasures) return false;
                const float* pos = type.treasures[sel->treasures[k]];
                CourseTreasure& treasure = treasures[numTreasures++];
                treasure = CourseTreasure{};
                treasure.x = incrementX + pos[0];
                treasure.z = pos[2];
                treasure.section = static_cast<uint8_t>(i);
                treasure.index = static_cast<uint8_t>(k);
                treasure.value = sel->treasureValues[k];
            }
            // TwoDTreasureMgr.load: one hidden treasure per selected enemy, worth numPlayers.
            for (int e = 0; e < sel->numEnemies; ++e) {
                CourseTreasure& treasure = treasures[numTreasures++];
                treasure = CourseTreasure{};
                treasure.section = static_cast<uint8_t>(i);
                treasure.index = static_cast<uint8_t>(sel->numTreasures + e);
                treasure.value = static_cast<uint8_t>(numPlayers);
                treasure.enemyGenerated = true;
            }
            if (sel->numStompers > kMaxSectionStompers || numStompers + sel->numStompers > kMaxStompers) return false;
            for (int s = 0; s < sel->numStompers; ++s) {
                if (sel->stompers[s] >= type.numStompers) return false;
                const StomperDef& def = type.stompers[sel->stompers[s]];
                if (def.type >= kNumStomperTypes || kStomperTypes[def.type].numCollSolids < 2) return false;
                CourseStomper& stomper = stompers[numStompers++];
                stomper.type = &kStomperTypes[def.type];
                stomper.x = incrementX + def.pos[0];
                stomper.z = def.pos[2];
                stomper.period = def.period;
                stomper.section = static_cast<uint8_t>(i);
            }
        }

        // Spawn points: the AI's selection; TwoDSectionMgr gives the end section index 0.
        const int numPoints = sel ? sel->numSpawnPoints : 1;
        if (sel && sel->numSpawnPoints > kMaxSectionSpawnPoints) return false;
        if (numSpawns + numPoints > kMaxSpawns) return false;
        for (int p = 0; p < numPoints; ++p) {
            const int index = sel ? sel->spawnPoints[p] : 0;
            if (index >= type.numSpawnPoints) return false;
            const SpawnPointDef& def = type.spawnPoints[index];
            spawns[numSpawns++] = CourseSpawn{def.save[0], def.save[2], def.load[0], def.load[2]};
            section.numSpawns++;
        }

        if (end) {
            endSectionX = incrementX;
            if (section.numSpawns == 0) return false;
            gameEndX = incrementX + spawns[numSpawns - 1].saveX;
        }
        incrementX += type.length;
    }
    gameLength = gameEndX - kGameStartX;
    return true;
}

void Course::blockOffset(int i, float t, float* x, float* z) const {
    const CourseBlock& b = blocks[i];
    const BlockTypeDef& type = kBlockTypes[b.def->type];
    float pos[3];
    pingPong(b.def->init, b.def->final, b.def->moving ? b.def->duration : 0.0f, t, false, pos);
    *x = b.originX + type.pos[0] + pos[0];
    *z = type.pos[2] + pos[2];
}

void Course::enemyPose(int i, float t, float* x, float* z, float* h) const {
    const CourseEnemy& e = enemies[i];
    const EnemyDef& def = *e.def;
    float pos[3];
    pingPong(def.init, def.final, def.moving ? def.duration : 0.0f, t, e.upDown, pos);
    *x = e.originX + pos[0];
    *z = pos[2];
    if (!def.moving) {
        *h = 0.0f;
        return;
    }
    if (e.upDown) {
        *h = 90.0f; // setupEnemy: self.suit.setH(90)
        return;
    }
    // setHeading at the start of each leg: H = -90 * sign(dx), turned in 0.1 s per 90 degrees.
    const float sign = def.final[0] > def.init[0] ? 1.0f : -1.0f;
    const float forwardH = -90.0f * sign;
    const float backH = 90.0f * sign;
    const float cycle = 2.0f * def.duration;
    const float p = positiveFmod(t, cycle);
    const bool forward = p < def.duration;
    const float into = forward ? p : p - def.duration;
    const float target = forward ? forwardH : backH;
    float from = forward ? backH : forwardH;
    if (forward && t < cycle) from = 0.0f; // the Cog starts at H 0
    from = fitSrcAngle2Dest(from, target);
    const float turn = 0.1f * std::fabs(from - target) / 90.0f;
    *h = into < turn ? from + (target - from) * (into / turn) : target;
}

float Course::stomperHeadZ(int i, float t) const {
    const CourseStomper& s = stompers[i];
    const StomperTypeDef& type = *s.type;
    const float range = type.headEndZ - type.headStartZ;
    // The loop is the motion (period) then the stomp sound and smoke (0.4 s); the last motion
    // call was at t = 1, where t % 1 = 0 leaves the head stuck down.
    const float tau = positiveFmod(t, s.period + kStomperSmokeTime);
    const float u = tau < s.period ? tau / s.period : 0.0f;
    if (u < kStomperStickTime) return type.headEndZ;
    if (u < kStomperTurnaround)
        return (u - kStomperStickTime) * -range / (kStomperTurnaround - kStomperStickTime) + type.headEndZ;
    return -range + (u - kStomperTurnaround) * range / (1.0f - kStomperTurnaround) + type.headEndZ;
}

float Course::stomperShaftScale(int i, float t) const {
    const StomperTypeDef& type = *stompers[i].type;
    const float heightDiff = stomperHeadZ(i, t) - type.headStartZ;
    const float range = type.headEndZ - type.headStartZ;
    return type.shaftStartScaleZ + heightDiff * (type.shaftEndScaleZ - type.shaftStartScaleZ) / range;
}

int Course::stomperState(int i, float t) const {
    const CourseStomper& s = stompers[i];
    const float tau = positiveFmod(t, s.period + kStomperSmokeTime);
    const float u = tau < s.period ? tau / s.period : 0.0f;
    if (u < kStomperStickTime) return 3;   // STUCK_DOWN
    if (u < kStomperTurnaround) return 1;  // GOING_UP
    if (u > kStomperTurnaround) return 2;  // GOING_DOWN
    return 1;
}

int Course::findEnemy(int section, int index) const {
    for (int i = 0; i < numEnemies; ++i)
        if (enemies[i].section == section && enemies[i].index == index) return i;
    return -1;
}

int Course::findTreasure(int section, int index) const {
    for (int i = 0; i < numTreasures; ++i)
        if (treasures[i].section == section && treasures[i].index == index) return i;
    return -1;
}

// ---- Simulation ----

void Simulation::init(const Course* course, int startIndex, float headCenterZ) {
    course_ = course;
    // __placeToon: ToonStartingPosition + (avIdList index, 0, 0), H -90.
    x_ = kToonStartingPosition[0] + float(startIndex);
    y_ = kToonStartingPosition[1];
    z_ = kToonStartingPosition[2];
    vz_ = 0.0f;
    h_ = turnFrom_ = turnTo_ = atRestHeading_ = -90.0f;
    turnTime_ = turnDuration_ = 0.0f;
    lastMove_ = moveX_ = 0;
    airborne_ = true;
    jumpHeld_ = false;
    headInFloor_ = false;
    headCenterZ_ = headCenterZ;
    state_ = ToonState::Normal;
    stateTime_ = stateStartT_ = 0.0f;
    stunnedUntil_ = -1.0f;
    shotAt_ = -1.0f;
    lastT_ = 0.0f;
    activeSection_ = 0;
    standingBlock_ = -1;
    for (int i = 0; i < kMaxEnemies; ++i) {
        enemyAlive_[i] = i < course->numEnemies;
        inEnemy_[i] = bulletInEnemy_[i] = false;
    }
    for (int i = 0; i < kMaxTreasures; ++i) {
        treasureHidden_[i] = treasureGrabbed_[i] = inTreasure_[i] = false;
        treasureShowAt_[i] = -1.0f;
        treasurePos_[i][0] = i < course->numTreasures ? course->treasures[i].x : 0.0f;
        treasurePos_[i][1] = i < course->numTreasures ? course->treasures[i].z : 0.0f;
    }
    for (int i = 0; i < kMaxStompers; ++i) {
        stomperStashedUntil_[i] = -1.0f;
        stomperState_[i] = 3; // TwoDStomper starts STUCK_DOWN
    }
    for (int i = 0; i < kMaxSections; ++i) lastSavePoint_[i] = 0;
    for (int i = 0; i < kMaxSpawns; ++i) inSave_[i] = false;
    // TwoDCamera.onstage: camera X = toon X + threeQuarterOffset.
    camX_ = x_ + kCameraOffset;
    camLerpTime_ = -1.0f;
}

float Simulation::fallBackDuration(int frames) {
    // enterFallBack: frames 1..22 retimed so the clip would last 4 s, then 23..end at 24 fps.
    const float newRate = float(frames - 2) / kFallBackAnimTime;
    return 22.0f / newRate + float(frames - 23) / 24.0f;
}

bool Simulation::treasureVisible(int i, float t) const {
    if (treasureHidden_[i] || treasureGrabbed_[i]) return false;
    if (!course_->treasures[i].enemyGenerated) return true;
    return treasureShowAt_[i] >= 0.0f && t >= treasureShowAt_[i];
}

void Simulation::enemyKilled(int enemy, float t) {
    if (enemy < 0 || enemy >= course_->numEnemies || !enemyAlive_[enemy]) return;
    enemyAlive_[enemy] = false;
    // setEnemyShot: the treasure appears where the Cog was, half its height up.
    float ex, ez, eh;
    course_->enemyPose(enemy, t, &ex, &ez, &eh);
    const CourseEnemy& e = course_->enemies[enemy];
    // TwoDTreasureMgr.enemyTreasures[enemyIndex] follow the section's selected treasures.
    const int treasure = course_->findTreasure(e.section, course_->sections[e.section].numListTreasures + e.index);
    if (treasure < 0) return;
    treasurePos_[treasure][0] = ex;
    treasurePos_[treasure][1] = ez + e.height / 2.0f;
    treasureShowAt_[treasure] = t + kEnemyTreasurePopup;
}

void Simulation::hideTreasure(int treasure) {
    if (treasure >= 0 && treasure < course_->numTreasures) treasureHidden_[treasure] = true;
}

void Simulation::respawnPoint(float* x, float* z) const {
    // TwoDSectionMgr.getLastSpawnPoint: the active section's load point (or the toon's
    // starting position) plus that section's X.
    const CourseSection& section = course_->sections[activeSection_];
    if (section.numSpawns > 0) {
        const CourseSpawn& spawn = course_->spawns[section.firstSpawn + lastSavePoint_[activeSection_]];
        *x = spawn.loadX + section.originX;
        *z = spawn.loadZ;
    } else {
        *x = kToonStartingPosition[0] + section.originX;
        *z = kToonStartingPosition[2];
    }
}

void Simulation::enterState(ToonState s, float t) {
    state_ = s;
    stateStartT_ = t;
    stateTime_ = 0.0f;
    switch (s) {
    case ToonState::FallBack: {
        vz_ = kFallBackLift;
        airborne_ = true;
        fallBackFromX_ = x_;
        fallBackToX_ = h_ == 90.0f ? x_ + kFallBackDist : x_ - kFallBackDist;
        break;
    }
    case ToonState::FallDown:
        respawnDone_ = false;
        break;
    case ToonState::Squish:
        stunnedUntil_ = t + kStunTime;
        break;
    case ToonState::Victory:
        victoryWait_ = z_ > 13.0f ? 1.0f : 0.0f; // enterVictory: waitToLand
        break;
    case ToonState::Normal:
        break;
    }
}

void Simulation::updateHeading(int move, float dt) {
    // TwoDDrive.__handleHeading / orientToon.
    if (move != lastMove_) {
        const float angle = move ? (move > 0 ? -90.0f : 90.0f) : atRestHeading_;
        if (move) {
            if (angle != atRestHeading_) {
                atRestHeading_ = angle;
                // avatarOrientationChanged -> TwoDCamera.setupChangeFacingInterval
                camLerpTime_ = 0.0f;
                camLerpHeading_ = angle;
            }
        }
        turnFrom_ = fitSrcAngle2Dest(h_, angle);
        turnTo_ = angle;
        turnDuration_ = 0.1f * std::fabs(turnFrom_ - angle) / 90.0f;
        turnTime_ = 0.0f;
    }
    lastMove_ = move;
    turnTime_ += dt;
    h_ = turnDuration_ > 0.0f && turnTime_ < turnDuration_
             ? turnFrom_ + (turnTo_ - turnFrom_) * (turnTime_ / turnDuration_)
             : turnTo_;
}

void Simulation::updateCamera(float dt) {
    if (camLerpTime_ >= 0.0f) {
        // myLerpPos: X moves a growing fraction of the way each frame, then gives up control.
        camLerpTime_ += dt;
        const float u = camLerpTime_ >= kCameraLerpTime ? 1.0f : camLerpTime_ / kCameraLerpTime;
        const float finalX = x_ - std::sin(camLerpHeading_ * kDegToRad) * kCameraOffset;
        camX_ += (finalX - camX_) * u;
        if (std::fabs(camX_ - finalX) < 0.01f || u >= 1.0f) {
            camX_ = finalX;
            camLerpTime_ = -1.0f;
        }
        return;
    }
    camX_ = x_ - std::sin(h_ * kDegToRad) * kCameraOffset;
}

bool Simulation::bulletLive(float t, float* bx, float* bz) const {
    if (shotAt_ < 0.0f) return false;
    const float into = t - shotAt_ - kBulletDelay;
    if (into < 0.0f || into >= kBulletTravel) return false;
    const float y = kBulletStartY + (kBulletEndY - kBulletStartY) * (into / kBulletTravel);
    *bx = x_ - std::sin(h_ * kDegToRad) * y;
    *bz = z_ + kBulletZ;
    return true;
}

void Simulation::collide(float t) {
    // Wall sphere (CollisionHandlerPusher): every WallBitmask polygon, then the tangible Cog
    // spheres.
    float cx = x_, cz = z_ + kAvatarRadius;
    const float contactX = cx;
    const float contactZ = cz;
    bool stashed[kMaxStompers];
    for (int i = 0; i < course_->numStompers; ++i) stashed[i] = t < stomperStashedUntil_[i];
    auto pushKind = [&](SegKind wanted) {
        forEachSegment(*course_, stashed, t, x_, [&](const Segment& s, float ox, float oz, float scale) {
            if (s.kind != wanted) return;
            const float depth = pushDepth(s, ox, oz, scale, contactX, contactZ, kAvatarRadius);
            if (depth > 0.0f) {
                cx += s.nx * depth;
                cz += s.nz * depth;
            }
        });
    };
    // CollisionHandlerPusher resolves the model's `wall` node before `floor1`.  Preserve
    // that ordering: a toon brushing a platform's vertical face during a jump is pushed
    // sideways before the underside clamps the body.
    pushKind(SegKind::Wall);
    pushKind(SegKind::Ceiling);
    for (int i = 0; i < course_->numEnemies; ++i) {
        if (!enemyAlive_[i]) continue;
        float ex, ez, eh;
        course_->enemyPose(i, t, &ex, &ez, &eh);
        ez += kEnemySphereZ;
        const float dx = cx - ex, dz = cz - ez;
        const float dist = std::sqrt(dx * dx + dz * dz);
        const float r = kAvatarRadius + kEnemySphereRadius;
        if (dist < r && dist > 1e-4f) {
            cx += dx / dist * (r - dist);
            cz += dz / dist * (r - dist);
        }
    }
    x_ = cx;
    z_ = cz - kAvatarRadius;
}

void Simulation::lift(float dt, float t, Events* ev) {
    // CollisionHandlerGravity with the ray from far above: the highest FloorBitmask polygon
    // under the toon that is less than offset + reach above its feet, or the drop plane.
    float best = kDropPlaneZ;
    int bestBlock = -1;
    for (int i = 0; i < course_->numBlocks; ++i) {
        float bx, bz;
        course_->blockOffset(i, t, &bx, &bz);
        for (const Segment& s : kBlockTypes[course_->blocks[i].def->type].segments) {
            if (s.kind != SegKind::Floor || s.nz <= 0.0f) continue;
            if (x_ < bx + s.x1 || x_ > bx + s.x2) continue;
            const float fz = bz + s.z1;
            if (fz - z_ < kFloorOffset + kReach && fz > best) {
                best = fz;
                bestBlock = i;
            }
        }
    }
    for (int i = 0; i < course_->numStompers; ++i) {
        const CourseStomper& st = course_->stompers[i];
        for (const Segment& s : kStomperBlockSegments) {
            if (s.kind != SegKind::Floor) continue;
            if (x_ < st.x + s.x1 || x_ > st.x + s.x2) continue;
            const float fz = st.z + kStomperBlockZ * st.type->scale + s.z1;
            if (fz - z_ < kFloorOffset + kReach && fz > best) {
                best = fz;
                bestBlock = -1;
            }
        }
    }
    float adjust = best - z_ + kFloorOffset;
    if (vz_ > 0.0f || adjust < 0.0f) {
        const float gravityAdjust = vz_ * dt - 0.5f * kGravity * dt * dt;
        if (adjust > gravityAdjust) {
            z_ += adjust;
            vz_ = 0.0f;
            airborne_ = false;
        } else {
            z_ += gravityAdjust;
            vz_ -= kGravity * dt;
            airborne_ = true;
        }
    } else {
        z_ += adjust;
        vz_ = 0.0f;
        airborne_ = false;
    }
    // MovingPlatform: a toon standing on a moving block rides it.
    if (!airborne_ && bestBlock >= 0 && course_->blocks[bestBlock].def->moving) {
        standingBlock_ = bestBlock;
        course_->blockOffset(bestBlock, t, &standingX_, &standingZ_);
    } else {
        standingBlock_ = -1;
    }
    (void)ev;
}

void Simulation::events(float t, Events* ev) {
    const float ex = x_, ez = z_ + kEventSphereZ;

    // Treasures (TwoDTreasure.__handleEnterSphere).
    for (int i = 0; i < course_->numTreasures; ++i) {
        const bool inside = treasureVisible(i, t) &&
                            circlesOverlap(ex, ez, kEventSphereRadius, treasurePos_[i][0], treasurePos_[i][1],
                                           kTreasureRadius);
        if (inside && !inTreasure_[i] && ev->treasureGrabs < 4) {
            treasureGrabbed_[i] = true;
            ev->treasureSection[ev->treasureGrabs] = course_->treasures[i].section;
            ev->treasureIndex[ev->treasureGrabs] = course_->treasures[i].index;
            ev->treasureGrabs++;
        }
        inTreasure_[i] = inside;
    }

    // Save points: the last one of the end section is the finish.
    for (int s = 0; s < course_->numSections; ++s) {
        const CourseSection& section = course_->sections[s];
        for (int p = 0; p < section.numSpawns; ++p) {
            const int index = section.firstSpawn + p;
            const CourseSpawn& spawn = course_->spawns[index];
            const bool inside = circlesOverlap(ex, ez, kEventSphereRadius, section.originX + spawn.saveX, spawn.saveZ,
                                               kSavePointRadius);
            if (inside && !inSave_[index]) {
                if (section.sectionType == -2 && p == section.numSpawns - 1) {
                    // localToonVictory
                    ev->victory = true;
                    if (state_ != ToonState::Victory && allowed(state_, ToonState::Victory))
                        enterState(ToonState::Victory, t);
                } else {
                    lastSavePoint_[s] = p;
                    activeSection_ = s;
                }
            }
            inSave_[index] = inside;
        }
    }

    // Cogs: the event sphere entering a Cog's sphere (TwoDEnemy.handleEnemyCollision).
    float bx = 0.0f, bz = 0.0f;
    const bool bullet = bulletLive(t, &bx, &bz);
    for (int i = 0; i < course_->numEnemies; ++i) {
        float cx, cz, ch;
        course_->enemyPose(i, t, &cx, &cz, &ch);
        cz += kEnemySphereZ;
        const bool alive = enemyAlive_[i];
        const bool inside = alive && circlesOverlap(ex, ez, kEventSphereRadius, cx, cz, kEnemySphereRadius);
        if (inside && !inEnemy_[i]) {
            // localToonHitByEnemy
            if (state_ != ToonState::FallBack && state_ != ToonState::Squish) {
                ev->hitByEnemy = true;
                if (allowed(state_, ToonState::FallBack)) enterState(ToonState::FallBack, t);
            }
        }
        inEnemy_[i] = inside;
        // TwoDBattleMgr.handleBulletCollision
        const bool hit = alive && bullet && circlesOverlap(bx, bz, kBulletRadius, cx, cz, kEnemySphereRadius);
        if (hit && !bulletInEnemy_[i] && ev->enemyShots < 4) {
            ev->shotSection[ev->enemyShots] = course_->enemies[i].section;
            ev->shotIndex[ev->enemyShots] = course_->enemies[i].index;
            ev->enemyShots++;
        }
        bulletInEnemy_[i] = hit;
    }
}

void Simulation::step(const Input& in, float t, float dt, Events* ev) {
    *ev = Events{};
    stateTime_ = t - stateStartT_;
    const bool inputs = state_ == ToonState::Normal;

    // TwoDGameToonSD timers.
    if (state_ == ToonState::FallBack && stateTime_ >= fallBackDuration(fallBackFrames_)) {
        enterState(ToonState::Normal, t);
    } else if (state_ == ToonState::FallDown && !respawnDone_ && stateTime_ >= kFallDownRespawn) {
        respawnPoint(&x_, &z_);
        respawnDone_ = true;
        ev->respawned = true;
        enterState(ToonState::Normal, t); // the resume Func follows the respawn at once
    } else if (state_ == ToonState::Squish && stateTime_ >= kSquishTime) {
        enterState(ToonState::Normal, t);
    }

    if (state_ == ToonState::Victory) {
        // enterVictory: wait to land, run to the elevator front, then dance inside.
        const float run = stateTime_ - victoryWait_;
        const float front[3] = {course_->endSectionX + kExitElevatorPos[0] + kElevatorFront[0],
                                kExitElevatorPos[1] + kElevatorFront[1], kExitElevatorPos[2] + kElevatorFront[2]};
        const float inside[3] = {course_->endSectionX + kExitElevatorPos[0] + kElevatorInside[0],
                                 kExitElevatorPos[1] + kElevatorInside[1], kExitElevatorPos[2] + kElevatorInside[2]};
        moveX_ = 0;
        if (run < 0.0f) {
            lift(dt, t, ev);
            victoryFrom_[0] = x_;
            victoryFrom_[1] = y_;
            victoryFrom_[2] = z_;
        } else if (run < kTimeToRunToElevator) {
            const float u = run / kTimeToRunToElevator;
            x_ = victoryFrom_[0] + (front[0] - victoryFrom_[0]) * u;
            y_ = victoryFrom_[1] + (front[1] - victoryFrom_[1]) * u;
            z_ = victoryFrom_[2] + (front[2] - victoryFrom_[2]) * u;
        } else {
            const float u = std::fmin(1.0f, (run - kTimeToRunToElevator) / 2.0f);
            x_ = front[0] + (inside[0] - front[0]) * u;
            y_ = front[1] + (inside[1] - front[1]) * u;
            z_ = front[2] + (inside[2] - front[2]) * u;
        }
        updateCamera(dt);
        lastT_ = t;
        return;
    }

    // TwoDDrive.__update (inputs are on only in 'normal').
    const int move = inputs ? (in.move > 0 ? 1 : in.move < 0 ? -1 : 0) : 0;
    moveX_ = move;
    if (inputs) {
        if (!in.jump) {
            jumpHeld_ = false;
        } else if (!jumpHeld_) {
            jumpHeld_ = true;
            // TwoDWalker.jumpPressed: only from the ground, never with the head in a block.
            if (!headInFloor_ && !airborne_) {
                vz_ += kJumpForce;
                airborne_ = true;
                ev->jumped = true;
            }
        }
        if (in.shoot && !shooting(t)) shotAt_ = t;
        updateHeading(move, dt);
    } else {
        jumpHeld_ = in.jump;
        updateHeading(0, dt);
    }
    x_ += float(move) * kToonSpeed * dt;
    if (state_ == ToonState::FallBack) {
        // enemyHitTrajectory: LerpFunc(setX) over 0.75 s.
        const float u = std::fmin(1.0f, stateTime_ / kFallBackMoveTime);
        x_ = fallBackFromX_ + (fallBackToX_ - fallBackFromX_) * u;
    }

    // MovingPlatform: carry the toon by its block's motion since the last step.
    if (standingBlock_ >= 0) {
        float bx, bz;
        course_->blockOffset(standingBlock_, t, &bx, &bz);
        x_ += bx - standingX_;
        z_ += bz - standingZ_;
    }

    // TwoDStomper.checkSquashedToon (interval loop), when a stomper starts down.
    for (int i = 0; i < course_->numStompers; ++i) {
        const int state = course_->stomperState(i, t);
        if (state == 2 && stomperState_[i] != 2) {
            const CourseStomper& st = course_->stompers[i];
            const float scale = st.type->scale;
            const float xDiff = (x_ - st.x) / scale;
            const float bottom = st.z + st.type->headEndZ * scale;
            const float headZ = st.z + course_->stomperHeadZ(i, t) * scale;
            if (xDiff > -1.0f && xDiff < 1.0f && z_ > bottom && z_ < headZ && !stunned(t)) {
                stomperStashedUntil_[i] = t + kStomperStashTime;
                // localToonSquished
                if (state_ != ToonState::FallBack && state_ != ToonState::Squish) {
                    ev->squished = true;
                    if (allowed(state_, ToonState::Squish)) enterState(ToonState::Squish, t);
                }
            }
        }
        stomperState_[i] = state;
    }

    // __updateLocalToonTask: camera, then below z -2 the toon fell.
    updateCamera(dt);
    if (z_ < kFallDownZ && state_ != ToonState::FallDown) {
        ev->fellDown = true;
        if (allowed(state_, ToonState::FallDown)) enterState(ToonState::FallDown, t);
    }

    // The main traverser ('early-event-sphere 1' in Configrc): the head sphere, the event
    // sphere and the bullet see the driven position before the wall pusher moves it.
    bool head = false;
    bool stashed[kMaxStompers];
    for (int i = 0; i < course_->numStompers; ++i) stashed[i] = t < stomperStashedUntil_[i];
    forEachSegment(*course_, stashed, t, x_, [&](const Segment& s, float ox, float oz, float scale) {
        if (s.kind == SegKind::Ceiling && circleTouchesSegment(s, ox, oz, scale, x_, z_ + headCenterZ_,
                                                                kHeadSphereRadius))
            head = true;
    });
    if (head && !headInFloor_ && vz_ > 0.0f) {
        // __handleHeadCollisionIntoFloor: the jump stops.
        vz_ = 0.0f;
        ev->headBump = true;
    }
    headInFloor_ = head;
    events(t, ev);
    collide(t);
    // base.shadowTrav: the gravity ray after the pusher.
    lift(dt, t, ev);
    lastT_ = t;
}

// ---- autopilot (test only) ----

namespace {

struct Rollout {
    float x = 0.0f;
    bool failed = false;
    float survived = 0.0f;
};

Rollout rollout(Simulation sim, const Input& first, float hold, float t, float horizon, float dt) {
    Rollout r;
    Events ev;
    const float startX = sim.x();
    float elapsed = 0.0f;
    while (elapsed < horizon) {
        Input in;
        if (elapsed < hold) in = first;
        t += dt;
        elapsed += dt;
        sim.step(in, t, dt, &ev);
        if (ev.fellDown || ev.hitByEnemy || ev.squished) {
            r.failed = true;
            break;
        }
        if (ev.victory) {
            r.x = 1e6f;
            return r;
        }
    }
    r.survived = elapsed;
    r.x = sim.x() - startX;
    return r;
}

} // namespace

Input autopilot(const Simulation& sim, float t, float dt) {
    Input best;
    if (sim.state() != ToonState::Normal) return best;
    (void)dt;
    struct Candidate {
        int move;
        bool jump;
        float hold;
    };
    static const Candidate kCandidates[] = {
        {1, false, 0.3f}, {1, true, 1.0f}, {1, true, 0.6f}, {1, false, 1.0f}, {0, false, 0.3f},
        {-1, false, 0.3f}, {-1, true, 1.0f}, {0, true, 0.3f}, {1, true, 0.3f},
    };
    constexpr float kStep = 1.0f / 15.0f;
    constexpr float kHorizon = 2.4f;
    float bestScore = -1e9f;
    for (const Candidate& c : kCandidates) {
        Input in;
        in.move = c.move;
        in.jump = c.jump;
        const Rollout r = rollout(sim, in, c.hold, t, kHorizon, kStep);
        const float score = r.failed ? -1000.0f + r.survived : r.x;
        if (score > bestScore + 0.01f) {
            bestScore = score;
            best = in;
        }
    }
    // Squirt any Cog ahead within the bullet's reach.
    const Course& course = sim.course();
    const float facing = -std::sin(sim.heading() * kDegToRad);
    for (int i = 0; i < course.numEnemies; ++i) {
        if (!sim.enemyAlive(i)) continue;
        float ex, ez, eh;
        course.enemyPose(i, t, &ex, &ez, &eh);
        const float ahead = (ex - sim.x()) * facing;
        if (ahead > 0.0f && ahead < kBulletEndY + kEnemySphereRadius &&
            std::fabs(ez + kEnemySphereZ - (sim.z() + kBulletZ)) < kEnemySphereRadius + kBulletRadius)
            best.shoot = true;
    }
    return best;
}

} // namespace tt::twodgame
