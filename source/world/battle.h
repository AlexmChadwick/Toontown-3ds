// Street cog battles, client side: the DistributedBattle state machine (docs/design/combat.md
// sections 3 to 7, "Client implementation status"). It decides what the local toon is doing in a
// fight, sends the clsend fields at the right moments, places and animates the participants
// (placeholder visuals: poses, hops, tint flashes, damage numbers) and answers the bottom-screen
// battle panel (ui/battle_panel.*).
//
// No 3DS headers: this file is built for the host by tools/nettest (against the fake server's
// scripted battle) as well as for the 3DS. Drawing lives elsewhere: main.cpp copies the poses
// into world/suits.* and world/remote_toons.*, moves the local player and reads the camera.
//
// Lifecycle:  Idle -> Requested (requestBattle sent, or toonRequestJoin) -> FaceOff -> Waiting /
//             Input / Chosen / Movie rounds -> Reward -> Idle (setState Resume, or the battle
//             object / our membership going away).
#pragma once

#include <cstdint>

#include "net/client.h"
#include "world/suit_path.h"

namespace tt::world {

// ToonAnim values (world/toon_avatar.h; main.cpp static_asserts they match).
constexpr uint8_t kBattleAnimNeutral = 0;
constexpr uint8_t kBattleAnimWalk = 1;
constexpr uint8_t kBattleAnimRun = 2;
constexpr uint8_t kBattleAnimAttack = 4; // the wave clip stands in for throw/squirt/cringe

// ToontownBattleGlobals track ids and BattleBase special values.
enum BattleTrack : int { kTrackHeal = 0, kTrackTrap, kTrackLure, kTrackSound, kTrackThrow, kTrackSquirt, kTrackDrop };
constexpr int kAttackPass = 98;  // BattleBase.PASS
constexpr int kAttackNone = -1;  // NO_ATTACK

// BattleBase.attackAffectsGroup(track, level) (AvPropTargetCat[AvPropTarget[track]][level]).
bool attackAffectsGroup(int track, int level);

// A participant's pose for this frame.
struct BattleActorPose {
    uint32_t doId;
    float x, y, z, h;   // feet, heading (degrees)
    float hop;          // extra height while attacking (shadow stays on the ground)
    float scale;        // 1 = normal
    float flash;        // 0..1 hit tint
    uint8_t anim;       // kBattleAnim* (toons) or SuitAnim (cogs)
    bool hidden;
};

// Floating text over an actor (damage numbers, misses): world position of the spawn point.
struct BattlePopup {
    float x, y, z;
    float age;          // seconds
    float rise;         // screen pixels per second to float up (0 = stay put)
    char text[24];
    uint32_t color;     // ABGR, as ui::Balloons takes it
};

enum class BattlePhase : uint8_t {
    Idle,       // not fighting
    Requested,  // requestBattle / toonRequestJoin sent, waiting for the AI
    FaceOff,    // cog and toon walk to their slots
    Waiting,    // between rounds, or we are pending (joined after the round started)
    Input,      // WaitForInput and we may choose
    Chosen,     // we chose (or timed out): waiting for the others / the movie
    Movie,      // PlayMovie
    Reward,     // the victory panel
};

struct BattleCogView {
    uint32_t doId;
    char name[28];  // "Flunky"
    int level;      // displayed level
    int hp, maxHp;
    bool active, lured;
};
struct BattleToonView {
    uint32_t doId;
    char name[24];
    bool active, running, local;
};
struct BattleRewardRow {
    int track;
    int earned, orig;
};
// What the bottom-screen panel shows.
struct BattleView {
    BattlePhase phase = BattlePhase::Idle;
    int secondsLeft = -1;          // WaitForInput countdown (20 s), -1 = none
    int numCogs = 0;
    BattleCogView cogs[net::kBattleSuits];
    int numToons = 0;
    BattleToonView toons[net::kBattleToons];
    bool canRun = false;
    const char* status = "";       // one line under the title
    int numRewardRows = 0;
    BattleRewardRow reward[7];
    int cogsDefeated = 0;
    bool rewardSent = false;
};

// What the panel asks for.
struct BattleAction {
    enum Kind : uint8_t { None, Attack, Pass, Run, Continue } kind = None;
    int track = 0, level = 0;
    uint32_t target = 0; // cog doId (toon doId for single target heals); 0 for group attacks
};

// Where a cog is right now (world/suits.*), for cogs that join an existing fight.
struct CogPoseSource {
    bool (*pose)(void* user, uint32_t doId, float* x, float* y, float* z, float* h) = nullptr;
    void* user = nullptr;
};

class Battle {
public:
    static constexpr int kMaxPopups = 12;
    static constexpr int kMaxFx = 72;

    void reset();

    // The local toon touched a walking cog (world/suits.* touching()): sends requestBattle with the
    // cog's pose and starts waiting. toon* is where we stood (the battle walk starts there).
    bool requestBattle(net::Client& client, uint32_t cogDoId, float cogX, float cogY, float cogZ, float cogH,
                       float toonX, float toonY, float toonZ, float toonH, uint32_t nowMs);
    // Walking into another toon's fight (lockout radius 9): toonRequestJoin. Call every frame.
    void checkJoin(net::Client& client, float toonX, float toonY, float toonZ, float toonH, uint32_t nowMs);

    void handleEvent(const net::Event& ev, net::Client& client, uint32_t nowMs);
    void update(net::Client& client, uint32_t nowMs, float dt, const CogPoseSource& cogs);
    // The panel's choice (only honoured in WaitForInput / Reward).
    void perform(net::Client& client, const BattleAction& a, uint32_t nowMs);
    // Test hook (also used by the unattended autotest): pick the first usable gag and cog ourselves.
    void setAutoPlay(bool on) { autoPlay_ = on; }

    // ---- state for main.cpp ----
    bool active() const { return phase_ != BattlePhase::Idle; }      // lock the toon, stop broadcasting
    bool inBattle() const { return phase_ > BattlePhase::Requested; } // positions and camera are ours
    BattlePhase phase() const { return phase_; }
    uint32_t battleId() const { return battleId_; }
    bool localSentFaceOff() const { return faceOffSent_; }
    // One-shot message for a toast ("That cog is busy"), nullptr when none.
    const char* popMessage();
    // True once after a battle ended with our toon at 0 laff (main teleports to the playground).
    bool popWentSad();

    int numSuitPoses() const { return numSuitPoses_; }
    const BattleActorPose& suitPose(int i) const { return suitPoses_[i]; }
    int numToonPoses() const { return numToonPoses_; }
    const BattleActorPose& toonPose(int i) const { return toonPoses_[i]; }
    bool localPose(BattleActorPose* out) const;
    int numPopups() const { return numPopups_; }
    const BattlePopup& popup(int i) const { return popups_[i]; }
    // Fixed orbit camera (BattleCamDefaultPos/Hpr, pulled back a little for our 55 degree FOV).
    void cameraEye(float* x, float* y, float* z) const;
    void cameraTarget(float* x, float* y, float* z) const;

    void fillView(const net::Client& client, uint32_t nowMs, BattleView* out) const;

private:
    struct Actor {
        bool used = false, isToon = false;
        uint32_t doId = 0;
        float x = 0, y = 0, z = 0, h = 0;       // displayed
        float fx = 0, fy = 0, fh = 0;           // move origin
        float tx = 0, ty = 0, th = 0;           // move target
        uint32_t moveStartMs = 0;
        float moveDur = 0;                      // seconds
        bool hasTarget = false;
        bool moving = false;
        float height = 5.0f;                    // nametag / popup height
        bool joinPending = false, joinSent = false;
        uint32_t joinAtMs = 0;
        // Movie effects.
        uint32_t attackStart = 0, attackEnd = 0;
        uint32_t flashEnd = 0;
        uint32_t dieStart = 0;
        bool dying = false, hidden = false;
        // Cogs: damage shown by this battle's movies since the AI last sent setHP (upstream
        // lowers suit.currHP locally; the AI only resends it after the battle).
        int damage = 0;
        int baseHp = -1;
        uint32_t faceUntil = 0;
        float faceH = 0;
    };
    enum FxKind : uint8_t { kFxAttack, kFxHit, kFxMiss, kFxHeal, kFxDie };
    struct Fx {
        uint32_t at;     // ms after the movie started
        uint8_t kind;
        uint32_t who;    // actor doId
        uint32_t other;  // target doId (attacks)
        int16_t value;   // damage / heal / attack length in ms
    };

    // geometry
    void toWorld(float lx, float ly, float* wx, float* wy) const;
    void fromWorld(float wx, float wy, float* lx, float* ly) const;
    void begin(net::Client& client, const net::Battle& nb, const CogPoseSource& cogs, uint32_t nowMs);
    void leave(net::Client& client, bool sad);
    void syncMembers(net::Client& client, const net::Battle& nb, const CogPoseSource& cogs, uint32_t nowMs);
    void retarget(const net::Battle& nb, uint32_t nowMs);
    void goTo(Actor& a, float tx, float ty, float th, float speed, uint32_t startMs, float durOverride);
    void onState(net::Client& client, const net::Battle& nb, uint32_t nowMs);
    void startMovie(net::Client& client, const net::Battle& nb, uint32_t nowMs);
    void buildMovie(const net::BattleMovie& m);
    void runFx(uint32_t nowMs);
    void addFx(uint32_t at, uint8_t kind, uint32_t who, uint32_t other, int value);
    void addPopup(const Actor& a, const char* text, uint32_t color, uint32_t nowMs, uint32_t lifeMs = 1600,
                  float rise = 28.0f);
    void computePoses(uint32_t nowMs);
    void autoPick(net::Client& client, const net::Battle& nb);
    Actor* findActor(uint32_t doId);
    int memberIndex(const uint32_t* list, int n, uint32_t doId) const;
    static int memberIndex_(const Actor* arr, uint32_t doId); // index of doId in a 4-slot actor array, or -1

    BattlePhase phase_ = BattlePhase::Idle;
    uint32_t battleId_ = 0;
    uint32_t doneBattle_ = 0;      // a battle we left; ignore it until its object is gone
    uint32_t requestMs_ = 0;
    uint32_t cooldownUntilMs_ = 0;
    uint32_t joinCooldownMs_ = 0;
    uint32_t nowMs_ = 0;
    float reqX_ = 0, reqY_ = 0, reqZ_ = 0, reqH_ = 0; // where the toon stood when it started
    bool joining_ = false;

    // geometry of the battle node
    float bx_ = 0, by_ = 0, bz_ = 0, bh_ = 0;
    uint32_t localId_ = 0;

    Actor suits_[net::kBattleSuits];
    Actor toons_[net::kBattleToons];

    // what we have already reacted to
    uint32_t seenMembers_ = 0, seenState_ = 0, seenAdjust_ = 0, seenMovie_ = 0;
    uint8_t seenToonsJoining_ = 0, seenSuitsJoining_ = 0;
    net::BattleStateId state_ = net::BattleStateId::None;

    // timers (local ms)
    uint32_t faceOffStartMs_ = 0, faceOffEndMs_ = 0;
    bool faceOffSent_ = false;
    uint32_t inputEndMs_ = 0;
    bool chose_ = false;
    bool timeoutSent_ = false;
    uint32_t adjustDoneAtMs_ = 0;
    bool adjustPending_ = false;
    uint32_t rewardStartMs_ = 0;
    bool rewardSent_ = false;
    bool running_ = false;
    bool autoPlay_ = false;
    uint32_t autoAtMs_ = 0;
    bool wentSad_ = false;
    const char* message_ = nullptr;
    bool tauntShown_ = false;

    // movie
    Fx fx_[kMaxFx];
    int numFx_ = 0, fxCursor_ = 0;
    uint32_t movieStartMs_ = 0, movieEndMs_ = 0;
    bool movieSent_ = false;
    bool moviePlaying_ = false;

    BattlePopup popups_[kMaxPopups];
    uint32_t popupStartMs_[kMaxPopups] = {};
    uint32_t popupLifeMs_[kMaxPopups] = {};
    int numPopups_ = 0;

    BattleActorPose suitPoses_[net::kBattleSuits];
    BattleActorPose toonPoses_[net::kBattleToons];
    int numSuitPoses_ = 0, numToonPoses_ = 0;
};

} // namespace tt::world
