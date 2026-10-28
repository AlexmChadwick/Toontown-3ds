#include "world/ceo_battle.h"
#include "core/profile.h"
#include "world/boss_battle.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <malloc.h>

#include "audio/audio.h"
#include "core/log.h"
#include "core/screenshot.h"
#include "net/net_platform.h"

namespace tt::world {

namespace {

constexpr float kDegrees = 0.01745329252f;
constexpr uint32_t kLobby = 10100;
constexpr uint32_t kHq = 10000;

// Upstream ToontownGlobals BossCog attack codes:
constexpr uint8_t kAttackArea = 4;
constexpr uint8_t kAttackDirected = 7;
constexpr uint8_t kAttackSlowDirected = 11;
constexpr uint8_t kAttackMove = 16;
constexpr uint8_t kAttackGolf = 17;
constexpr uint8_t kAttackGolfArea = 18;
constexpr uint8_t kAttackGear = 19;
constexpr uint8_t kAttackOvertime = 20;

float distance(Vec3 first, Vec3 second) {
    const float dx = first.x - second.x;
    const float dy = first.y - second.y;
    const float dz = first.z - second.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

bool member(const uint32_t* ids, int count, uint32_t id) {
    for (int index = 0; index < count; ++index) {
        if (ids[index] == id) {
            return true;
        }
    }
    return false;
}

bool phaseIs(const net::BossState& state, const char* phase) {
    return !std::strcmp(state.phase, phase);
}

C3D_Mtx transform(Vec3 position, float heading = 0.0f, float pitch = 0.0f, float scale = 1.0f) {
    C3D_Mtx matrix;
    Mtx_Identity(&matrix);
    Mtx_Translate(&matrix, position.x, position.y, position.z, true);
    Mtx_RotateZ(&matrix, heading * kDegrees, true);
    Mtx_RotateX(&matrix, pitch * kDegrees, true);
    Mtx_Scale(&matrix, scale, scale, scale);
    return matrix;
}

struct BeltItem {
    Vec3 position;
    uint32_t sequence;
    bool visible;
};

BeltItem beltItem(const CeoBelt& belt, int slot, float seconds, bool toonup) {
    // Upstream DistributedFoodBelt.createOneFoodIval: four staggered nodes, then free fall.
    const float speed = toonup ? 1.0f : 5.0f;
    const float length = distance(belt.start, belt.end);
    const float travel = length / speed;
    const float drop = std::sqrt(2.0f * std::max(0.0f, belt.end.z + 0.1f) / 32.174f);
    const float elapsed = seconds - travel * slot / 4.0f;
    if (elapsed < 0.0f) {
        return {{}, 0, false};
    }

    const float period = travel + drop;
    const uint32_t cycle = static_cast<uint32_t>(elapsed / period);
    const float time = std::fmod(elapsed, period);
    const float fraction = std::min(1.0f, time / travel);
    const float fall = std::max(0.0f, time - travel);

    const Vec3 position{
        belt.start.x + (belt.end.x - belt.start.x) * fraction,
        belt.start.y + (belt.end.y - belt.start.y) * fraction + speed * fall,
        belt.start.z + 0.1f - 16.087f * fall * fall
    };
    return {position, cycle * 4 + static_cast<uint32_t>(slot) + 1, true};
}

} // namespace

void CeoBattle::board(BossFrame& frame, uint32_t nowMs) {
    for (int index = 0; index < net::kMaxCogElevators; ++index) {
        const net::CogElevator& elevator = frame.client.cogElevatorSlot(index);
        if (!elevator.active || elevator.classId != dc::DistributedBBElevator::kClassId ||
            !elevator.destZone || elevator.destZone == frame.client.zoneId()) {
            continue;
        }

        // BossElevatorAI sends this field only to the rider, then immediately clears
        // the seat. Receipt is the authorization; a same-frame empty slot is expected.
        frame.client.sendSetParent(net::kParentRender);
        frame.client.changeZone(elevator.destZone);
        frame.remotes.clear();
        TT_LOG("CEO: targeted elevator destination %lu", static_cast<unsigned long>(elevator.destZone));
        return;
    }

    const uint32_t zone = frame.place.zoneId();
    if (boardZone_ != zone) {
        boardZone_ = zone;
        anchorValid_ = false;
        if (zone == kLobby) {
            FILE* file = std::fopen("sdmc:/3ds/toontown3ds/data/boss/ceo_lobby_anchor.txt", "r");
            unsigned version = 0;
            anchorValid_ = file && std::fscanf(file, "BPA1 %u %f %f %f", &version,
                &anchor_.x, &anchor_.y, &anchor_.z) == 4 && version == 1 &&
                std::isfinite(anchor_.x) && std::isfinite(anchor_.y) && std::isfinite(anchor_.z);
            if (file) {
                std::fclose(file);
            }
            TT_LOG("CEO: cached lobby anchor, valid %d", anchorValid_);
        }
    }

    const bool scripted = frame.test.active() && !std::strcmp(frame.test.label(), "ceo: board");
    if (scripted && zone != kLobby) {
        frame.uiState.teleportRequest = kLobby;
        return;
    }

    if (zone != kLobby || !anchorValid_ || !frame.client.inZone()) {
        return;
    }

    if (scripted) {
        frame.player.reset(anchor_.x, anchor_.y, anchor_.z + 0.1f, 180.0f);
    }

    const Vec3 toon{frame.player.x(), frame.player.y(), frame.player.z()};
    if (distance(toon, anchor_) > 5.0f || nowMs - boardMs_ < 2500) {
        return;
    }

    for (int index = 0; index < net::kMaxCogElevators; ++index) {
        const net::CogElevator& elevator = frame.client.cogElevatorSlot(index);
        if (!elevator.active || elevator.classId != dc::DistributedBBElevator::kClassId) {
            continue;
        }

        if (member(elevator.slots, 8, frame.client.avatarId())) {
            if (frame.input.down & KEY_B) {
                frame.client.sendCogElevatorExit(elevator.doId);
            }
        } else if (scripted || (frame.input.down & KEY_A)) {
            if (frame.client.sendCogElevatorBoard(elevator.doId)) {
                boardMs_ = nowMs;
            }
        }
    }
}

void CeoBattle::begin(BossFrame& frame, const net::BossState& state) {
    active_ = true;
    entered_ = false;
    bossId_ = state.doId;
    phase_[0] = '\0';
    reserveAck_ = false;
    attackRevision_ = 0;
    controlledId_ = 0;
    charging_ = false;
    bossPosition_ = {0.0f, 355.0f, 0.0f};
    bossHeading_ = 0.0f;

    for (bool& value : acknowledged_) {
        value = false;
    }
    for (Shot& shot : shots_) {
        shot = {};
    }
    for (uint32_t& value : waterRevisions_) {
        value = 0;
    }
    for (uint32_t& value : golfRevisions_) {
        value = 0;
    }
    for (auto& belt : usedFood_) {
        for (uint32_t& value : belt) {
            value = 0;
        }
    }
    for (auto& belt : usedToonup_) {
        for (uint32_t& value : belt) {
            value = 0;
        }
    }

    C3D_FrameSync();
    frame.battle.reset();
    frame.ui.setBattle(nullptr, {});
    frame.remotes.clear();
    frame.suits.setZone(frame.client.zoneId());
    frame.place.unload();
    frame.player.setDynamic(nullptr);
    frame.player.reset(-1.40f, 59.78f, 0.0f, 0.0f); // BossbotHQBossBattle.teleportInPosHpr
    frame.ui.book().close();
}

void CeoBattle::leave(BossFrame& frame, bool sad) {
    C3D_FrameSync();
    frame.client.sendBossExit();
    for (uint32_t& id : dinerIds_) {
        if (id) {
            frame.suits.removeExternal(id);
        }
        id = 0;
    }
    assets_.release();
    frame.battle.reset();
    frame.remotes.clear();
    frame.ui.setBattle(nullptr, {});

    uint32_t destination = sad ? 2000 : kHq;
    if (!frame.client.boss() && frame.client.inZone() && Place::available(frame.client.zoneId())) {
        destination = frame.client.zoneId();
    }
    if (!frame.place.load(destination)) {
        destination = 2000;
        frame.place.load(destination);
    }
    if (frame.client.zoneId() != destination) {
        frame.client.changeZone(destination);
    }

    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float heading = 0.0f;
    frame.place.arrival(0, &x, &y, &z, &heading);
    frame.place.finishLoading(x, y, z, nullptr, nullptr);
    frame.player.setCollision(&frame.place.collision());
    frame.player.setDynamic(nullptr);
    frame.player.reset(x, y, z, heading);
    frame.client.sendSetParent(net::kParentRender);
    active_ = false;
    bossId_ = 0;
}

Vec3 CeoBattle::pitcher(int table) const {
    const CeoPose& pose = assets_.layout.tables[table];
    const Vec3 offset = ceoRotate(assets_.layout.pitcher, pose.heading);
    return {
        pose.position.x + offset.x,
        pose.position.y + offset.y,
        pose.position.z + offset.z
    };
}

void CeoBattle::update(BossFrame& frame, const net::BossState& state, uint32_t nowMs) {
    if (std::strcmp(phase_, state.phase)) {
        std::snprintf(phase_, sizeof(phase_), "%s", state.phase);
        for (bool& ack : acknowledged_) {
            ack = false;
        }
        for (Shot& shot : shots_) {
            shot.active = false;
        }
        charging_ = false;
        controlledId_ = 0;

        if (phaseIs(state, "BattleOne")) {
            frame.player.reset(20.0f, -45.0f, 0.0f, 0.0f);
        }
        if (phaseIs(state, "BattleTwo")) {
            frame.player.reset(0.0f, 100.0f, 0.0f, 0.0f);
        }
        if (phaseIs(state, "BattleThree")) {
            frame.player.reset(20.0f, 115.0f, 0.0f, 0.0f);
        }
        if (phaseIs(state, "BattleFour")) {
            frame.player.reset(0.0f, 125.0f, 0.0f, 0.0f);
        }
        TT_LOG("CEO: phase %s", state.phase);
    }

    if (!entered_) {
        entered_ = frame.client.sendBossEnter();
    }

    const float elapsed = (nowMs - state.phaseMs) / 1000.0f;
    for (int index = 0; index < state.numBarriers; ++index) {
        const net::BossBarrier& barrier = state.barriers[index];
        const float delay = ceoBarrierDelay(barrier.name);
        if (barrier.waitingForUs && delay >= 0.0f && elapsed >= delay &&
            !std::strcmp(barrier.name, state.phase) &&
            (!acknowledged_[index] || barriers_[index] != barrier.context) &&
            frame.client.sendBossBarrier(barrier.context)) {
            acknowledged_[index] = true;
            barriers_[index] = barrier.context;
        }
    }

    const bool cogRound = phaseIs(state, "BattleOne") || phaseIs(state, "BattleThree");
    if (cogRound && !frame.battle.active()) {
        const uint32_t id = member(state.sideA, state.numA, frame.client.avatarId())
            ? state.battleA
            : state.battleB;
        if (id && frame.client.findBattle(id)) {
            frame.battle.requestJoin(frame.client, id, frame.player.x(), frame.player.y(),
                frame.player.z(), frame.player.headingDeg(), nowMs);
        }
    }

    if (const net::Battle* battle = frame.client.localBattle()) {
        for (int index = 0; index < battle->numBossBarriers; ++index) {
            const net::BossBarrier& barrier = battle->bossBarriers[index];
            if (barrier.waitingForUs && !std::strcmp(barrier.name, "ReservesJoining") &&
                nowMs - battle->stateMs >= 3500 &&
                (!reserveAck_ || reserveBattle_ != battle->doId || reserveContext_ != barrier.context) &&
                frame.client.sendBossBattleBarrier(battle->doId, barrier.context)) {
                reserveAck_ = true;
                reserveBattle_ = battle->doId;
                reserveContext_ = barrier.context;
            }
        }
    }

    frame.battle.setAutoPlay(frame.test.active());
    frame.battle.update(frame.client, nowMs, frame.dt, {SuitActors::poseThunk, &frame.suits});
    frame.suits.update(frame.dt, nowMs, &assets_.collision);
    for (int index = 0; index < frame.battle.numSuitPoses(); ++index) {
        const BattleActorPose& pose = frame.battle.suitPose(index);
        frame.suits.overridePose(pose.doId, pose.x, pose.y, pose.z + pose.hop, pose.h,
            pose.hidden, pose.flash, pose.anim, pose.clipArg, pose.clipStartMs);
    }

    const net::OwnStats& stats = frame.client.ownStats();
    frame.battle.fillView(frame.client, nowMs, &battleView_);
    frame.ui.setBattle(frame.battle.active() ? &battleView_ : nullptr,
        {stats.inventory, stats.experience, stats.trackAccess});

    if (frame.battle.active()) {
        frame.ui.handleInput(frame.input.down, frame.input.held, frame.touch, frame.uiState);
        BattleAction action;
        while (frame.ui.battle().popAction(action)) {
            frame.battle.perform(frame.client, action, nowMs);
        }
    }

    bool controlling = false;
    for (int index = 0; index < net::kMaxCeoChildren; ++index) {
        const net::CeoChild& child = frame.client.ceoChildSlot(index);
        controlling |= child.active && child.owner == frame.client.avatarId();
    }

    Input movement = frame.input;
    if (controlling || frame.battle.active() || (!phaseIs(state, "BattleTwo") && !phaseIs(state, "BattleFour"))) {
        movement = {};
    }

    frame.player.setCollision(&assets_.collision);
    frame.player.update(movement, frame.dt);

    BattleActorPose pose;
    if (frame.battle.localPose(&pose)) {
        frame.player.reset(pose.x, pose.y, pose.z + pose.hop, pose.h);
        if (pose.anim >= kBattleAnimClip0 && pose.anim < kBattleAnimClip0 + kNumBattleClips) {
            const ToonAnim clip = toonAnimByName(kBattleClipNames[pose.anim - kBattleAnimClip0]);
            frame.toon.playAction(clip, clip == kAnimVictory ? ClipMode::Loop : ClipMode::Once);
        } else {
            frame.toon.play(static_cast<ToonAnim>(pose.anim), 1.0f);
        }
    } else if (!frame.toon.actionActive()) {
        float rate = 1.0f;
        const ToonAnim animation = frame.player.animState(&rate);
        frame.toon.play(animation, rate);
    }

    if (phaseIs(state, "BattleTwo") || phaseIs(state, "BattleFour")) {
        banquet(frame, state, nowMs);
    }
    if (phaseIs(state, "BattleFour")) {
        court(frame, state, nowMs);
    }

    if (nowMs - moveMs_ >= 200) {
        frame.client.sendPosHpr(frame.player.x(), frame.player.y(), frame.player.z(),
            frame.player.headingDeg(), 0.0f, 0.0f);
        frame.client.sendAnimState(frame.player.airborne() ? "jumpAirborne"
            : frame.player.moving() ? "run" : "Happy", 1.0f);
        frame.client.setOwnWorldPos(frame.player.x(), frame.player.y(), frame.player.z());
        moveMs_ = nowMs;
    }

    frame.toon.update(frame.dt);
    frame.remotes.update(frame.dt);
    frame.effects.update(frame.dt, nowMs, &frame.battle, frame.toon.look());
}

void CeoBattle::banquet(BossFrame& frame, const net::BossState& state, uint32_t nowMs) {
    const bool serving = phaseIs(state, "BattleTwo");
    const bool scripted = frame.test.active() && !std::strncmp(frame.test.label(), "ceo: ", 5);
    const bool action = (frame.input.down & KEY_A) || scripted;
    Vec3 toon{frame.player.x(), frame.player.y(), frame.player.z()};

    if (action && nowMs - actionMs_ >= 1000) {
        if (!serving || !state.heldFood) {
            bool requested = false;
            for (int index = 0; index < net::kMaxCeoChildren && !requested; ++index) {
                const net::CeoChild& belt = frame.client.ceoChildSlot(index);
                if (!belt.active || belt.kind != net::CeoKind::Belt || belt.bossId != bossId_ ||
                    belt.state != (serving ? net::kBeltStateOn : net::kBeltStateToonup)) {
                    continue;
                }
                if (!serving && (scripted || frame.client.ownStats().hp >= frame.client.ownStats().maxHp)) {
                    continue;
                }
                for (int slot = 0; slot < 4; ++slot) {
                    const BeltItem item = beltItem(assets_.layout.belts[belt.index], slot,
                        (nowMs - belt.stateMs) / 1000.0f, !serving);
                    uint32_t& used = serving ? usedFood_[belt.index][slot] : usedToonup_[belt.index][slot];
                    if (!item.visible || item.sequence == used) {
                        continue;
                    }
                    if (scripted) {
                        frame.player.reset(item.position.x + 2.0f, item.position.y, 0.0f, 0.0f);
                    }
                    toon = {frame.player.x(), frame.player.y(), frame.player.z()};
                    if (distance(toon, item.position) < 6.0f &&
                        frame.client.sendCeoFood(belt.index, slot, item.sequence, !serving)) {
                        used = item.sequence;
                        actionMs_ = nowMs;
                        requested = true;
                        break;
                    }
                }
            }
        } else {
            bool requested = false;
            for (int index = 0; index < net::kMaxCeoChildren && !requested; ++index) {
                const net::CeoChild& table = frame.client.ceoChildSlot(index);
                if (!table.active || table.kind != net::CeoKind::Table || table.bossId != bossId_) {
                    continue;
                }
                for (int seat = 0; seat < table.numDiners; ++seat) {
                    if (table.diners[seat] != net::kDinerStatusHungry &&
                        table.diners[seat] != net::kDinerStatusAngry) {
                        continue;
                    }
                    const Vec3 position = ceoSeat(assets_.layout, table.index, seat);
                    if (scripted) {
                        frame.player.reset(position.x, position.y - 3.0f, 0.0f, 0.0f);
                    }
                    toon = {frame.player.x(), frame.player.y(), frame.player.z()};
                    if (distance(toon, position) < 10.0f && frame.client.sendCeoServe(table.doId, seat)) {
                        actionMs_ = nowMs;
                        requested = true;
                        break;
                    }
                }
            }
        }
    }

    // All diner states remain networked; only eight nearest seated visuals occupy
    // the shared bounded Cog actor pool. Synthetic visual IDs never send fields.
    uint32_t nearest[8] = {};
    Vec3 positions[8] = {};
    float headings[8] = {};
    float distances[8];
    for (float& value : distances) {
        value = 1e20f;
    }

    if (serving) {
        for (int index = 0; index < net::kMaxCeoChildren; ++index) {
            const net::CeoChild& table = frame.client.ceoChildSlot(index);
            if (!table.active || table.kind != net::CeoKind::Table || table.bossId != bossId_) {
                continue;
            }
            for (int seat = 0; seat < table.numDiners; ++seat) {
                if (table.diners[seat] == net::kDinerStatusInactive ||
                    table.diners[seat] == net::kDinerStatusDead) {
                    continue;
                }
                const Vec3 position = ceoSeat(assets_.layout, table.index, seat);
                const float separation = distance(toon, position);
                for (int slot = 0; slot < 8; ++slot) {
                    if (separation >= distances[slot]) {
                        continue;
                    }
                    for (int move = 7; move > slot; --move) {
                        nearest[move] = nearest[move - 1];
                        positions[move] = positions[move - 1];
                        headings[move] = headings[move - 1];
                        distances[move] = distances[move - 1];
                    }
                    nearest[slot] = 0xf0000000u + table.index * 8 + seat;
                    positions[slot] = position;
                    headings[slot] = assets_.layout.tables[table.index].heading + assets_.layout.chairs[seat].heading;
                    distances[slot] = separation;
                    break;
                }
            }
        }
    }

    for (uint32_t old : dinerIds_) {
        if (old && !member(nearest, 8, old)) {
            frame.suits.removeExternal(old);
        }
    }
    for (int slot = 0; slot < 8; ++slot) {
        dinerIds_[slot] = nearest[slot];
        if (nearest[slot]) {
            frame.suits.setExternal(nearest[slot], "tbc", 0, positions[slot].x, positions[slot].y,
                positions[slot].z, headings[slot], false, true);
        }
    }
}

void CeoBattle::court(BossFrame& frame, const net::BossState& state, uint32_t nowMs) {
    const bool scripted = frame.test.active() && !std::strncmp(frame.test.label(), "ceo: ", 5);
    const bool golfScript = scripted && !std::strcmp(frame.test.label(), "ceo: golf") &&
        (nowMs - state.phaseMs) < 12000;
    const Vec3 toon{frame.player.x(), frame.player.y(), frame.player.z()};

    if (attackRevision_ != state.attackRevision) {
        attackRevision_ = state.attackRevision;
        reached_ = false;
        tableHit_ = false;
        away_ = false;
        attackTarget_ = toon;

        if (state.attack == kAttackMove && state.attackToon < kCeoTables) {
            attackTarget_ = assets_.layout.tables[state.attackToon].position;
            const float targetHeading = std::atan2(-(attackTarget_.x - bossPosition_.x),
                attackTarget_.y - bossPosition_.y) / kDegrees + 180.0f;
            const float delta = std::remainder(targetHeading - bossHeading_, 360.0f);
            turnRemaining_ = std::fabs(delta) / (60.0f - 40.0f * ceoSpeedDamage(state, nowMs) / 90.0f);
            bossHeading_ += delta;
        } else if (state.attack == kAttackDirected || state.attack == kAttackSlowDirected ||
                   state.attack == kAttackGolf || state.attack == kAttackGolfArea ||
                   state.attack == kAttackGear || state.attack == kAttackOvertime) {
            if (state.attack == kAttackOvertime && state.attackToon < 2) {
                attackTarget_ = assets_.layout.belts[state.attackToon].start;
            }
            const int countMax = (state.attack == kAttackGolf || state.attack == kAttackGolfArea) ? 5 : 4;
            for (int count = 0; count < countMax; ++count) {
                for (Shot& shot : shots_) {
                    if (shot.active) {
                        continue;
                    }
                    shot = {};
                    shot.active = true;
                    shot.attack = state.attack;
                    shot.origin = {bossPosition_.x, bossPosition_.y, 10.0f};
                    shot.target = {attackTarget_.x + (count - 2) * 3.0f, attackTarget_.y, attackTarget_.z + 2.0f};
                    shot.previous = shot.origin;
                    shot.duration = std::max(0.5f, distance(shot.origin, shot.target) / 60.0f);
                    shot.age = -2.5f - count * 0.15f;
                    break;
                }
            }
        }
    }

    if (state.attack == kAttackMove && state.attackToon < kCeoTables) {
        const float remaining = distance(bossPosition_, attackTarget_);
        if (turnRemaining_ > 0.0f) {
            turnRemaining_ -= frame.dt;
        } else if (remaining > 0.1f) {
            const float step = std::min(1.0f, ceoRollSpeed(state, nowMs) * frame.dt / remaining);
            bossPosition_.x += (attackTarget_.x - bossPosition_.x) * step;
            bossPosition_.y += (attackTarget_.y - bossPosition_.y) * step;
        }
        if (remaining < 12.0f && !tableHit_) {
            tableHit_ = frame.client.sendCeoTable(state.attackToon, net::Client::CeoTableAction::Hit);
        }
        if (remaining < 0.2f && !reached_) {
            reached_ = frame.client.sendCeoTable(state.attackToon, net::Client::CeoTableAction::Reached);
        }
        if (reached_ && !away_) {
            away_ = frame.client.sendCeoTable(state.attackToon, net::Client::CeoTableAction::Away);
        }
    }

    const net::CeoChild* controlled = nullptr;
    for (int index = 0; index < net::kMaxCeoChildren; ++index) {
        const net::CeoChild& child = frame.client.ceoChildSlot(index);
        if (!child.active || child.bossId != bossId_) {
            continue;
        }
        if (child.owner == frame.client.avatarId() && child.state == net::kTableStateControlled) {
            controlled = &child;
        }
    }

    if (!controlled && nowMs - actionMs_ > 1000) {
        for (int index = 0; index < net::kMaxCeoChildren; ++index) {
            const net::CeoChild& child = frame.client.ceoChildSlot(index);
            if (!child.active || child.bossId != bossId_ || child.owner != 0 ||
                child.kind == net::CeoKind::Belt) {
                continue;
            }
            const bool golf = child.kind == net::CeoKind::Golf;
            if (scripted && golf != golfScript) {
                continue;
            }
            const Vec3 position = golf ? ceoGolfPosition(child.index) : pitcher(child.index);
            if (((frame.input.down & KEY_A) && distance(toon, position) < 12.0f) || scripted) {
                if (frame.client.sendCeoControl(child.doId, true)) {
                    actionMs_ = nowMs;
                }
                break;
            }
        }
    }

    if (controlled) {
        const bool golf = controlled->kind == net::CeoKind::Golf;
        const Vec3 position = golf ? ceoGolfPosition(controlled->index) : pitcher(controlled->index);
        if (controlledId_ != controlled->doId) {
            controlledId_ = controlled->doId;
            heading_ = controlled->heading;
            charge_ = 0.0f;
            charging_ = false;
        }

        frame.player.reset(position.x - 2.0f, position.y, position.z + (golf ? 0.0f : 1.0f),
            golf ? -90.0f : heading_);

        if ((frame.input.down & KEY_X) || (scripted && golf != golfScript)) {
            frame.client.sendCeoControl(controlled->doId, false);
        }

        heading_ = std::remainder(heading_ - frame.input.pad.dx / 156.0f * (golf ? 20.0f : 30.0f) * frame.dt, 360.0f);
        if (scripted) {
            heading_ = std::atan2(-(bossPosition_.x - position.x), bossPosition_.y - position.y) / kDegrees;
        }

        if (nowMs - aimMs_ >= 200) {
            frame.client.sendCeoAim(controlled->doId, sequence_++, heading_);
            aimMs_ = nowMs;
        }

        if (frame.input.held & KEY_B) {
            charging_ = true;
            charge_ += frame.dt;
        }

        const bool release = charging_ && !(frame.input.held & KEY_B);
        if (release || (scripted && nowMs - actionMs_ > (golf ? 3500u : 1200u))) {
            const float period = golf ? 3.0f : 15.0f; // Upstream golf-power-speed / water-power-speed
            const float power = scripted ? 1.0f : 1.0f - std::pow(1.0f - std::fmod(charge_ / period, 1.0f), 0.75f);
            if (golf) {
                frame.client.sendCeoGolf(controlled->doId, static_cast<uint8_t>(power * 100.0f), heading_, sequence_++);
            } else {
                const Vec3 nozzle = ceoRotate(assets_.layout.nozzle, heading_);
                const Vec3 origin{position.x + nozzle.x, position.y + nozzle.y, position.z + nozzle.z};
                const Vec3 offset = ceoRotate({0.0f, 250.0f * std::max(0.001f, power), 0.0f}, heading_);
                const Vec3 end{origin.x + offset.x, origin.y + offset.y, origin.z};
                const float endpoints[] = {origin.x, origin.y, origin.z, end.x, end.y, end.z};

                if (frame.client.sendCeoWater(controlled->doId, endpoints) &&
                    segmentBox(origin, end,
                        {bossPosition_.x - 8.0f, bossPosition_.y - 8.0f, bossPosition_.z},
                        {bossPosition_.x + 8.0f, bossPosition_.y + 8.0f, bossPosition_.z + 28.0f})) {
                    float wall = 0.0f;
                    const bool blocked = assets_.collision.cameraRaycast(origin,
                        {bossPosition_.x, bossPosition_.y, origin.z}, &wall);
                    if (!blocked) {
                        const uint8_t damage = power < 0.75f ? 1 : power < 0.97f ? 2 : 3;
                        frame.client.sendCeoWaterHit(controlled->doId, damage);
                    }
                }
            }
            actionMs_ = nowMs;
            charge_ = 0.0f;
            charging_ = false;
        }
    } else {
        controlledId_ = 0;
    }

    for (int index = 0; index < net::kMaxCeoChildren; ++index) {
        const net::CeoChild& child = frame.client.ceoChildSlot(index);
        if (!child.active || child.kind != net::CeoKind::Golf || child.bossId != bossId_ ||
            !child.golfRevision || golfRevisions_[child.index] == child.golfRevision) {
            continue;
        }
        golfRevisions_[child.index] = child.golfRevision;
        for (Shot& shot : shots_) {
            if (shot.active) {
                continue;
            }
            shot = {};
            shot.active = true;
            shot.golf = true;
            shot.owner = child.owner;
            shot.object = child.doId;
            shot.origin = ceoGolfPosition(child.index);
            shot.previous = shot.origin;
            shot.heading = child.golfAngle;
            shot.power = child.golfPower;
            shot.age = -0.4f; // Contact delay
            break;
        }
    }

    for (Shot& shot : shots_) {
        if (!shot.active) {
            continue;
        }
        shot.age += frame.dt;
        if (shot.age < 0.0f) {
            continue;
        }

        Vec3 current;
        if (shot.golf) {
            current = ceoBallPosition(shot.origin, shot.power, shot.heading, shot.age);
        } else {
            const float fraction = shot.age / shot.duration;
            current = {
                shot.origin.x + (shot.target.x - shot.origin.x) * fraction,
                shot.origin.y + (shot.target.y - shot.origin.y) * fraction,
                shot.origin.z + (shot.target.z - shot.origin.z) * fraction
            };
        }

        if (shot.golf && segmentBox(shot.previous, current,
            {bossPosition_.x - 8.0f, bossPosition_.y - 8.0f, 0.0f},
            {bossPosition_.x + 8.0f, bossPosition_.y + 8.0f, 28.0f})) {
            if (shot.owner == frame.client.avatarId()) {
                frame.client.sendCeoBallHit(shot.object, 1);
            }
            shot.active = false;
        } else if (!shot.golf && segmentBox(shot.previous, current,
            {frame.player.x() - 1.4f, frame.player.y() - 1.4f, frame.player.z()},
            {frame.player.x() + 1.4f, frame.player.y() + 1.4f, frame.player.z() + 5.0f}) &&
            nowMs - zapMs_ > 1000) {
            if (frame.client.sendCeoZap(frame.player.x(), frame.player.y(), frame.player.z(),
                frame.player.headingDeg(), shot.attack)) {
                zapMs_ = nowMs;
                if (controlled) {
                    frame.client.sendCeoControl(controlled->doId, false, true);
                }
            }
            shot.active = false;
        }

        shot.previous = current;
        if (shot.age > 4.0f || current.z < -1.0f) {
            shot.active = false;
        }
    }

    const float attackAge = (nowMs - state.attackMs) / 1000.0f;
    const float separation = distance({frame.player.x(), frame.player.y(), frame.player.z()}, bossPosition_);
    const bool area = state.attack == kAttackArea && attackAge >= 1.21f && attackAge < 1.6f && !frame.player.airborne();
    const bool body = (state.attack == kAttackMove || state.attack == 1 || state.attack == 2 ||
                       state.attack == 3 || state.attack == 5) && separation < 12.0f;

    if ((area || body) && nowMs - zapMs_ > 1000 &&
        frame.client.sendCeoZap(frame.player.x(), frame.player.y(), frame.player.z(),
            frame.player.headingDeg(), state.attack)) {
        zapMs_ = nowMs;
        if (controlled) {
            frame.client.sendCeoControl(controlled->doId, false, true);
        }
    }
}

void CeoBattle::drawBoss(BossFrame& frame, const net::BossState& state) {
    if (phaseIs(state, "Reward") || phaseIs(state, "Epilogue")) {
        return;
    }
    if (phaseIs(state, "Victory") && net::plat::nowMs() - state.phaseMs > 5500) {
        return;
    }

    C3D_Mtx world = transform(bossPosition_, bossHeading_);
    C3D_Mtx torso = world;
    C3D_Mtx head = world;

    for (int part = 0; part < 4; ++part) {
        render::Anim& animation = assets_.animations[part];
        if (!animation.loaded()) {
            continue;
        }

        C3D_Mtx partWorld = (part == 1) ? torso : (part == 2) ? head : world;
        u32 first = 0;
        u32 second = 0;
        float blend = 0.0f;

        if (std::strstr(assets_.currentClip, "neutral") || !std::strcmp(assets_.currentClip, "Ff_speech")) {
            render::sampleClip(animation, assets_.clipTime, &first, &second, &blend);
        } else {
            const float sample = std::min(assets_.clipTime * animation.fps(),
                static_cast<float>(animation.numFrames() - 1));
            first = static_cast<u32>(sample);
            second = std::min(first + 1, animation.numFrames() - 1);
            blend = sample - first;
        }

        frame.renderer.drawMorph(assets_.models[part], animation, first, second, blend, partWorld);

        if (animation.hasAttach() && part < 2) {
            C3D_Mtx attachment;
            animation.attach(first, second, blend, &attachment);
            Mtx_Multiply((part == 0) ? &torso : &head, &partWorld, &attachment);
        }
    }
}

void CeoBattle::draw(BossFrame& frame, const net::BossState& state, uint32_t nowMs) {
    C3D_FVec eye = frame.player.cameraEye();
    C3D_FVec target = frame.player.cameraTarget();

    if (frame.battle.inBattle()) {
        frame.battle.cameraEye(&eye.x, &eye.y, &eye.z);
        frame.battle.cameraTarget(&target.x, &target.y, &target.z);
    } else if (controlledId_) {
        const net::CeoChild* child = frame.client.findCeoChild(controlledId_);
        if (child) {
            const Vec3 position = (child->kind == net::CeoKind::Golf)
                ? ceoGolfPosition(child->index)
                : pitcher(child->index);
            const Vec3 behind = ceoRotate({0.0f, -18.0f, 14.0f}, heading_);
            eye = FVec3_New(position.x + behind.x, position.y + behind.y, position.z + behind.z);
            target = FVec3_New(bossPosition_.x, bossPosition_.y, bossPosition_.z + 12.0f);
        }
    } else if (phaseIs(state, "Introduction") || phaseIs(state, "Victory")) {
        eye = FVec3_New(bossPosition_.x + 30.0f, bossPosition_.y - 55.0f, 25.0f);
        target = FVec3_New(bossPosition_.x, bossPosition_.y, 13.0f);
    }

    gfxSet3D(false);
    frame.stereoActive = false;
    frame.renderer.resetStats();
    frame.renderer.beginWorld(frame.renderer.topLeft(), eye, target, 0.0f, 0.0f);

    if (assets_.ready()) {
        C3D_Mtx identity = transform({0.0f, 0.0f, 0.0f});
        frame.renderer.drawModel(assets_.models[4], identity); // Arena

        for (int index = 0; index < net::kMaxCeoChildren; ++index) {
            const net::CeoChild& child = frame.client.ceoChildSlot(index);
            if (!child.active || child.bossId != bossId_) {
                continue;
            }

            if (child.kind == net::CeoKind::Table && child.state != net::kTableStateOff) {
                const CeoPose& pose = assets_.layout.tables[child.index];
                C3D_Mtx table = transform(pose.position, pose.heading);
                if (child.state == net::kTableStateFlat) {
                    Mtx_Scale(&table, 1.0f, 1.0f, 0.01f);
                }
                frame.renderer.drawModel(assets_.models[5], table);

                if (phaseIs(state, "BattleFour") && child.state != net::kTableStateFlat) {
                    const float bottleHeading = (child.owner == frame.client.avatarId()) ? heading_ : child.heading;
                    C3D_Mtx bottle = transform(pitcher(child.index), bottleHeading);
                    frame.renderer.drawModel(assets_.models[6], bottle);
                }

                if (child.waterRevision && nowMs - child.waterMs < 500) {
                    for (int drop = 0; drop < 4; ++drop) {
                        const float fraction = (drop + 1) / 4.0f;
                        C3D_Mtx water = transform(
                            {
                                child.water[0] + (child.water[3] - child.water[0]) * fraction,
                                child.water[1] + (child.water[4] - child.water[1]) * fraction,
                                child.water[2] + (child.water[5] - child.water[2]) * fraction
                            },
                            0.0f, 0.0f, 0.35f
                        );
                        frame.renderer.drawModel(assets_.models[8], water);
                    }
                }
            } else if (child.kind == net::CeoKind::Belt &&
                       (child.state == net::kBeltStateOn || child.state == net::kBeltStateToonup)) {
                const bool isToonup = (child.state == net::kBeltStateToonup);
                for (int slot = 0; slot < 4; ++slot) {
                    const BeltItem item = beltItem(assets_.layout.belts[child.index], slot,
                        (nowMs - child.stateMs) / 1000.0f, isToonup);
                    const uint32_t used = isToonup
                        ? usedToonup_[child.index][slot]
                        : usedFood_[child.index][slot];
                    if (!item.visible || used == item.sequence) {
                        continue;
                    }
                    C3D_Mtx food = transform(item.position, 0.0f, 0.0f, 0.75f);
                    frame.renderer.drawModel(assets_.models[7], food);
                }
            } else if (child.kind == net::CeoKind::Golf && child.state != net::kGolfStateOff) {
                C3D_Mtx spot = transform(ceoGolfPosition(child.index));
                frame.renderer.drawModel(assets_.models[8], spot);
                frame.renderer.drawModel(assets_.models[9], spot);
            }
        }

        for (const Shot& shot : shots_) {
            if (!shot.active || shot.age < 0.0f) {
                continue;
            }
            C3D_Mtx model = transform(shot.previous, shot.age * 360.0f, 0.0f, shot.golf ? 1.0f : 0.2f);
            const int modelIdx = (shot.golf || shot.attack == kAttackGolf || shot.attack == kAttackGolfArea) ? 8 : 11;
            frame.renderer.drawModel(assets_.models[modelIdx], model);
        }

        if (phaseIs(state, "BattleFour")) {
            const Vec3 hand = ceoRotate({10.0f, 0.0f, 20.0f}, bossHeading_);
            C3D_Mtx club = transform({bossPosition_.x + hand.x, bossPosition_.y + hand.y, hand.z}, bossHeading_);
            frame.renderer.drawModel(assets_.models[10], club);
        }

        if (state.heldFood && phaseIs(state, "BattleTwo")) {
            C3D_Mtx food = transform(
                {frame.player.x(), frame.player.y(), frame.player.z() + 3.0f},
                frame.player.headingDeg(), 0.0f, 0.75f
            );
            frame.renderer.drawModel(assets_.models[7], food);
        }

        drawBoss(frame, state);

        C3D_Mtx toon;
        frame.player.worldMatrix(&toon);
        frame.toon.draw(frame.renderer, toon);
        frame.remotes.draw(frame.renderer, frame.player.x(), frame.player.y());
        frame.suits.draw(frame.renderer);
        frame.effects.draw(frame.renderer, &frame.battle, frame.toon.look());
    }

    frame.renderer.endWorld();
    C2D_Prepare(); // citro2d state must be re-bound after the world shaders
    prof::lap(prof::kBossDraw);
    frame.stats = frame.renderer.stats();

    ui::Context context{};
    context.stats = &frame.client.ownStats();
    context.infoTitle = assets_.failed() ? "CEO assets missing / over budget"
        : !assets_.ready() ? "Loading CEO banquet" : state.phase;

    std::snprintf(status_, sizeof(status_), "CEO %.0f / 500  Speed %.1f / 90  Laff %d",
        net::currentBossDamage(state, nowMs), ceoSpeedDamage(state, nowMs), frame.client.ownStats().hp);

    std::snprintf(details_, sizeof(details_), "%s  Aim %.0f  Charge %.1fs",
        state.heldFood ? "Carrying food" : "", heading_, charge_);

    if (phaseIs(state, "Reward") || phaseIs(state, "Epilogue")) {
        std::snprintf(details_, sizeof(details_), "Pink slips %u%s",
            unsigned(frame.client.bossPinkSlips()), state.promoted ? "  Promotion earned" : "");
    }

    context.infoLines[0] = status_;
    context.infoLines[1] = details_;
    context.infoLines[2] = assets_.failed() ? "Copy data/boss/ceo_*; B: return"
        : phaseIs(state, "Epilogue") ? "A: return to Bossbot HQ"
        : phaseIs(state, "BattleTwo") ? "A near belt: food; A near hungry diner: serve"
        : "A: table/golf/jump  Pad: aim  B: charge/release  X: leave";

    context.heapUsedKB = mallinfo().uordblks / 1024;
    context.linearFreeKB = linearSpaceFree() / 1024;
    context.vramFreeKB = vramSpaceFree() / 1024;
    if (frame.suits.loadingAssets()) context.infoLines[2]="Streaming Cog models and clips...";
    frame.ui.draw(frame.bottom, frame.uiState, context);

    // Damage bar across bottom of touch screen:
    const float damageFraction = std::clamp(net::currentBossDamage(state, nowMs) / 500.0f, 0.0f, 1.0f);
    C2D_DrawRectSolid(12.0f, 196.0f, 0.8f, damageFraction * 296.0f, 5.0f, C2D_Color32(220, 80, 40, 255));
}

bool CeoBattle::frame(BossFrame& frame) {
    const uint32_t nowMs = net::plat::nowMs();

    if (!active_) {
        board(frame, nowMs);
        const net::BossState* state = frame.client.boss();
        if (!state || state->kind != net::BossKind::Bossbot) {
            return false;
        }
        begin(frame, *state);
    }

    frame.client.update(nowMs);
    net::Event event;
    while (frame.client.pollEvent(event)) {
        frame.battle.handleEvent(event, frame.client, nowMs);
        frame.remotes.handleEvent(event, frame.client);
        frame.suits.handleEvent(event, frame.client, nowMs);
    }

    const net::BossState state = frame.client.boss() ? *frame.client.boss() : net::BossState{};
    const bool ending = (frame.input.down & KEY_A) ||
        (frame.test.active() && !std::strcmp(frame.test.label(), "ceo: end"));

    if (!state.active || state.doId != bossId_ || frame.client.ownStats().hp <= 0 ||
        (assets_.failed() && (frame.input.down & KEY_B)) ||
        ((phaseIs(state, "Epilogue") || phaseIs(state, "Frolic")) && ending && nowMs - state.phaseMs > 1500)) {
        leave(frame, frame.client.ownStats().hp <= 0);
        return false;
    }

    if (assets_.ready()) {
        update(frame, state, nowMs);
    } else {
        if (!entered_) {
            entered_ = frame.client.sendBossEnter();
        }
        if (phaseIs(state, "WaitForToons") && nowMs - state.phaseMs >= 500) {
            for (int index = 0; index < state.numBarriers; ++index) {
                const net::BossBarrier& barrier = state.barriers[index];
                if (barrier.waitingForUs && !std::strcmp(barrier.name, "WaitForToons") &&
                    (!acknowledged_[index] || barriers_[index] != barrier.context) &&
                    frame.client.sendBossBarrier(barrier.context)) {
                    acknowledged_[index] = true;
                    barriers_[index] = barrier.context;
                }
            }
        }
    }

    prof::lap(prof::kBossUpdate);
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    prof::lap(prof::kWait);
    frame.cache.pump();
    frame.suits.pumpAssets();

    prof::lap(prof::kLoads);
    if (frame.shot) {
        char path[128];
        std::snprintf(path, sizeof(path), "sdmc:/3ds/toontown3ds/shot_%s_top.bin", frame.shot);
        shot::dumpTarget(frame.renderer.topLeft(), path);
        std::snprintf(path, sizeof(path), "sdmc:/3ds/toontown3ds/shot_%s_bottom.bin", frame.shot);
        shot::dumpTarget(frame.bottom, path);
        frame.shot = nullptr;
        prof::lap(prof::kCapture);
    }

    assets_.pump(ceoClip(state, nowMs));
    assets_.clipTime += frame.dt;
    prof::lap(prof::kLoads);
    draw(frame, state, nowMs);

    frame.commandUsage = C3D_GetCmdBufUsage();
    prof::lap(prof::kBottomUi);
    C3D_FrameEnd(0);
    prof::lap(prof::kFrameEnd);
    audio::update();
    prof::lap(prof::kAudio);
    return true;
}

} // namespace tt::world
