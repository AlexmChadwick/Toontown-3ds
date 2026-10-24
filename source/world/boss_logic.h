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
const char* vpClip(const net::BossState& state, uint32_t nowMs);
float piePower(float heldSeconds);
Vec3 piePosition(Vec3 origin, float heading, float pitch, float power, float seconds);
bool segmentBox(Vec3 start, Vec3 end, Vec3 minimum, Vec3 maximum);
// Hatch and body are in the VP's root space (heading 180 during pies).
enum class VpPieHit { None, Body, Insides };
VpPieHit vpPieHit(Vec3 start, Vec3 end, Vec3 boss, bool hatchOpen);
bool vpHatchOpen(const net::BossState& state, uint32_t nowMs);

} // namespace tt::world
