#pragma once

#include <cstdint>

namespace tt::net {
struct BattleMovie;
}

namespace tt::world {

class Battle;

// Types of camera shots ported from MovieCamera.py
enum class BattleCamShotKind : uint8_t {
    HeldBattle,      // Held camera at fixed position/hpr relative to battle node
    HeldAvatar,      // Held camera relative to an avatar (toon or suit)
    FocusPoint,      // Camera at (x, y, z) in battle looking at a target point
    Shake,           // Quake/tremor/shake camera with ground shaking
    FaceOffTaunt,    // FaceOff taunt camera framing the suit
    FaceOffWalk,     // FaceOff camera tracking the suit walking in
    WaitForInput,    // Default overhead battle camera
    Reward           // Victory dance / reward camera
};

struct BattleCameraShot {
    BattleCamShotKind kind = BattleCamShotKind::WaitForInput;
    uint32_t startMs = 0;
    uint32_t durationMs = 0;
    uint32_t targetActorId = 0; // avatar doId for HeldAvatar, FocusPoint target, or FaceOff
    float x = 0.0f, y = 0.0f, z = 0.0f;
    float h = 0.0f, p = 0.0f, r = 0.0f;
    float targetX = 0.0f, targetY = 0.0f, targetZ = 0.0f; // for FocusPoint in battle coordinates
    float shakeIntensity = 0.0f;
    bool isQuake = false;
};

// Deterministic PRNG for movie camera shots so every client produces the identical cinematics
class BattleCamRng {
public:
    explicit BattleCamRng(uint32_t seed = 12345) : state_(seed ? seed : 12345) {}
    void seed(uint32_t s) { state_ = s ? s : 12345; }
    uint32_t next() {
        state_ = state_ * 1664525u + 1013904223u;
        return state_;
    }
    float rand01() {
        return float(next() >> 8) * (1.0f / 16777216.0f);
    }
    int choice(int n) {
        if (n <= 1) return 0;
        return int(next() % uint32_t(n));
    }
private:
    uint32_t state_;
};

class BattleCamera {
public:
    static constexpr int kMaxShots = 32;

    BattleCamera();
    void reset();

    // Mode transitions
    void startFaceOff(uint32_t suitId, uint32_t startMs, uint32_t faceoffWalkMs);
    void startWaitForInput();
    void startReward(uint32_t localToonId, uint32_t startMs, uint32_t durationMs);

    // Build timeline of shots for an entire battle movie
    void buildMovie(uint32_t battleId, uint32_t round, const Battle& battle,
                    const net::BattleMovie& m, uint32_t movieStartMs);

    // Per-attack shot choice generators (MovieCamera.py ports)
    void chooseHealShot(uint32_t toonId, uint32_t startMs, uint32_t durMs, bool isUber);
    void chooseTrapShot(uint32_t toonId, uint32_t startMs, uint32_t durMs);
    void chooseLureShot(uint32_t toonId, uint32_t startMs, uint32_t durMs, bool hasTrainTrap);
    void chooseSoundShot(uint32_t toonId, uint32_t targetCogId, uint32_t startMs, uint32_t durMs,
                         int numSounds, int numTargets, bool isUber);
    void chooseThrowShot(uint32_t toonId, uint32_t targetCogId, uint32_t startMs, uint32_t durMs,
                         int numThrows, int numTargets);
    void chooseSquirtShot(uint32_t toonId, uint32_t targetCogId, uint32_t startMs, uint32_t durMs,
                          int numSquirts, int numTargets);
    void chooseDropShot(uint32_t toonId, uint32_t targetCogId, uint32_t startMs, uint32_t durMs,
                        int numDrops, int numTargets);
    void chooseSuitShot(int attackId, uint32_t suitId, uint32_t targetToonId, uint32_t startMs,
                        uint32_t durMs, bool isGroup);
    void chooseSOSShot(uint32_t toonId, uint32_t startMs, uint32_t durMs);
    void chooseRewardShot(uint32_t localToonId, uint32_t startMs, uint32_t durMs);

    // Query active eye and target positions at nowMs (fed to Battle::cameraEye/cameraTarget)
    void sample(uint32_t nowMs, const Battle& battle,
                float* eyeX, float* eyeY, float* eyeZ,
                float* tgtX, float* tgtY, float* tgtZ) const;

    // Shot count and inspection (for host tests)
    int numShots() const { return numShots_; }
    const BattleCameraShot& shot(int idx) const { return shots_[idx]; }
    BattleCamShotKind currentMode() const { return currentMode_; }
    BattleCamRng& rng() { return rng_; }

private:
    void addShot(const BattleCameraShot& s);

    // Shot helpers matching MovieCamera.py
    BattleCameraShot makeAllGroupShot(uint32_t startMs, uint32_t durMs);
    BattleCameraShot makeAllGroupLowShot(uint32_t startMs, uint32_t durMs);
    BattleCameraShot makeAllGroupLowDiagonalShot(uint32_t startMs, uint32_t durMs);
    BattleCameraShot makeToonGroupShot(uint32_t startMs, uint32_t durMs);
    BattleCameraShot makeToonGroupHighShot(uint32_t startMs, uint32_t durMs);
    BattleCameraShot makeSuitGroupShot(uint32_t startMs, uint32_t durMs);
    BattleCameraShot makeSuitGroupLowLeftShot(uint32_t startMs, uint32_t durMs);
    BattleCameraShot makeSuitGroupThreeQuarterLeftBehindShot(uint32_t startMs, uint32_t durMs);

    BattleCameraShot makeAvatarCloseUpShot(uint32_t actorId, uint32_t startMs, uint32_t durMs);
    BattleCameraShot makeAvatarCloseUpThrowShot(uint32_t actorId, uint32_t startMs, uint32_t durMs);
    BattleCameraShot makeAvatarCloseUpThreeQuarterRightShot(uint32_t actorId, uint32_t startMs, uint32_t durMs);
    BattleCameraShot makeAvatarCloseUpThreeQuarterRightShotWide(uint32_t actorId, uint32_t startMs, uint32_t durMs);
    BattleCameraShot makeAvatarCloseUpThreeQuarterLeftShot(uint32_t actorId, uint32_t startMs, uint32_t durMs);
    BattleCameraShot makeAvatarBehindShot(uint32_t actorId, uint32_t startMs, uint32_t durMs);
    BattleCameraShot makeAvatarBehindHighShot(uint32_t actorId, uint32_t startMs, uint32_t durMs);
    BattleCameraShot makeAvatarBehindHighRightShot(uint32_t actorId, uint32_t startMs, uint32_t durMs);
    BattleCameraShot makeAvatarBehindThreeQuarterRightShot(uint32_t actorId, uint32_t startMs, uint32_t durMs);

    BattleCameraShot makeRandomActorShot(uint32_t actorId, bool isSuit, bool isGroup,
                                         uint32_t startMs, uint32_t durMs);
    BattleCameraShot makeRandomOverShoulderShot(uint32_t suitId, uint32_t toonId, bool focusOnToon,
                                               uint32_t startMs, uint32_t durMs);
    BattleCameraShot makeRandomSplitShot(uint32_t suitId, uint32_t toonId,
                                         uint32_t startMs, uint32_t durMs);
    BattleCameraShot makeSuitCameraShakeShot(uint32_t suitId, float intensity, bool isQuake,
                                            uint32_t startMs, uint32_t durMs);

    BattleCameraShot shots_[kMaxShots];
    int numShots_ = 0;
    BattleCamShotKind currentMode_ = BattleCamShotKind::WaitForInput;
    uint32_t modeStartMs_ = 0;
    uint32_t modeDurationMs_ = 0;
    uint32_t faceOffSuitId_ = 0;
    uint32_t faceOffWalkMs_ = 0;
    uint32_t rewardLocalToonId_ = 0;

    BattleCamRng rng_;
};

} // namespace tt::world
