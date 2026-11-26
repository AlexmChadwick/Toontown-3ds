#include "world/boss_logic.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace tt::world {
namespace {
constexpr float kDegrees = 3.14159265359f / 180.0f;
}

BossPhase bossPhase(const char* state) {
    if (!std::strcmp(state, "WaitForToons") || !std::strcmp(state, "Elevator")) return BossPhase::Arrival;
    if (!std::strcmp(state, "Introduction")) return BossPhase::Intro;
    if (!std::strcmp(state, "BattleOne") || !std::strcmp(state, "BattleTwo")) return BossPhase::Round;
    if (!std::strcmp(state, "RollToBattleTwo") || !std::strcmp(state, "PrepareBattleTwo") ||
        !std::strcmp(state, "PrepareBattleThree")) return BossPhase::Transition;
    if (!std::strcmp(state, "BattleThree")) return BossPhase::Pies;
    if (!std::strcmp(state, "NearVictory")) return BossPhase::FinalPie;
    if (!std::strcmp(state, "Victory")) return BossPhase::Victory;
    if (!std::strcmp(state, "Reward")) return BossPhase::Reward;
    if (!std::strcmp(state, "Epilogue") || !std::strcmp(state, "Frolic")) return BossPhase::Epilogue;
    return BossPhase::Off;
}

float bossBarrierDelay(const char* name) {
    if (!std::strcmp(name, "WaitForToons")) return 0.5f;
    if (!std::strcmp(name, "Elevator")) return 6.0f;
    if (!std::strcmp(name, "Introduction")) return kVpIntroSeconds;
    if (!std::strcmp(name, "RollToBattleTwo")) return 8.0f;
    if (!std::strcmp(name, "PrepareBattleTwo")) return 4.0f;
    if (!std::strcmp(name, "PrepareBattleThree")) return 8.0f;
    if (!std::strcmp(name, "Victory")) return 6.0f;
    if (!std::strcmp(name, "Reward")) return 4.0f;
    return -1; // Never acknowledge an unknown barrier or a live combat phase.
}

Vec3 vpPosition(float damage) {
    // DistributedSellbotBoss.__makeBossDamageMovie rolls at constant speed along
    // these two segments. The first is sloped; use its true length for the split.
    constexpr Vec3 top{0, 60, 18};
    constexpr Vec3 bottom{0, -110, -6.5f};
    constexpr Vec3 death{0, -175, -6.5f};
    const float first = std::sqrt(170.0f * 170.0f + 24.5f * 24.5f);
    const float distance = std::clamp(damage / 100.0f, 0.0f, 1.0f) * (first + 65.0f);
    const float fraction = std::min(1.0f, distance / first);
    if (distance <= first) return {0, top.y + fraction * (bottom.y - top.y), top.z + fraction * (bottom.z - top.z)};
    return {0, bottom.y - (distance - first), death.z};
}

float vpCageHeight(const net::BossState& state, uint32_t nowMs) {
    // DistributedSellbotBoss.cageHeights / enterBattleOne / enterBattleTwo /
    // __makeCageOpenMovie. A one-sided Cog round lowers the cage one extra stop.
    const BossPhase phase = bossPhase(state.phase);
    const bool single = !state.battleA || !state.battleB;
    if (phase == BossPhase::Epilogue) {
        const float fraction = std::clamp((nowMs - state.phaseMs) / 500.0f, 0.0f, 1.0f);
        return 25 - 7 * fraction;
    }
    if (phase == BossPhase::Pies || phase == BossPhase::FinalPie || phase == BossPhase::Victory ||
        phase == BossPhase::Reward || !std::strcmp(state.phase, "PrepareBattleThree")) return 25;
    if (!std::strcmp(state.phase, "BattleTwo")) return single ? 44 : 63;
    if (!std::strcmp(state.phase, "PrepareBattleTwo")) return 63;
    if (!std::strcmp(state.phase, "BattleOne")) return single ? 81 : 100;
    return 100;
}

float vpCageDoorAngle(const net::BossState& state, uint32_t nowMs) {
    if (bossPhase(state.phase) != BossPhase::Epilogue) return 0;
    const float fraction = std::clamp((nowMs - state.phaseMs) / 500.0f - 1, 0.0f, 1.0f);
    return 90 * fraction;
}

float bossElevatorBob(float t) {
    if (t < 0 || t >= 8) return 0;
    const float r = std::fmod(t, 4.0f);
    const auto easeOut = [](float u) { return 1 - (1 - u) * (1 - u); };
    if (r >= 0.5f && r < 1.0f) return -0.2f * easeOut((r - 0.5f) * 2);
    if (r >= 1.0f && r < 1.5f) return -0.2f * (1 - (r - 1.0f) * 2);
    if (r >= 2.5f && r < 3.0f) return 0.2f * easeOut((r - 2.5f) * 2);
    if (r >= 3.0f) return 0.2f * (1 - (r - 3.0f));
    return 0;
}

void bossCameraAim(Vec3 position, float h, float p, Vec3* eye, Vec3* target) {
    *eye = position;
    *target = {position.x - std::sin(h * kDegrees) * std::cos(p * kDegrees) * 10,
               position.y + std::cos(h * kDegrees) * std::cos(p * kDegrees) * 10, position.z + std::sin(p * kDegrees) * 10};
}

bool vpDoober(int index, float t, Vec3* position, float* heading, bool* walking) {
    if (index < 0 || index > 7) return false;
    static const float kPoints[4][3] = {{4.5f, -7, 10}, {1.5f, -7.5f, 5}, {-1.5f, -7.5f, -5}, {-4.5f, -7, -10}};
    static const float kDelay[8] = {0, 4, 8, 12, 14, 10, 6, 2}; // doobers[7] walks at 2, [4] at 14
    const bool sideB = index >= 4;
    const float* point = kPoints[index % 4];
    // Battle nodes: (-+25, -35) facing -+90; the promotion spot is the toon point 10 units back.
    const float nodeHeading = sideB ? 90.0f : -90.0f;
    const float c = std::cos(nodeHeading * kDegrees), s = std::sin(nodeHeading * kDegrees);
    const float lx = point[0], ly = point[1] + 10;
    const Vec3 legs[4] = {{(sideB ? 25.0f : -25.0f) + c * lx - s * ly, -35 + s * lx + c * ly, 0},
                          {sideB ? 20.0f : -20.0f, -50, 0}, {0, -50, 0}, {0, -135, -6.5f}};
    const auto headsUp = [](Vec3 from, Vec3 to) { return std::atan2(-(to.x - from.x), to.y - from.y) / kDegrees; };
    *position = legs[0], *heading = nodeHeading + point[2], *walking = false;
    if (t < 9) return true;
    float clock = (t < 16 ? t - 9 : 14 + (t - 16)) - kDelay[index];
    *heading = headsUp(legs[0], legs[1]);
    if (clock < 0) return true;
    for (int leg = 0; leg < 3; ++leg) {
        const Vec3 a = legs[leg], b = legs[leg + 1];
        const float dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
        const float time = std::sqrt(dx * dx + dy * dy + dz * dz) / (4.8f * 1.8f);
        if (clock < time) {
            const float u = clock / time;
            *position = {a.x + dx * u, a.y + dy * u, a.z + dz * u};
            *heading = headsUp(a, b), *walking = true;
            return true;
        }
        clock -= time;
    }
    return false;
}

void vpIntro(float t, bool toonsA, bool toonsB, VpIntroShot* out) {
    struct Cue {
        float at;
        Vec3 position;
        float h, p;
    };
    // dialogTrack's camera.setPosHpr cues; 34 s: loseCogSuits at battleANode / battleBNode
    // (0, 18, 5) h -180, 1 s per side with Toons (world values for SellbotBossBattleOnePosHpr).
    static const Cue kCues[] = {{10, {-23.1f, 15.7f, 17.2f}, -160, -2.4f}, {16, {-25, -99, 10}, -14, 10},
                                {25, {-12, -15, 27}, -151, -15}, {27, {-12, 48, 94}, -26, 20},
                                {31, {-20, -35, 10}, -88, 25}, {37, {-23.4f, -145.6f, 44}, -10, -12.5f}};
    if (t < 10) { // camera.posHprInterval(8, ..., blendType='easeInOut') from (0, 25, 30) h 0
        float u = std::clamp(t / 8, 0.0f, 1.0f);
        u = u * u * (3 - 2 * u);
        bossCameraAim({-22 * u, 25 - 125 * u, 30 + 5 * u}, -10 * u, -13 * u, &out->eye, &out->target);
    } else {
        const Cue* cue = &kCues[0];
        for (const Cue& c : kCues) {
            if (t >= c.at) cue = &c;
        }
        bossCameraAim(cue->position, cue->h, cue->p, &out->eye, &out->target);
        if (t >= 34 && t < 37 && (toonsA || toonsB)) {
            const bool sideB = !toonsA || (toonsB && t >= 35);
            bossCameraAim(sideB ? Vec3{7, -35, 5} : Vec3{-7, -35, 5}, sideB ? -90.0f : 90.0f, 0, &out->eye, &out->target);
        }
    }
    struct Line {
        float from, to;
        const char* text;
    };
    // BossCogPromoteDoobers / DoobersAway['s'] / WelcomeToons / PromoteToons / DiscoverToons / AttackToons.
    static const Line kBoss[] = {
        {5.6f, 10, "You are hereby promoted to full-fledged Sellbots.  Congratulations!"},
        {12, 16, "Go!  And make that sale!"}, {18, 22, "Welcome, new Cogs!"},
        {22, 25, "You are hereby promoted to full-fledged Sellbots.  Congratu--"},
        {31, 34, "Huh?  Toons!  In disguise!"}, {39, 1e9f, "Attack!!"}};
    static const Line kCaged[] = {{22.2f, 25, "Hey! Hiya! Hey over there!"}, {27, 31, "So, did you Toons come to rescue me?"}};
    out->bossChat = out->cagedChat = nullptr;
    out->bossChatAge = out->cagedChatAge = 0;
    for (const Line& line : kBoss) {
        if (t >= line.from && t < line.to) out->bossChat = line.text, out->bossChatAge = t - line.from;
    }
    for (const Line& line : kCaged) {
        if (t >= line.from && t < line.to) out->cagedChat = line.text, out->cagedChatAge = t - line.from;
    }
}

const char* vpClip(const net::BossState& state, uint32_t nowMs) {
    const BossPhase phase = bossPhase(state.phase);
    if (phase == BossPhase::Intro) { // bossAnimTrack, then the dialog track's Ff_lookRt / turn2Fb / Fb_neutral
        const float time = (nowMs - state.phaseMs) / 1000.0f;
        if (time < 10) return "Ff_speech";
        if (time < 12) return "ltTurn2Wave";
        if (time < 16) return "wave";
        if (time < 25) return "Ff_speech"; // ltTurn2Wave played back to front, then Ff_speech
        if (time < 31) return "Ff_lookRt";
        if (time < 37) return "turn2Fb";
        return "Fb_neutral";
    }
    if (phase == BossPhase::Victory) return "Fb_fall";
    if (phase != BossPhase::Pies && phase != BossPhase::FinalPie) return "Ff_neutral";
    if (vpHatchOpen(state, nowMs)) return state.strafeSide ? "hatch_rear" : "hatch_front";
    const float attackTime = (nowMs - state.attackMs) / 1000.0f;
    if (state.attack == net::kBossDizzyNow) return attackTime < 3 ? "Fb_firstHit" : "Fb_downNeutral";
    if (state.attack == net::kBossDizzy) return "Fb_downNeutral";
    if (attackTime < 3) {
        if (state.attack == net::kBossSwatLeft) return "Fb_downLtSwing";
        if (state.attack == net::kBossSwatRight) return "Fb_downRtSwing";
        if (state.attack == net::kBossAreaAttack) return "Fb_jump";
        if (state.attack == net::kBossFrontAttack || state.attack == net::kBossRecoverDizzy) return "Bb2Ff_spin";
        if (state.attack == net::kBossDirectedAttack || state.attack == net::kBossSlowDirectedAttack) return "Fb_UpThrow";
    }
    return "Fb_neutral";
}

float piePower(float heldSeconds) {
    // LocalToon.__getPiePower: oscillating charge with exponent .75 and period .2.
    const float value = std::pow(std::max(0.0f, heldSeconds) / 0.2f, 0.75f);
    const float cycle = std::fmod(value, 2.0f);
    return std::floor((cycle <= 1 ? cycle : 2 - cycle) * 100) / 100;
}

Vec3 piePosition(Vec3 origin, float heading, float pitch, float power, float seconds) {
    // Toon.getTossPieInterval: power 0 travels 100 units in 1 second, power 1
    // travels 30 in 1.5 seconds. Panda ProjectileInterval defaults to 32.174.
    const float charge = std::clamp(power, 0.0f, 1.0f);
    const float duration = 1 + 0.5f * charge;
    const float speed = (100 - 70 * charge) / duration;
    const float vertical = 16.087f * duration;
    const float horizontal = (speed * std::cos(pitch * kDegrees) - vertical * std::sin(pitch * kDegrees)) * seconds;
    return {origin.x - std::sin(heading * kDegrees) * horizontal,
            origin.y + std::cos(heading * kDegrees) * horizontal,
            origin.z + (speed * std::sin(pitch * kDegrees) + vertical * std::cos(pitch * kDegrees)) * seconds - 16.087f * seconds * seconds};
}

bool segmentBox(Vec3 start, Vec3 end, Vec3 minimum, Vec3 maximum) {
    const float first[] = {start.x, start.y, start.z};
    const float last[] = {end.x, end.y, end.z};
    const float low[] = {minimum.x, minimum.y, minimum.z};
    const float high[] = {maximum.x, maximum.y, maximum.z};
    float enter = 0;
    float leave = 1;
    for (int axis = 0; axis < 3; ++axis) {
        const float delta = last[axis] - first[axis];
        if (std::fabs(delta) < 0.00001f) {
            if (first[axis] < low[axis] || first[axis] > high[axis]) return false;
        } else {
            float near = (low[axis] - first[axis]) / delta;
            float far = (high[axis] - first[axis]) / delta;
            if (near > far) std::swap(near, far);
            enter = std::max(enter, near);
            leave = std::min(leave, far);
            if (enter > leave) return false;
        }
    }
    return true;
}

VpPieHit vpPieHit(Vec3 start, Vec3 end, Vec3 boss, bool hatchOpen) {
    start = {start.x - boss.x, start.y - boss.y, start.z - boss.z};
    end = {end.x - boss.x, end.y - boss.y, end.z - boss.z};
    // BossCog's root faces -Y during BattleThree; the lower opening is a narrow
    // target under the torso. Test it first so a body hit cannot hide an inside hit.
    if (hatchOpen && segmentBox(start, end, {-4, -12, 0.5f}, {4, 12, 4.5f})) return VpPieHit::Insides;
    if (segmentBox(start, end, {-8, -11, 4.5f}, {8, 11, 26})) return VpPieHit::Body;
    return VpPieHit::None;
}

bool vpHatchOpen(const net::BossState& state, uint32_t nowMs) {
    const float time = (nowMs - state.strafeMs) / 1000.0f;
    const float duration = 5 - 4 * std::clamp(net::currentBossDamage(state, nowMs) / 100, 0.0f, 1.0f);
    return state.strafeRevision && time >= 0.2f && time < 0.7f + duration;
}

} // namespace tt::world
