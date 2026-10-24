#include "world/battle_camera.h"

#include <cmath>
#include <cstring>
#include "world/battle.h"

namespace tt::world {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegToRad = kPi / 180.0f;

// Framing distance scaling: 1.05x pull-back for 55 deg vertical FOV on 400x240 screen
constexpr float kFramingScale = 1.05f;

inline float clamp(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

void transformRelative(float ox, float oy, float oz, float oh,
                       float rx, float ry, float rz, float rh, float rp, float rr,
                       float* wx, float* wy, float* wz,
                       float* wh, float* wp, float* wr) {
    const float rad = oh * kDegToRad;
    const float cosR = std::cos(rad);
    const float sinR = std::sin(rad);
    *wx = ox + (rx * cosR - ry * sinR);
    *wy = oy + (rx * sinR + ry * cosR);
    *wz = oz + rz;
    *wh = oh + rh;
    *wp = rp;
    *wr = rr;
}

void hprToTarget(float wx, float wy, float wz, float wh, float wp, float dist,
                 float* tx, float* ty, float* tz) {
    const float hrad = wh * kDegToRad;
    const float prad = wp * kDegToRad;
    const float cosP = std::cos(prad);
    const float sinP = std::sin(prad);
    const float fx = -std::sin(hrad) * cosP;
    const float fy =  std::cos(hrad) * cosP;
    const float fz =  sinP;
    *tx = wx + fx * dist;
    *ty = wy + fy * dist;
    *tz = wz + fz * dist;
}

// Suit attack open shot durations from upstream MovieCamera.py
float getSuitAttackOpenDurationSec(int attackId) {
    switch (attackId) {
    case 1:  return 2.8f; // BITE
    case 3:  return 2.4f; // BRAIN_STORM
    case 4:  return 4.7f; // BUZZ_WORD
    case 6:  return 2.9f; // CANNED
    case 7:  return 2.8f; // CHOMP
    case 9:  return 3.3f; // CLIPON_TIE
    case 10: return 3.4f; // CRUNCH
    case 11: return 1.7f; // DEMOTION
    case 12: return 3.9f; // DOUBLE_TALK
    case 14: return 3.2f; // EVICTION_NOTICE
    case 15: return 2.7f; // EVIL_EYE
    case 16: return 2.7f; // FILIBUSTER
    case 17: return 3.2f; // FILL_WITH_LEAD
    case 18: return 2.3f; // FINGER_WAG
    case 19: return 1.7f; // FIRED
    case 22: return 2.6f; // FOUNTAIN_PEN
    case 23: return 2.5f; // FREEZE_ASSETS
    case 25: return 2.8f; // HALF_WINDSOR
    case 26: return 1.3f; // HEAD_SHRINK
    case 27: return 1.4f; // GLOWER_POWER
    case 28: return 0.9f; // GUILT_TRIP
    case 29: return 5.1f; // HANG_UP
    case 30: return 2.5f; // HOT_AIR
    case 32: return 1.5f; // LEGALESE
    case 33: return 2.5f; // LIQUIDATE
    case 34: return 2.9f; // MARKET_CRASH
    case 35: return 2.8f; // MUMBO_JUMBO
    case 36: return 1.6f; // PARADIGM_SHIFT
    case 37: return 2.8f; // PECKING_ORDER
    case 38: return 2.3f; // PLAY_HARDBALL
    case 39: return 2.7f; // PICK_POCKET
    case 40: return 2.8f; // PINK_SLIP
    case 41: return 2.8f; // POUND_KEY
    case 42: return 2.4f; // POWER_TIE
    case 43: return 1.1f; // POWER_TRIP
    case 45: return 2.2f; // RAZZLE_DAZZLE
    case 46: return 3.5f; // RED_TAPE
    case 47: return 1.1f; // RE_ORG
    case 48: return 2.8f; // RESTRAINING_ORDER
    case 50: return 3.2f; // RUBBER_STAMP
    case 51: return 2.2f; // RUB_OUT
    case 52: return 2.9f; // SACKED
    case 53: return 2.8f; // SCHMOOZE
    case 55: return 4.1f; // SHRED
    case 56: return 1.7f; // SPIN
    case 57: return 1.7f; // SYNERGY
    case 59: return 4.5f; // TEE_OFF
    case 63: return 1.2f; // WITHDRAWAL
    default: return 3.5f; // Default upstream open shot duration
    }
}

} // namespace

BattleCamera::BattleCamera() {
    reset();
}

void BattleCamera::reset() {
    numShots_ = 0;
    currentMode_ = BattleCamShotKind::WaitForInput;
    modeStartMs_ = 0;
    modeDurationMs_ = 0;
    faceOffSuitId_ = 0;
    faceOffWalkMs_ = 0;
    rewardLocalToonId_ = 0;
}

void BattleCamera::addShot(const BattleCameraShot& s) {
    if (numShots_ < kMaxShots) {
        shots_[numShots_++] = s;
    }
}

void BattleCamera::startFaceOff(uint32_t suitId, uint32_t startMs, uint32_t faceoffWalkMs) {
    reset();
    currentMode_ = BattleCamShotKind::FaceOffTaunt;
    modeStartMs_ = startMs;
    faceOffSuitId_ = suitId;
    faceOffWalkMs_ = faceoffWalkMs;
}

void BattleCamera::startWaitForInput() {
    reset();
    currentMode_ = BattleCamShotKind::WaitForInput;
}

void BattleCamera::startReward(uint32_t localToonId, uint32_t startMs, uint32_t durationMs) {
    reset();
    currentMode_ = BattleCamShotKind::Reward;
    modeStartMs_ = startMs;
    modeDurationMs_ = durationMs;
    rewardLocalToonId_ = localToonId;
    chooseRewardShot(localToonId, startMs, durationMs);
}

// ----------------------------------------------------------------------
// Low-level shot definitions
// ----------------------------------------------------------------------
BattleCameraShot BattleCamera::makeAllGroupShot(uint32_t startMs, uint32_t durMs) {
    BattleCameraShot s;
    s.kind = BattleCamShotKind::HeldBattle;
    s.startMs = startMs;
    s.durationMs = durMs;
    s.x = 10.0f * kFramingScale;
    s.y = 0.0f;
    s.z = 10.0f;
    s.h = 89.0f;
    s.p = -30.0f;
    s.r = 0.0f;
    return s;
}

BattleCameraShot BattleCamera::makeAllGroupLowShot(uint32_t startMs, uint32_t durMs) {
    BattleCameraShot s;
    s.kind = BattleCamShotKind::HeldBattle;
    s.startMs = startMs;
    s.durationMs = durMs;
    s.x = 15.0f * kFramingScale;
    s.y = 0.0f;
    s.z = 3.0f;
    s.h = 89.0f;
    s.p = 0.0f;
    s.r = 0.0f;
    return s;
}

BattleCameraShot BattleCamera::makeAllGroupLowDiagonalShot(uint32_t startMs, uint32_t durMs) {
    BattleCameraShot s;
    s.kind = BattleCamShotKind::HeldBattle;
    s.startMs = startMs;
    s.durationMs = durMs;
    s.x = 7.0f * kFramingScale;
    s.y = 5.0f;
    s.z = 6.0f;
    s.h = 119.0f;
    s.p = -30.0f;
    s.r = 0.0f;
    return s;
}

BattleCameraShot BattleCamera::makeToonGroupShot(uint32_t startMs, uint32_t durMs) {
    BattleCameraShot s;
    s.kind = BattleCamShotKind::HeldBattle;
    s.startMs = startMs;
    s.durationMs = durMs;
    s.x = 10.0f * kFramingScale;
    s.y = 0.0f;
    s.z = 10.0f;
    s.h = 115.0f;
    s.p = -30.0f;
    s.r = 0.0f;
    return s;
}

BattleCameraShot BattleCamera::makeToonGroupHighShot(uint32_t startMs, uint32_t durMs) {
    BattleCameraShot s;
    s.kind = BattleCamShotKind::HeldBattle;
    s.startMs = startMs;
    s.durationMs = durMs;
    s.x = 5.0f * kFramingScale;
    s.y = 0.0f;
    s.z = 1.0f;
    s.h = 115.0f;
    s.p = 45.0f;
    s.r = 0.0f;
    return s;
}

BattleCameraShot BattleCamera::makeSuitGroupShot(uint32_t startMs, uint32_t durMs) {
    BattleCameraShot s;
    s.kind = BattleCamShotKind::HeldBattle;
    s.startMs = startMs;
    s.durationMs = durMs;
    s.x = 10.0f * kFramingScale;
    s.y = 0.0f;
    s.z = 10.0f;
    s.h = 65.0f;
    s.p = -30.0f;
    s.r = 0.0f;
    return s;
}

BattleCameraShot BattleCamera::makeSuitGroupLowLeftShot(uint32_t startMs, uint32_t durMs) {
    BattleCameraShot s;
    s.kind = BattleCamShotKind::HeldBattle;
    s.startMs = startMs;
    s.durationMs = durMs;
    s.x = 8.4f * kFramingScale;
    s.y = -3.85f;
    s.z = 2.75f;
    s.h = 36.3f;
    s.p = 3.25f;
    s.r = 0.0f;
    return s;
}

BattleCameraShot BattleCamera::makeSuitGroupThreeQuarterLeftBehindShot(uint32_t startMs, uint32_t durMs) {
    BattleCameraShot s;
    s.kind = BattleCamShotKind::HeldBattle;
    s.startMs = startMs;
    s.durationMs = durMs;
    if (rng_.rand01() > 0.5f) {
        s.x = 12.37f * kFramingScale;
        s.h = 134.61f;
    } else {
        s.x = -12.37f * kFramingScale;
        s.h = -134.61f;
    }
    s.y = 11.5f;
    s.z = 8.16f;
    s.p = -22.7f;
    s.r = 0.0f;
    return s;
}

BattleCameraShot BattleCamera::makeAvatarCloseUpShot(uint32_t actorId, uint32_t startMs, uint32_t durMs) {
    BattleCameraShot s;
    s.kind = BattleCamShotKind::HeldAvatar;
    s.targetActorId = actorId;
    s.startMs = startMs;
    s.durationMs = durMs;
    s.x = 0.0f;
    s.y = 8.0f * kFramingScale;
    s.z = 3.0f; // scaled by actor height in sample()
    s.h = 179.0f;
    s.p = 15.0f;
    s.r = 0.0f;
    return s;
}

BattleCameraShot BattleCamera::makeAvatarCloseUpThrowShot(uint32_t actorId, uint32_t startMs, uint32_t durMs) {
    BattleCameraShot s;
    s.kind = BattleCamShotKind::HeldAvatar;
    s.targetActorId = actorId;
    s.startMs = startMs;
    s.durationMs = durMs;
    s.x = 3.0f;
    s.y = 8.0f * kFramingScale;
    s.z = 3.0f;
    s.h = 159.0f;
    s.p = 3.6f;
    s.r = 0.0f;
    return s;
}

BattleCameraShot BattleCamera::makeAvatarCloseUpThreeQuarterRightShot(uint32_t actorId, uint32_t startMs, uint32_t durMs) {
    BattleCameraShot s;
    s.kind = BattleCamShotKind::HeldAvatar;
    s.targetActorId = actorId;
    s.startMs = startMs;
    s.durationMs = durMs;
    s.x = 5.2f;
    s.y = 5.45f * kFramingScale;
    s.z = 3.0f;
    s.h = 131.5f;
    s.p = 3.6f;
    s.r = 0.0f;
    return s;
}

BattleCameraShot BattleCamera::makeAvatarCloseUpThreeQuarterRightShotWide(uint32_t actorId, uint32_t startMs, uint32_t durMs) {
    BattleCameraShot s;
    s.kind = BattleCamShotKind::HeldAvatar;
    s.targetActorId = actorId;
    s.startMs = startMs;
    s.durationMs = durMs;
    s.x = 7.2f;
    s.y = 8.45f * kFramingScale;
    s.z = 3.0f;
    s.h = 131.5f;
    s.p = 3.6f;
    s.r = 0.0f;
    return s;
}

BattleCameraShot BattleCamera::makeAvatarCloseUpThreeQuarterLeftShot(uint32_t actorId, uint32_t startMs, uint32_t durMs) {
    BattleCameraShot s;
    s.kind = BattleCamShotKind::HeldAvatar;
    s.targetActorId = actorId;
    s.startMs = startMs;
    s.durationMs = durMs;
    s.x = -5.2f;
    s.y = 5.45f * kFramingScale;
    s.z = 3.0f;
    s.h = -131.5f;
    s.p = 3.6f;
    s.r = 0.0f;
    return s;
}

BattleCameraShot BattleCamera::makeAvatarBehindShot(uint32_t actorId, uint32_t startMs, uint32_t durMs) {
    BattleCameraShot s;
    s.kind = BattleCamShotKind::HeldAvatar;
    s.targetActorId = actorId;
    s.startMs = startMs;
    s.durationMs = durMs;
    s.x = 5.0f;
    s.y = -7.0f * kFramingScale;
    s.z = 4.5f;
    s.h = 40.0f;
    s.p = -12.0f;
    s.r = 0.0f;
    return s;
}

BattleCameraShot BattleCamera::makeAvatarBehindHighShot(uint32_t actorId, uint32_t startMs, uint32_t durMs) {
    BattleCameraShot s;
    s.kind = BattleCamShotKind::HeldAvatar;
    s.targetActorId = actorId;
    s.startMs = startMs;
    s.durationMs = durMs;
    s.x = -4.0f;
    s.y = -7.0f * kFramingScale;
    s.z = 9.5f;
    s.h = -30.0f;
    s.p = -35.0f;
    s.r = 0.0f;
    return s;
}

BattleCameraShot BattleCamera::makeAvatarBehindHighRightShot(uint32_t actorId, uint32_t startMs, uint32_t durMs) {
    BattleCameraShot s;
    s.kind = BattleCamShotKind::HeldAvatar;
    s.targetActorId = actorId;
    s.startMs = startMs;
    s.durationMs = durMs;
    s.x = 4.0f;
    s.y = -7.0f * kFramingScale;
    s.z = 9.5f;
    s.h = 30.0f;
    s.p = -35.0f;
    s.r = 0.0f;
    return s;
}

BattleCameraShot BattleCamera::makeAvatarBehindThreeQuarterRightShot(uint32_t actorId, uint32_t startMs, uint32_t durMs) {
    BattleCameraShot s;
    s.kind = BattleCamShotKind::HeldAvatar;
    s.targetActorId = actorId;
    s.startMs = startMs;
    s.durationMs = durMs;
    s.x = 7.67f;
    s.y = -8.52f * kFramingScale;
    s.z = 3.0f;
    s.h = 25.0f;
    s.p = 7.5f;
    s.r = 0.0f;
    return s;
}

BattleCameraShot BattleCamera::makeRandomActorShot(uint32_t actorId, bool isSuit, bool isGroup,
                                                  uint32_t startMs, uint32_t durMs) {
    BattleCameraShot s;
    s.kind = BattleCamShotKind::FocusPoint;
    s.targetActorId = actorId;
    s.startMs = startMs;
    s.durationMs = durMs;
    if (isSuit) {
        s.x = (4.0f + rng_.rand01() * 8.0f) * kFramingScale;
        s.y = isGroup ? -4.0f : (-2.0f - rng_.rand01() * 4.0f);
        s.z = isGroup ? 3.0f : (3.5f + rng_.rand01() * 5.0f);
    } else {
        s.x = (2.0f + rng_.rand01() * 8.0f) * kFramingScale;
        s.y = isGroup ? 1.0f : (-2.0f + rng_.rand01() * 3.0f);
        s.z = isGroup ? 2.5f : (4.0f + rng_.rand01() * 5.0f);
    }
    if (rng_.rand01() > 0.5f) s.x = -s.x;
    s.targetX = 0.0f;
    s.targetY = 0.0f;
    s.targetZ = 3.5f; // relative to actor
    return s;
}

BattleCameraShot BattleCamera::makeRandomOverShoulderShot(uint32_t suitId, uint32_t toonId, bool focusOnToon,
                                                         uint32_t startMs, uint32_t durMs) {
    BattleCameraShot s;
    s.kind = BattleCamShotKind::FocusPoint;
    s.targetActorId = focusOnToon ? toonId : suitId;
    s.startMs = startMs;
    s.durationMs = durMs;
    s.x = (2.0f + rng_.rand01() * 10.0f) * kFramingScale;
    if (focusOnToon) {
        s.y = 8.0f + rng_.rand01() * 6.0f;
        s.z = 7.0f + rng_.rand01() * 4.0f;
    } else {
        s.y = -10.0f - rng_.rand01() * 6.0f;
        s.z = 5.0f;
    }
    if (rng_.rand01() > 0.5f) s.x = -s.x;
    s.targetX = 0.0f;
    s.targetY = 0.0f;
    s.targetZ = 3.5f;
    return s;
}

BattleCameraShot BattleCamera::makeRandomSplitShot(uint32_t suitId, uint32_t toonId,
                                                  uint32_t startMs, uint32_t durMs) {
    (void)toonId;
    BattleCameraShot s;
    s.kind = BattleCamShotKind::FocusPoint;
    s.targetActorId = suitId;
    s.startMs = startMs;
    s.durationMs = durMs;
    s.x = (9.0f + rng_.rand01() * 2.0f) * kFramingScale;
    s.y = -2.0f - rng_.rand01() * 2.0f;
    s.z = 4.0f + rng_.rand01() * 4.0f;
    if (rng_.rand01() > 0.5f) s.x = -s.x;
    s.targetX = 0.0f;
    s.targetY = 0.0f;
    s.targetZ = 3.0f;
    return s;
}

BattleCameraShot BattleCamera::makeSuitCameraShakeShot(uint32_t suitId, float intensity, bool isQuake,
                                                      uint32_t startMs, uint32_t durMs) {
    BattleCameraShot s;
    s.kind = BattleCamShotKind::Shake;
    s.targetActorId = suitId;
    s.startMs = startMs;
    s.durationMs = durMs;
    s.x = (10.0f + rng_.rand01() * 3.0f) * kFramingScale;
    if (rng_.rand01() > 0.5f) s.x = -s.x;
    s.y = -5.0f;
    s.z = 7.0f + rng_.rand01() * 3.0f;
    s.shakeIntensity = intensity;
    s.isQuake = isQuake;
    return s;
}

// ----------------------------------------------------------------------
// Per-attack shot choice generators
// ----------------------------------------------------------------------
void BattleCamera::chooseHealShot(uint32_t toonId, uint32_t startMs, uint32_t durMs, bool isUber) {
    (void)toonId;
    const uint32_t openDur = isUber ? 5000 : 2800;
    const uint32_t safeOpen = openDur < durMs ? openDur : durMs;
    const uint32_t closeDur = durMs > safeOpen ? (durMs - safeOpen) : 0;

    addShot(makeToonGroupShot(startMs, safeOpen));
    if (closeDur > 0) {
        addShot(isUber ? makeAllGroupLowShot(startMs + safeOpen, closeDur)
                       : makeToonGroupShot(startMs + safeOpen, closeDur));
    }
}

void BattleCamera::chooseTrapShot(uint32_t toonId, uint32_t startMs, uint32_t durMs) {
    (void)toonId;
    constexpr uint32_t openDur = 3000;
    const uint32_t safeOpen = openDur < durMs ? openDur : durMs;
    const uint32_t closeDur = durMs > safeOpen ? (durMs - safeOpen) : 0;

    addShot(makeAllGroupLowShot(startMs, safeOpen));
    if (closeDur > 0) {
        addShot(makeAllGroupLowShot(startMs + safeOpen, closeDur));
    }
}

void BattleCamera::chooseLureShot(uint32_t toonId, uint32_t startMs, uint32_t durMs, bool hasTrainTrap) {
    constexpr uint32_t openDur = 3000;
    const uint32_t safeOpen = openDur < durMs ? openDur : durMs;
    const uint32_t closeDur = durMs > safeOpen ? (durMs - safeOpen) : 0;

    addShot(makeAllGroupLowShot(startMs, safeOpen));
    if (closeDur > 0) {
        if (hasTrainTrap) {
            addShot(makeAvatarBehindHighRightShot(toonId, startMs + safeOpen, closeDur));
        } else {
            addShot(makeAllGroupLowShot(startMs + safeOpen, closeDur));
        }
    }
}

void BattleCamera::chooseSoundShot(uint32_t toonId, uint32_t targetCogId, uint32_t startMs, uint32_t durMs,
                                   int numSounds, int numTargets, bool isUber) {
    const uint32_t openDur = isUber ? 5000 : 3100;
    const uint32_t safeOpen = openDur < durMs ? openDur : durMs;
    const uint32_t closeDur = durMs > safeOpen ? (durMs - safeOpen) : 0;

    if (numSounds == 1) {
        const int r = rng_.choice(3);
        if (r == 0) {
            addShot(isUber ? makeAvatarCloseUpThreeQuarterRightShotWide(toonId, startMs, safeOpen)
                           : makeAvatarCloseUpThreeQuarterRightShot(toonId, startMs, safeOpen));
        } else if (r == 1) {
            addShot(makeAllGroupLowShot(startMs, safeOpen));
        } else {
            addShot(makeSuitGroupThreeQuarterLeftBehindShot(startMs, safeOpen));
        }
    } else {
        if (rng_.choice(2) == 0) {
            addShot(makeAllGroupLowShot(startMs, safeOpen));
        } else {
            addShot(makeSuitGroupThreeQuarterLeftBehindShot(startMs, safeOpen));
        }
    }

    if (closeDur > 0) {
        if (numTargets == 1) {
            const int r = rng_.choice(4);
            if (r == 0) addShot(makeAvatarCloseUpThrowShot(targetCogId, startMs + safeOpen, closeDur));
            else if (r == 1) addShot(makeAvatarCloseUpThreeQuarterLeftShot(targetCogId, startMs + safeOpen, closeDur));
            else if (r == 2) addShot(makeAllGroupLowShot(startMs + safeOpen, closeDur));
            else addShot(makeSuitGroupThreeQuarterLeftBehindShot(startMs + safeOpen, closeDur));
        } else {
            if (rng_.choice(2) == 0) addShot(makeAllGroupLowShot(startMs + safeOpen, closeDur));
            else addShot(makeSuitGroupThreeQuarterLeftBehindShot(startMs + safeOpen, closeDur));
        }
    }
}

void BattleCamera::chooseThrowShot(uint32_t toonId, uint32_t targetCogId, uint32_t startMs, uint32_t durMs,
                                   int numThrows, int numTargets) {
    constexpr uint32_t openDur = 3000;
    const uint32_t safeOpen = openDur < durMs ? openDur : durMs;
    const uint32_t closeDur = durMs > safeOpen ? (durMs - safeOpen) : 0;

    if (numThrows == 1) {
        const int r = rng_.choice(5);
        if (r == 0) addShot(makeAvatarCloseUpThrowShot(toonId, startMs, safeOpen));
        else if (r == 1) addShot(makeAvatarCloseUpThreeQuarterRightShot(toonId, startMs, safeOpen));
        else if (r == 2) addShot(makeAvatarBehindShot(toonId, startMs, safeOpen));
        else if (r == 3) addShot(makeAllGroupLowShot(startMs, safeOpen));
        else addShot(makeSuitGroupThreeQuarterLeftBehindShot(startMs, safeOpen));
    } else {
        if (rng_.choice(2) == 0) addShot(makeAllGroupLowShot(startMs, safeOpen));
        else addShot(makeSuitGroupThreeQuarterLeftBehindShot(startMs, safeOpen));
    }

    if (closeDur > 0) {
        if (numTargets == 1) {
            const int r = rng_.choice(4);
            if (r == 0) addShot(makeAvatarCloseUpThrowShot(targetCogId, startMs + safeOpen, closeDur));
            else if (r == 1) addShot(makeAvatarCloseUpThreeQuarterLeftShot(targetCogId, startMs + safeOpen, closeDur));
            else if (r == 2) addShot(makeAllGroupLowShot(startMs + safeOpen, closeDur));
            else addShot(makeSuitGroupThreeQuarterLeftBehindShot(startMs + safeOpen, closeDur));
        } else {
            if (rng_.choice(2) == 0) addShot(makeAllGroupLowShot(startMs + safeOpen, closeDur));
            else addShot(makeSuitGroupThreeQuarterLeftBehindShot(startMs + safeOpen, closeDur));
        }
    }
}

void BattleCamera::chooseSquirtShot(uint32_t toonId, uint32_t targetCogId, uint32_t startMs, uint32_t durMs,
                                    int numSquirts, int numTargets) {
    chooseThrowShot(toonId, targetCogId, startMs, durMs, numSquirts, numTargets);
}

void BattleCamera::chooseDropShot(uint32_t toonId, uint32_t targetCogId, uint32_t startMs, uint32_t durMs,
                                  int numDrops, int numTargets) {
    chooseThrowShot(toonId, targetCogId, startMs, durMs, numDrops, numTargets);
}

void BattleCamera::chooseSuitShot(int attackId, uint32_t suitId, uint32_t targetToonId, uint32_t startMs,
                                  uint32_t durMs, bool isGroup) {
    // Special camera attack effects
    if (attackId == 44) { // QUAKE
        addShot(makeSuitCameraShakeShot(suitId, 5.15f, true, startMs, durMs));
        return;
    }
    if (attackId == 54) { // SHAKE
        addShot(makeSuitCameraShakeShot(suitId, 1.75f, false, startMs, durMs));
        return;
    }
    if (attackId == 61) { // TREMOR
        addShot(makeSuitCameraShakeShot(suitId, 0.25f, false, startMs, durMs));
        return;
    }
    if (attackId == 39) { // PICK_POCKET
        addShot(makeAllGroupLowShot(startMs, durMs));
        return;
    }

    const uint32_t openDur = uint32_t(getSuitAttackOpenDurationSec(attackId) * 1000.0f);
    const uint32_t safeOpen = openDur < durMs ? openDur : durMs;
    const uint32_t closeDur = durMs > safeOpen ? (durMs - safeOpen) : 0;

    if (isGroup) {
        addShot(makeRandomActorShot(suitId, true, true, startMs, safeOpen));
        if (closeDur > 0) {
            addShot(makeAllGroupLowShot(startMs + safeOpen, closeDur));
        }
    } else {
        const float r1 = rng_.rand01();
        if (r1 > 0.6f) {
            addShot(makeRandomActorShot(suitId, true, false, startMs, safeOpen));
        } else if (r1 > 0.2f) {
            addShot(makeRandomOverShoulderShot(suitId, targetToonId, false, startMs, safeOpen));
        } else {
            addShot(makeRandomSplitShot(suitId, targetToonId, startMs, safeOpen));
        }

        if (closeDur > 0) {
            const float r2 = rng_.rand01();
            if (r2 > 0.6f) {
                addShot(makeRandomActorShot(targetToonId, false, false, startMs + safeOpen, closeDur));
            } else if (r2 > 0.2f) {
                addShot(makeRandomOverShoulderShot(suitId, targetToonId, true, startMs + safeOpen, closeDur));
            } else {
                addShot(makeRandomSplitShot(suitId, targetToonId, startMs + safeOpen, closeDur));
            }
        }
    }
}

void BattleCamera::chooseSOSShot(uint32_t toonId, uint32_t startMs, uint32_t durMs) {
    const int r = rng_.choice(4);
    if (r == 0) addShot(makeAvatarCloseUpThreeQuarterRightShot(toonId, startMs, durMs));
    else if (r == 1) addShot(makeAvatarBehindShot(toonId, startMs, durMs));
    else if (r == 2) addShot(makeAvatarBehindHighShot(toonId, startMs, durMs));
    else addShot(makeSuitGroupThreeQuarterLeftBehindShot(startMs, durMs));
}

void BattleCamera::chooseRewardShot(uint32_t localToonId, uint32_t startMs, uint32_t durMs) {
    const int r = rng_.choice(3);
    if (r == 0) addShot(makeAvatarCloseUpThreeQuarterRightShot(localToonId, startMs, durMs));
    else if (r == 1) addShot(makeAvatarCloseUpShot(localToonId, startMs, durMs));
    else addShot(makeToonGroupShot(startMs, durMs));
}

// ----------------------------------------------------------------------
// Build camera timeline for an entire movie
// ----------------------------------------------------------------------
void BattleCamera::buildMovie(uint32_t battleId, uint32_t round, const Battle& battle,
                              const net::BattleMovie& m, uint32_t movieStartMs) {
    (void)battle;
    reset();
    currentMode_ = BattleCamShotKind::WaitForInput; // movie shots will override
    rng_.seed(battleId ^ (round * 0x9E3779B9u) ^ 0x55AA3311u);

    uint32_t t = movieStartMs + 300;

    // Sort toon attacks by track then level, exactly matching Battle::buildMovie
    int order[4];
    int n = 0;
    for (int i = 0; i < 4; ++i) {
        const net::BattleToonAttack& a = m.toonAttacks[i];
        if (a.track < 0 || a.track > 6 || a.toonIdx < 0 || a.toonIdx >= m.numToons) continue;
        int pos = n++;
        while (pos > 0) {
            const net::BattleToonAttack& p = m.toonAttacks[order[pos - 1]];
            if (p.track < a.track || (p.track == a.track && p.level <= a.level)) break;
            order[pos] = order[pos - 1];
            --pos;
        }
        order[pos] = i;
    }

    for (int k = 0; k < n; ++k) {
        const net::BattleToonAttack& a = m.toonAttacks[order[k]];
        const uint32_t who = m.toons[a.toonIdx];
        uint32_t target = 0;
        if (a.target >= 0) {
            if (a.track == 0 && a.target < m.numToons) target = m.toons[a.target];
            else if (a.track != 0 && a.target < m.numSuits) target = m.suits[a.target];
        }

        uint32_t attackDur = 2200;
        if (a.track == 4) attackDur = 3500; // Throw
        else if (a.track == 5) attackDur = 3300; // Squirt
        else if (a.track == 6) attackDur = 4200; // Drop
        else if (a.track == 3) attackDur = 3500; // Sound
        else if (a.track == 1) attackDur = 3500; // Trap
        else if (a.track == 2) attackDur = 4500; // Lure
        else if (a.track == 0) attackDur = 3200; // Heal

        const bool isGroup = (a.track == 3) || (a.track == 0 && a.level >= 1) || (a.level == 6);

        switch (a.track) {
        case 0: // Heal
            chooseHealShot(who, t, attackDur, a.level == 6);
            break;
        case 1: // Trap
            chooseTrapShot(who, t, attackDur);
            break;
        case 2: // Lure
            chooseLureShot(who, t, attackDur, false);
            break;
        case 3: // Sound
            chooseSoundShot(who, target, t, attackDur, 1, m.numSuits, a.level == 6);
            break;
        case 4: // Throw
            chooseThrowShot(who, target, t, attackDur, 1, 1);
            break;
        case 5: // Squirt
            chooseSquirtShot(who, target, t, attackDur, 1, 1);
            break;
        case 6: // Drop
            chooseDropShot(who, target, t, attackDur, 1, isGroup ? m.numSuits : 1);
            break;
        default:
            addShot(makeAllGroupLowShot(t, attackDur));
            break;
        }

        uint32_t dur = attackDur;
        for (int j = 0; j < m.numSuits; ++j) {
            if ((a.died >> j) & 1) {
                dur = attackDur + 1500; // extra time for defeat animation
            }
        }
        t += dur;
    }

    // Suit attacks
    for (int i = 0; i < 4; ++i) {
        const net::BattleSuitAttack& s = m.suitAttacks[i];
        if (s.suitIdx < 0 || s.suitIdx >= m.numSuits || s.attack < 0) continue;
        const uint32_t who = m.suits[s.suitIdx];
        const uint32_t target = (s.target >= 0 && s.target < m.numToons) ? m.toons[s.target] : 0;
        constexpr uint32_t suitAttackDur = 2600;
        chooseSuitShot(s.attack, who, target, t, suitAttackDur, s.target < 0);
        t += suitAttackDur;
    }
}

// ----------------------------------------------------------------------
// Camera sampling for current frame
// ----------------------------------------------------------------------
void BattleCamera::sample(uint32_t nowMs, const Battle& battle,
                          float* eyeX, float* eyeY, float* eyeZ,
                          float* tgtX, float* tgtY, float* tgtZ) const {
    // Default battle center
    float bx = 0.0f, by = 0.0f, bz = 0.0f, bh = 0.0f;
    battle.actorPos(battle.battleId(), &bx, &by, &bz, &bh);

    // 1. FaceOff modes
    if (currentMode_ == BattleCamShotKind::FaceOffTaunt) {
        const uint32_t elapsed = nowMs - modeStartMs_;
        float sx = 0.0f, sy = 0.0f, sz = 0.0f, sh = 0.0f;
        battle.actorPos(faceOffSuitId_, &sx, &sy, &sz, &sh);
        const float sHeight = battle.actorHeight(faceOffSuitId_) > 0.0f ? battle.actorHeight(faceOffSuitId_) : 5.0f;

        if (elapsed < 1000) {
            // Taunt shot framing the suit
            const float tauntY = 16.0f * kFramingScale;
            const float tauntX = 5.0f;
            const float tauntZ = clamp(sHeight * 0.66f, sHeight - 1.8f, 10.0f);

            float wx, wy, wz, wh, wp, wr;
            transformRelative(sx, sy, sz, sh, tauntX, tauntY, tauntZ, 180.0f, -5.0f, 0.0f,
                              &wx, &wy, &wz, &wh, &wp, &wr);
            *eyeX = wx; *eyeY = wy; *eyeZ = wz;
            *tgtX = sx; *tgtY = sy; *tgtZ = sz + sHeight * 0.75f;
            return;
        } else {
            // Walking Cog follow camera (BattleCamFaceOffPos = (0, -10, 4))
            const float rad = bh * kDegToRad;
            *eyeX = bx + (0.0f * std::cos(rad) - (-10.0f) * std::sin(rad));
            *eyeY = by + (0.0f * std::sin(rad) + (-10.0f) * std::cos(rad));
            *eyeZ = bz + 4.0f;
            *tgtX = sx; *tgtY = sy; *tgtZ = sz + sHeight * 0.5f;
            return;
        }
    }

    // 2. Movie shots timeline
    if (numShots_ > 0) {
        const BattleCameraShot* active = nullptr;
        for (int i = 0; i < numShots_; ++i) {
            if (nowMs >= shots_[i].startMs && nowMs < shots_[i].startMs + shots_[i].durationMs) {
                active = &shots_[i];
                break;
            }
        }
        if (!active && nowMs >= shots_[0].startMs) {
            active = &shots_[numShots_ - 1]; // Hold the last shot if movie is still finishing
        }

        if (active) {
            switch (active->kind) {
            case BattleCamShotKind::HeldBattle: {
                const float rad = bh * kDegToRad;
                *eyeX = bx + (active->x * std::cos(rad) - active->y * std::sin(rad));
                *eyeY = by + (active->x * std::sin(rad) + active->y * std::cos(rad));
                *eyeZ = bz + active->z;
                hprToTarget(*eyeX, *eyeY, *eyeZ, bh + active->h, active->p, 15.0f, tgtX, tgtY, tgtZ);
                return;
            }
            case BattleCamShotKind::HeldAvatar: {
                float ax = 0.0f, ay = 0.0f, az = 0.0f, ah = 0.0f;
                if (battle.actorPos(active->targetActorId, &ax, &ay, &az, &ah)) {
                    const float aHeight = battle.actorHeight(active->targetActorId) > 0.0f
                                        ? battle.actorHeight(active->targetActorId) : 4.5f;
                    float wx, wy, wz, wh, wp, wr;
                    const float relZ = active->z > 2.0f ? (aHeight * 0.66f) : active->z;
                    transformRelative(ax, ay, az, ah, active->x, active->y, relZ,
                                      active->h, active->p, active->r,
                                      &wx, &wy, &wz, &wh, &wp, &wr);
                    *eyeX = wx; *eyeY = wy; *eyeZ = wz;
                    hprToTarget(wx, wy, wz, wh, wp, 15.0f, tgtX, tgtY, tgtZ);
                    return;
                }
                break;
            }
            case BattleCamShotKind::FocusPoint: {
                const float rad = bh * kDegToRad;
                *eyeX = bx + (active->x * std::cos(rad) - active->y * std::sin(rad));
                *eyeY = by + (active->x * std::sin(rad) + active->y * std::cos(rad));
                *eyeZ = bz + active->z;

                float ax = 0.0f, ay = 0.0f, az = 0.0f, ah = 0.0f;
                if (active->targetActorId != 0 && battle.actorPos(active->targetActorId, &ax, &ay, &az, &ah)) {
                    *tgtX = ax + active->targetX;
                    *tgtY = ay + active->targetY;
                    *tgtZ = az + active->targetZ;
                } else {
                    *tgtX = bx + (active->targetX * std::cos(rad) - active->targetY * std::sin(rad));
                    *tgtY = by + (active->targetX * std::sin(rad) + active->targetY * std::cos(rad));
                    *tgtZ = bz + active->targetZ;
                }
                return;
            }
            case BattleCamShotKind::Shake: {
                const float rad = bh * kDegToRad;
                const float t = float(nowMs - active->startMs) * 0.001f;
                float shakeZ = active->shakeIntensity * 0.4f * std::sin(t * 30.0f);
                float shakeY = active->isQuake ? (active->shakeIntensity * 0.2f * std::cos(t * 24.0f)) : 0.0f;

                *eyeX = bx + (active->x * std::cos(rad) - (active->y + shakeY) * std::sin(rad));
                *eyeY = by + (active->x * std::sin(rad) + (active->y + shakeY) * std::cos(rad));
                *eyeZ = bz + active->z + shakeZ;
                *tgtX = bx; *tgtY = by; *tgtZ = bz + 1.5f;
                return;
            }
            default:
                break;
            }
        }
    }

    // 3. Default WaitForInput camera (BattleCamDefaultPos = (0, -8.6, 16.5), BattleCamDefaultHpr = (0, -61, 0))
    // Pulled back slightly (0, -9.5, 17.0) to frame 4 toons and 4 Cogs on 55 deg vertical FOV
    const float rad = bh * kDegToRad;
    constexpr float defX = 0.0f;
    constexpr float defY = -9.5f;
    constexpr float defZ = 17.0f;
    constexpr float defP = -61.0f;

    *eyeX = bx + (defX * std::cos(rad) - defY * std::sin(rad));
    *eyeY = by + (defX * std::sin(rad) + defY * std::cos(rad));
    *eyeZ = bz + defZ;
    hprToTarget(*eyeX, *eyeY, *eyeZ, bh, defP, 16.0f, tgtX, tgtY, tgtZ);
}

} // namespace tt::world
