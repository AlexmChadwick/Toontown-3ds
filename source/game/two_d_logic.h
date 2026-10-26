// Toon Escape (DistributedTwoDGame, minigame 15) without 3DS headers, host tested in
// tools/games2_test against the upstream Python.
//
// - Course: TwoDSectionMgr / TwoDSection, built from the AI's setSectionsSelected, with the
//   block, Cog, stomper and treasure motion of TwoDBlock / TwoDEnemy / TwoDStomper as
//   functions of the game time (every interval is started at setGameStart's elapsed time).
// - Walker: TwoDDrive on LocalAvatar's 'twoD' GravityWalker controls in the X/Z plane: the
//   wall sphere pushed out of WallBitmask polygons, the gravity ray onto FloorBitmask ones,
//   the event sphere for treasures / save points / Cogs and the head sphere that stops a
//   jump under a block ('floor1').
// - Simulation: one local toon's TwoDGameToonSD states (normal, fallBack, fallDown, squish,
//   victory), the squirt gun bullet of TwoDBattleMgr and the stomper squash check, reporting
//   what DistributedTwoDGame sends as events.
//
// Collision outlines come from the models (tools/gen_toonblitz_tables.py). Every polygon in
// them spans y = 0, so a 2D outline is exact for a toon held at y = 0.
#pragma once

#include <cstdint>

#include "game/two_d_tables.h"

namespace tt::twodgame {

// ---- upstream code constants ----
constexpr float kStartSectionX = -48.0f;    // TwoDSectionMgr.setupStartSection: setX(-48)
constexpr float kFirstSectionX = -24.0f;    // TwoDSectionMgr.setupSections: incrementX = -24
constexpr float kGameStartX = -40.0f;       // DistributedTwoDGame.load: gameStartX
constexpr float kStartWallPos[3] = {-28.0f, 0.0f, 4.0f};  // setupStartSection (start section space)
constexpr float kStartPipePos[3] = {12.0f, 0.0f, 44.0f};
constexpr float kStartArrowPos[3] = {23.0f, 1.5f, 12.76f};
constexpr float kEndWallPos[3] = {100.0f, 0.0f, 4.0f};    // setupEndSection (end section space)
constexpr float kEndArrowPos[3] = {6.0f, 1.5f, 12.76f};
constexpr float kExitElevatorPos[3] = {52.0f, -2.0f, 12.7f};
constexpr float kWallScale = 0.8f;
constexpr float kElevatorDoorTravel = 3.02f; // getElevatorCloseTrack: doors lerp to +-3.02
constexpr float kElevatorDoorTime = 2.0f;
constexpr float kDropPlaneZ = -50.0f;       // ToonBlitzAssetMgr dropPlane (FloorBitmask)
constexpr float kFallDownZ = -2.0f;         // __updateLocalToonTask: getZ() < -2.0
constexpr float kToonSpeed = 12.0f;         // __defineConstants: TOON_SPEED
constexpr float kTimeToRunToElevator = 1.5f;
constexpr float kJumpForce = 30.0f;         // TwoDDrive: avatarControlJumpForce
constexpr float kGravity = 64.348f;         // GravityWalker(gravity = 64.348)
constexpr float kAvatarRadius = 1.4f;       // LocalAvatar.setupControls(avatarRadius = 1.4)
constexpr float kFloorOffset = 0.025f;      // OTPGlobals.FloorOffset
constexpr float kReach = 4.0f;              // setupControls(reach = 4.0)
constexpr float kEventSphereZ = kAvatarRadius - 0.1f;      // GravityWalker.setupEventSphere
constexpr float kEventSphereRadius = kAvatarRadius * 1.04f;
constexpr float kHeadSphereRadius = 1.0f;   // DistributedTwoDGame.setupHeadCollision
constexpr float kEnemySphereZ = 2.0f;       // TwoDEnemy.setupCollision: CollisionSphere(0, 0, 2, 2)
constexpr float kEnemySphereRadius = 2.0f;
constexpr float kTreasureRadius = 1.3f;     // TwoDTreasure.RADIUS
constexpr float kSavePointRadius = 3.0f;    // TwoDSpawnPointMgr.setupCollision
constexpr float kTreasureScale = 2.5f;      // TwoDTreasure: model.setScale(2.5)
constexpr float kEnemyTreasurePopup = 2.4f; // TwoDTreasure.popupEnemyTreasure: Wait(2.4)
// TwoDBattleMgr: a 0.2-scaled unit sphere lerped from (0, 1, 3) to (0, 11, 3) in 0.25 s after 0.15 s.
constexpr float kBulletRadius = 0.2f;
constexpr float kBulletStartY = 1.0f;
constexpr float kBulletEndY = 11.0f;        // start + SHOOT_DISTANCE (10)
constexpr float kBulletZ = 3.0f;
constexpr float kBulletDelay = 0.15f;
constexpr float kBulletTravel = 0.25f;
// getToonShootTrack: water-gun frames 48-58 at 24 fps then 107-126 at playRate 2 (ActorInterval
// duration = (endFrame - startFrame + 1) / (24 * playRate)); a new shot waits for the track.
constexpr float kShootTrackDuration = 11.0f / 24.0f + 20.0f / 48.0f;
// TwoDGameToonSD
constexpr float kFallBackDist = 9.0f;
constexpr float kFallBackLift = 12.0f;      // lifter.setVelocity(12)
constexpr float kFallBackMoveTime = 0.75f;  // enemyHitTrajectory
constexpr float kFallBackAnimTime = 4.0f;   // enterFallBack: duration = 4
constexpr float kFallDownRespawn = 1.0f;    // enterFallDown: Wait(1), setPos(spawn)
constexpr float kSquishTime = 2.5f;         // enterSquish: Wait(2.5), resume
constexpr float kStunTime = 4.0f;           // Toon.stunToon: eight 0.5 s colour lerps
constexpr float kStomperStashTime = 2.5f;   // checkSquashedToon: Wait(2.5), unstash
constexpr float kStomperSmokeTime = 0.4f;   // getSmokeTrack, after the motion in the loop
constexpr float kStomperStickTime = 0.2f;   // getMotionIval.motionFunc: stickTime
constexpr float kStomperTurnaround = 0.95f; //                            turnaround
constexpr float kCameraOffset = 2.0f;       // TwoDCamera.threeQuarterOffset
constexpr float kCameraLerpTime = 5.0f;     // setupChangeFacingInterval: LerpFunc(duration=5.0)

constexpr int kMaxSections = 8;             // NumSections is 5 in every hood
constexpr int kMaxBlocks = 192;
constexpr int kMaxEnemies = 40;
constexpr int kMaxTreasures = 160;
constexpr int kMaxSpawns = 40;
constexpr int kMaxStompers = 40;
constexpr int kMaxPlayers = 4;

const HoodRules& hoodRules(uint32_t hood); // TTC's rules for an unknown hood

// One twoDSectionInfo of setSectionsSelected.
struct SectionSelection {
    uint8_t sectionIndex = 0;
    uint8_t numEnemies = 0;
    uint8_t enemies[kMaxSectionEnemies] = {};
    uint8_t numTreasures = 0;
    uint8_t treasures[kMaxSectionTreasures] = {};
    uint8_t treasureValues[kMaxSectionTreasures] = {};
    uint8_t numSpawnPoints = 0;
    uint8_t spawnPoints[kMaxSectionSpawnPoints] = {};
    uint8_t numStompers = 0;
    uint8_t stompers[kMaxSectionStompers] = {};
};

struct CourseBlock {
    const BlockDef* def = nullptr;
    float originX = 0.0f; // section node X
    int section = -1;     // -1 for the start section
};

struct CourseEnemy {
    const EnemyDef* def = nullptr;
    const char* suit = "";
    float originX = 0.0f;
    float height = 0.0f;  // Suit.getHeight (SuitDNA tables)
    uint8_t section = 0;
    uint8_t index = 0;    // index in the section's selected list (claimEnemyShot)
    bool upDown = false;  // abs(finalZ - initZ) > 0: propeller Cog, easeInOut, faces H 90
};

struct CourseTreasure {
    float x = 0.0f, z = 0.0f;
    uint8_t section = 0;
    uint8_t index = 0;    // the section's treasure list, then one per enemy (TwoDTreasureMgr)
    uint8_t value = 1;
    bool enemyGenerated = false; // hidden until its Cog dies (TwoDTreasureMgr.enemyTreasures)
};

struct CourseSpawn {
    float saveX = 0.0f, saveZ = 0.0f;
    float loadX = 0.0f, loadZ = 0.0f; // section-relative, as getLastSpawnPoint adds the X later
};

struct CourseSection {
    float originX = 0.0f;
    float length = 0.0f;
    int sectionType = -1; // -2 for the end section
    int firstSpawn = 0, numSpawns = 0;
    int numListTreasures = 0; // selected treasures; the enemy ones are numbered after them
};

struct CourseStomper {
    const StomperTypeDef* type = nullptr;
    float x = 0.0f, z = 0.0f; // world
    float period = 1.0f;
    uint8_t section = 0;
};

class Course {
public:
    bool build(const SectionSelection* selections, int count, int numPlayers);

    // Interval positions at game time t (seconds since setGameStart).
    void blockOffset(int i, float t, float* x, float* z) const;
    void enemyPose(int i, float t, float* x, float* z, float* h) const;
    float stomperHeadZ(int i, float t) const;    // model units (TwoDStomper head z)
    float stomperShaftScale(int i, float t) const;
    int stomperState(int i, float t) const;      // 1 going up, 2 going down, 3 stuck down

    CourseBlock blocks[kMaxBlocks];
    int numBlocks = 0;
    CourseEnemy enemies[kMaxEnemies];
    int numEnemies = 0;
    CourseTreasure treasures[kMaxTreasures];
    int numTreasures = 0;
    CourseSpawn spawns[kMaxSpawns];
    int numSpawns = 0;
    CourseStomper stompers[kMaxStompers];
    int numStompers = 0;
    CourseSection sections[kMaxSections];
    int numSections = 0;     // selected sections plus the end section
    float endSectionX = 0.0f;
    float gameEndX = 0.0f;   // the end section's last save point (TwoDSpawnPointMgr.gameEndX)
    float gameLength = 0.0f;
    int numPlayers = 1;

    int findEnemy(int section, int index) const;
    int findTreasure(int section, int index) const;
};

// TwoDGameToonSD states of the local toon.
enum class ToonState : uint8_t { Normal, FallBack, FallDown, Squish, Victory };

// What DistributedTwoDGame would send / show this step (cleared by step()).
struct Events {
    bool jumped = false;
    bool headBump = false;      // assetMgr.playHeadCollideSound
    bool fellDown = false;      // localToonFellDown -> toonFellDown
    bool hitByEnemy = false;    // localToonHitByEnemy -> toonHitByEnemy
    bool squished = false;      // localToonSquished -> toonSquished
    bool victory = false;       // localToonVictory -> toonVictory
    bool respawned = false;
    int treasureGrabs = 0;      // __treasureGrabbed -> claimTreasure(section, index)
    uint8_t treasureSection[4] = {}, treasureIndex[4] = {};
    int enemyShots = 0;         // __enemyShot -> claimEnemyShot(section, index)
    uint8_t shotSection[4] = {}, shotIndex[4] = {};
};

struct Input {
    int move = 0;          // -1 left, 0, +1 right (ArrowKeys left/right)
    bool jump = false;     // up arrow held (one jump per press)
    bool shoot = false;    // shoot key pressed this step
};

class Simulation {
public:
    // headCenterZ: head sphere centre above the feet (shoulder height plus setupHeadCollision's
    // per-species offset). startIndex: avIdList index (__placeToon adds it to X).
    void init(const Course* course, int startIndex, float headCenterZ);
    // One frame at game time t (after the step). Inputs are ignored outside 'normal'.
    void step(const Input& in, float t, float dt, Events* ev);
    // Server said the enemy is dead (setEnemyShot health <= 0): death track and its treasure.
    void enemyKilled(int enemy, float t);
    // setTreasureGrabbed by another toon: hideTreasure.
    void hideTreasure(int treasure);

    float x() const { return x_; }
    float z() const { return z_; }
    float y() const { return y_; }
    float heading() const { return h_; }
    bool airborne() const { return airborne_; }
    bool moving() const { return moveX_ != 0; }
    ToonState state() const { return state_; }
    float stateTime() const { return stateTime_; }
    bool shooting(float t) const { return shotAt_ >= 0.0f && t - shotAt_ < kShootTrackDuration; }
    float shotAt() const { return shotAt_; }
    bool stunned(float t) const { return t < stunnedUntil_; }
    int activeSection() const { return activeSection_; }
    // fallBack interval length for a slip-backward clip of `frames` frames (24 fps).
    static float fallBackDuration(int frames);
    void setFallBackFrames(int frames) { fallBackFrames_ = frames; }
    // TwoDCamera X (it keeps 53 units in front at y -53, z 17.3, looking along +Y).
    float cameraX() const { return camX_; }
    bool bulletLive(float t, float* bx, float* bz) const;
    const Course& course() const { return *course_; }
    bool enemyAlive(int i) const { return enemyAlive_[i]; }
    bool treasureVisible(int i, float t) const;
    bool treasureGrabbed(int i) const { return treasureGrabbed_[i]; }
    float treasureX(int i) const { return treasurePos_[i][0]; }
    float treasureZ(int i) const { return treasurePos_[i][1]; }
    bool stomperStashed(int i, float t) const { return t < stomperStashedUntil_[i]; }

private:
    void collide(float t);
    void lift(float dt, float t, Events* ev);
    void events(float t, Events* ev);
    void enterState(ToonState s, float t);
    void respawnPoint(float* x, float* z) const;
    void updateHeading(int move, float dt);
    void updateCamera(float dt);
    bool overlapsSegment(float cx, float cz, float r, float x1, float z1, float x2, float z2) const;

    const Course* course_ = nullptr;
    float x_ = 0.0f, y_ = 0.0f, z_ = 0.0f, vz_ = 0.0f;
    float h_ = -90.0f;          // TwoDDrive heading (-90 faces +X)
    float turnFrom_ = -90.0f, turnTo_ = -90.0f, turnTime_ = 0.0f, turnDuration_ = 0.0f;
    float atRestHeading_ = -90.0f;
    int lastMove_ = 0, moveX_ = 0;
    bool airborne_ = false;
    bool jumpHeld_ = false;     // TwoDDrive.wasUpReleased
    bool headInFloor_ = false;
    float headCenterZ_ = 3.0f;
    ToonState state_ = ToonState::Normal;
    float stateTime_ = 0.0f;
    float stateStartT_ = 0.0f;
    float fallBackFromX_ = 0.0f, fallBackToX_ = 0.0f;
    int fallBackFrames_ = 67;   // slip-backward
    bool respawnDone_ = false;
    float victoryFrom_[3] = {}, victoryWait_ = 0.0f;
    float stunnedUntil_ = -1.0f;
    float shotAt_ = -1.0f;
    float lastT_ = 0.0f;
    int activeSection_ = 0;
    int standingBlock_ = -1;
    float standingX_ = 0.0f, standingZ_ = 0.0f;
    // Per-game state of the course objects.
    bool enemyAlive_[kMaxEnemies] = {};
    bool treasureHidden_[kMaxTreasures] = {};  // hideTreasure (grabbed by anyone)
    bool treasureGrabbed_[kMaxTreasures] = {}; // our claim sent: TwoDTreasure.ignoreAll
    float treasureShowAt_[kMaxTreasures] = {}; // enemy treasures: popup time, < 0 hidden
    float treasurePos_[kMaxTreasures][2] = {};  // x, z (enemy treasures move to their Cog)
    float stomperStashedUntil_[kMaxStompers] = {};
    int stomperState_[kMaxStompers] = {};
    int lastSavePoint_[kMaxSections] = {};
    bool inEnemy_[kMaxEnemies] = {};
    bool inTreasure_[kMaxTreasures] = {};
    bool bulletInEnemy_[kMaxEnemies] = {};
    bool inSave_[kMaxSpawns] = {};
    float camX_ = 0.0f;
    float camLerpTime_ = -1.0f;
    float camLerpHeading_ = -90.0f;
};

// DistributedTwoDGameAI.gameOver: max(1, int(sqrt(max(0, score) * ScoreToJellyBeansMultiplier))).
int jellybeans(int rawScore);
// DistributedTwoDGameAI.toonVictory: int(BaseBonusOnCompletion + BonusPerSecondLeft * timeLeft), >= 0.
int victoryBonus(uint32_t hood, float timeLeft);

// Test-only controller. No heap allocation or modification of the live Simulation.
class Autopilot {
public:
    // Search tuning, not upstream game rules. A 4.4-second horizon at the app's 30 Hz.
    static constexpr int kBeamWidth = 8;
    static constexpr int kMacroFrames = 6;
    static constexpr int kSearchDepth = 22;
    static constexpr int kPlanFrames = kMacroFrames * kSearchDepth;
    static constexpr int kStepsPerFrame = 64;

    Input input(const Simulation& sim, float t, float dt);
    void reset();
    int stepsLastFrame() const { return stepsLastFrame_; }
    float predictedX() const { return predictedX_; }
    float predictedZ() const { return predictedZ_; }
    void copyPredictedPath(uint8_t* out, int capacity) const;

private:
    struct Node {
        Simulation sim;
        uint8_t path[kSearchDepth] = {};
        float score = 0.0f;
        int enemyHealth[kMaxEnemies] = {};
    };

    void beginSearch(const Simulation& root, float nextTime, float dt);
    void expand();
    void retainCandidate();
    void publish();

    Node beam_[2][kBeamWidth];
    Node candidate_;
    Simulation tail_;
    uint8_t queue_[2 * kPlanFrames] = {};
    int queued_ = 0;
    int cursor_ = 0;
    int current_ = 0;
    int count_ = 0;
    int nextCount_ = 0;
    int depth_ = 0;
    int node_ = 0;
    int action_ = 0;
    int frame_ = 0;
    int projection_ = 0;
    int stepsLastFrame_ = 0;
    float times_[kPlanFrames + 1] = {};
    float stepDt_ = 0.0f;
    float tailTime_ = 0.0f;
    float predictedX_ = 0.0f;
    float predictedZ_ = 0.0f;
    uint8_t predictedPath_[kSearchDepth] = {};
    bool searching_ = false;
};

} // namespace tt::twodgame
