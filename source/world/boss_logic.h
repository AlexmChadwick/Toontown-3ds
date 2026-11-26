// Pure VP phase and collision rules. Sources: DistributedSellbotBoss.py, BossCog.py,
// DistributedSellbotBossAI.py and ToontownGlobals.py in ref/ref/open-toontown.
#pragma once

#include "net/boss_state.h"
#include "world/collision.h"

namespace tt::world {

enum class BossPhase { Off, Arrival, Intro, Round, Transition, Pies, FinalPie, Victory, Reward, Epilogue };
BossPhase bossPhase(const char* state);
float bossBarrierDelay(const char* name);
Vec3 vpPosition(float damage);
float vpCageHeight(const net::BossState& state, uint32_t nowMs);
float vpCageDoorAngle(const net::BossState& state, uint32_t nowMs);
const char* vpClip(const net::BossState& state, uint32_t nowMs);
// ElevatorUtils.getRideElevatorInterval (boss elevators), played twice: camera z offset at t s.
float bossElevatorBob(float t);
// Panda camera pos / hpr (degrees) -> eye and a look-at point 10 units ahead.
void bossCameraAim(Vec3 position, float h, float p, Vec3* eye, Vec3* target);
// DistributedSellbotBoss.makeIntroductionMovie at t seconds: camera cue and who says what.
struct VpIntroShot {
    Vec3 eye, target;
    const char* bossChat;  // nullptr when silent
    const char* cagedChat; // the caged Toon
    float bossChatAge, cagedChatAge;
};
constexpr float kVpIntroSeconds = 40; // the dialog track's last cue (37) + backupToonsToBattlePosition (3)
// toonsA / toonsB: whether each battle side has Toons (loseCogSuits skips an empty side's shot).
void vpIntro(float t, bool toonsA, bool toonsB, VpIntroShot* out);
// The introduction's eight doobers (setDooberIds; 0-3 at battle A, 4-7 at battle B):
// __doobersToPromotionPosition, then __walkDoober's walk past the turn points down the ramp at
// SuitWalkSpeed * 1.8 (dooberTrack 0..9 from 9 s, from 14 at 16 s). False once at the fly-away
// point (flyAway leaves through the floor behind the camera).
bool vpDoober(int index, float t, Vec3* position, float* heading, bool* walking);
float piePower(float heldSeconds);
Vec3 piePosition(Vec3 origin, float heading, float pitch, float power, float seconds);
bool segmentBox(Vec3 start, Vec3 end, Vec3 minimum, Vec3 maximum);
// Hatch and body are in the VP's root space (heading 180 during pies).
enum class VpPieHit { None, Body, Insides };
VpPieHit vpPieHit(Vec3 start, Vec3 end, Vec3 boss, bool hatchOpen);
bool vpHatchOpen(const net::BossState& state, uint32_t nowMs);

} // namespace tt::world
