#include "world/cj_battle.h"
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
constexpr float kDegrees = .01745329252f;
constexpr const char* kDirectory = "sdmc:/3ds/toontown3ds/data/boss/";
constexpr uint32_t kLobby = 13100;
constexpr uint32_t kHq = 13000;
float distance(Vec3 first, Vec3 second) {
    const float dx = first.x - second.x;
    const float dy = first.y - second.y;
    const float dz = first.z - second.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}
bool member(const uint32_t* ids, int count, uint32_t id) {
    for (int index = 0; index < count; ++index) {
        if (ids[index] == id) return true;
    }
    return false;
}
bool phaseIs(const net::BossState& state, const char* phase) {
    return !std::strcmp(state.phase, phase);
}
C3D_Mtx transform(Vec3 position, float heading = 0, float pitch = 0, float scale = 1) {
    C3D_Mtx matrix;
    Mtx_Identity(&matrix);
    Mtx_Translate(&matrix, position.x, position.y, position.z, true);
    Mtx_RotateZ(&matrix, heading * kDegrees, true);
    Mtx_RotateX(&matrix, pitch * kDegrees, true);
    Mtx_Scale(&matrix, scale, scale, scale);
    return matrix;
}
C3D_Mtx scaleRoot() {
    C3D_Mtx matrix = transform(kCjScalePosition, 90);
    Mtx_Scale(&matrix, 1.75f, 1.75f, 1.5f);
    return matrix;
}
}

void CjBattle::board(BossFrame& frame, uint32_t nowMs) {
    for (int index = 0; index < net::kMaxCogElevators; ++index) {
        const net::CogElevator& elevator = frame.client.cogElevatorSlot(index);
        if (!elevator.active || elevator.classId != dc::DistributedCJElevator::kClassId ||
            !elevator.destZone || elevator.destZone == frame.client.zoneId()) continue;
        // BossElevatorAI sends this field only to the rider, then immediately clears
        // the seat. Receipt is the authorization; a same-frame empty slot is expected.
        frame.client.sendSetParent(net::kParentRender);
        frame.client.changeZone(elevator.destZone);
        frame.remotes.clear();
        TT_LOG("CJ: targeted elevator destination %lu", static_cast<unsigned long>(elevator.destZone));
        return;
    }
    const uint32_t zone = frame.place.zoneId();
    if (boardZone_ != zone) {
        boardZone_ = zone;
        anchorValid_ = false;
        if (zone == kLobby) {
            FILE* file = std::fopen("sdmc:/3ds/toontown3ds/data/boss/cj_lobby_anchor.txt", "r");
            unsigned version = 0;
            anchorValid_ = file && std::fscanf(file, "BPA1 %u %f %f %f", &version,
                &anchor_.x, &anchor_.y, &anchor_.z) == 4 && version == 1 &&
                std::isfinite(anchor_.x) && std::isfinite(anchor_.y) && std::isfinite(anchor_.z);
            if (file) std::fclose(file);
            TT_LOG("CJ: cached lobby anchor, valid %d", anchorValid_);
        }
    }
    const bool scripted = frame.test.active() && !std::strcmp(frame.test.label(), "cj: board");
    if (scripted && zone != kLobby) {
        frame.uiState.teleportRequest = kLobby;
        return;
    }
    if (zone != kLobby || !anchorValid_ || !frame.client.inZone()) return;
    if (scripted) frame.player.reset(anchor_.x, anchor_.y, anchor_.z + .1f, 180);
    const Vec3 toon{frame.player.x(), frame.player.y(), frame.player.z()};
    if (distance(toon, anchor_) > 5 || nowMs - boardMs_ < 2500) return;
    for (int index = 0; index < net::kMaxCogElevators; ++index) {
        const net::CogElevator& elevator = frame.client.cogElevatorSlot(index);
        if (!elevator.active || elevator.classId != dc::DistributedCJElevator::kClassId) continue;
        if (member(elevator.slots, 8, frame.client.avatarId())) {
            if (frame.input.down & KEY_B) frame.client.sendCogElevatorExit(elevator.doId);
        } else if (scripted || (frame.input.down & KEY_A)) {
            if (frame.client.sendCogElevatorBoard(elevator.doId)) boardMs_ = nowMs;
        }
    }
}

void CjBattle::begin(BossFrame& frame, const net::BossState& state) {
    active_ = true;
    entered_ = false;
    defeated_ = false;
    bossId_ = state.doId;
    phase_[0] = 0;
    charging_ = false;
    bossAttack_ = 0;
    bossAttackHit_ = false;
    autoChair_ = 0;
    reserveAck_ = false;
    for (uint32_t& revision : cannonFire_) revision = 0;
    for (uint32_t& revision : lawyerAttack_) revision = 0;
    for (uint32_t& stamp : lawyerHit_) stamp = 0;
    for (uint32_t& stamp : gavelHit_) stamp = 0;
    for (JurorFlight& flight : flights_) flight = {};
    for (Evidence& evidence : evidence_) evidence = {};
    for (bool& ack : acknowledged_) ack = false;
    C3D_FrameSync();
    frame.battle.reset();
    frame.ui.setBattle(nullptr, {});
    frame.remotes.clear();
    frame.suits.setZone(frame.client.zoneId());
    frame.place.unload();
    frame.player.setDynamic(nullptr);
    frame.player.reset(-2.84f, -99.47f, 0, 0); // LawbotHQBossBattle teleportInPosHpr.
    frame.ui.book().close();
}

void CjBattle::leave(BossFrame& frame, bool sad) {
    C3D_FrameSync();
    frame.client.sendBossExit();
    // Network deletion can clear a child before this frame observes the exit.
    // Keep the published actor IDs independently so no ghost survives departure.
    for (uint32_t& id : externalIds_) {
        if (id) frame.suits.removeExternal(id);
        id = 0;
    }
    for (int index = 0; index < 9; ++index) {
        npcs_[index].setLook(nullptr);
        frame.cache.release(&looks_[index]);
    }
    npcsLoaded_ = false;
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
    if (frame.client.zoneId() != destination) frame.client.changeZone(destination);
    float x = 0;
    float y = 0;
    float z = 0;
    float heading = 0;
    frame.place.arrival(0, &x, &y, &z, &heading);
    frame.place.finishLoading(x, y, z, nullptr, nullptr);
    frame.player.setCollision(&frame.place.collision());
    frame.player.setDynamic(nullptr);
    frame.player.reset(x, y, z, heading);
    frame.client.sendSetParent(net::kParentRender);
    active_ = false;
    bossId_ = 0;
}

void CjBattle::loadNpcs(BossFrame& frame) {
    if (npcsLoaded_) return;
    npcsLoaded_ = true;
    FILE* file = std::fopen("sdmc:/3ds/toontown3ds/data/boss/cj_npcs.bin", "rb");
    char magic[4] = {};
    uint16_t version = 0;
    uint16_t count = 0;
    bool valid = file && std::fread(magic, 1, 4, file) == 4 && !std::memcmp(magic, "BND1", 4) &&
        std::fread(&version, 2, 1, file) == 1 && version == 1 &&
        std::fread(&count, 2, 1, file) == 1 && count == 9;
    for (int index = 0; valid && index < count; ++index) {
        uint32_t npcId = 0;
        uint8_t dna[15] = {};
        valid = std::fread(&npcId, 4, 1, file) == 1 && std::fread(dna, 1, 15, file) == 15;
        const int slot = npcId == 0 ? 8 : npcId >= 2001 && npcId <= 2008 ? int(npcId - 2001) : -1;
        ToonDna parsed;
        if (valid && slot >= 0 && parseNetString(dna, 15, &parsed)) frame.cache.acquire(parsed, ToonLod::Low, &looks_[slot]);
    }
    if (file) std::fclose(file);
    for (int index = 0; index < 9; ++index) npcs_[index].setLook(&looks_[index], &frame.cache.fallback());
}

void CjBattle::update(BossFrame& frame, const net::BossState& state, uint32_t nowMs) {
    const float elapsed = (nowMs - state.phaseMs) / 1000.0f;
    if (std::strcmp(phase_, state.phase)) {
        std::snprintf(phase_, sizeof(phase_), "%s", state.phase);
        for (bool& ack : acknowledged_) ack = false;
        charging_ = false;
        for (Evidence& evidence : evidence_) evidence.active = false;
        if (phaseIs(state, "Defeat")) defeated_ = true;
        if (phaseIs(state, "BattleThree")) frame.player.reset(-3, 0, .1f, 0);
        if (phaseIs(state, "BattleTwo") && !state.battleA && !state.battleB) {
            for (int index = 0; index < net::kMaxCjChildren; ++index) {
                const net::CjChild& cannon = frame.client.cjChildSlot(index);
                if (cannon.active && cannon.kind == net::CjKind::Cannon) {
                    frame.player.reset(cannon.x + 4, cannon.y, cannon.z, 90);
                    break;
                }
            }
        }
        TT_LOG("CJ: phase %s", state.phase);
    }
    if (!entered_) {
        frame.client.sendSetParent(net::kParentRender);
        entered_ = frame.client.sendBossEnter();
    }
    for (int index = 0; index < state.numBarriers; ++index) {
        const net::BossBarrier& barrier = state.barriers[index];
        const float delay = cjBarrierDelay(barrier.name);
        if (!barrier.waitingForUs || delay < 0 || elapsed < delay || std::strcmp(barrier.name, state.phase) ||
            (acknowledged_[index] && barriers_[index] == barrier.context)) continue;
        if (frame.client.sendBossBarrier(barrier.context)) {
            acknowledged_[index] = true;
            barriers_[index] = barrier.context;
        }
    }
    const bool cogRound = phaseIs(state, "BattleOne") ||
        (phaseIs(state, "BattleTwo") && (state.battleA || state.battleB));
    if (cogRound && !frame.battle.active()) {
        const uint32_t battleId = member(state.sideA, state.numA, frame.client.avatarId()) ? state.battleA : state.battleB;
        if (battleId && frame.client.findBattle(battleId)) {
            frame.battle.requestJoin(frame.client, battleId, frame.player.x(), frame.player.y(), frame.player.z(),
                                    frame.player.headingDeg(), nowMs);
        }
    }
    if (const net::Battle* battle = frame.client.localBattle()) {
        for (int index = 0; index < battle->numBossBarriers; ++index) {
            const net::BossBarrier& barrier = battle->bossBarriers[index];
            if (!barrier.waitingForUs || std::strcmp(barrier.name, "ReservesJoining") || nowMs - battle->stateMs < 3500 ||
                (reserveAck_ && reserveBattle_ == battle->doId && reserveContext_ == barrier.context)) continue;
            if (frame.client.sendBossBattleBarrier(battle->doId, barrier.context)) {
                reserveBattle_ = battle->doId;
                reserveContext_ = barrier.context;
                reserveAck_ = true;
            }
        }
    }
    frame.battle.setAutoPlay(frame.test.active());
    frame.battle.update(frame.client, nowMs, frame.dt, {SuitActors::poseThunk, &frame.suits});
    frame.suits.update(frame.dt, nowMs, &assets_.collision);
    for (int index = 0; index < frame.battle.numSuitPoses(); ++index) {
        const BattleActorPose& pose = frame.battle.suitPose(index);
        frame.suits.overridePose(pose.doId, pose.x, pose.y, pose.z + pose.hop, pose.h, pose.hidden, pose.flash,
                                 pose.anim, pose.clipArg, pose.clipStartMs);
    }
    const net::OwnStats& stats = frame.client.ownStats();
    frame.battle.fillView(frame.client, nowMs, &battleView_);
    frame.ui.setBattle(frame.battle.active() ? &battleView_ : nullptr,
                       {stats.inventory, stats.experience, stats.trackAccess});
    if (frame.battle.active()) {
        frame.ui.handleInput(frame.input.down, frame.input.held, frame.touch, frame.uiState);
        BattleAction action;
        while (frame.ui.battle().popAction(action)) frame.battle.perform(frame.client, action, nowMs);
    }
    bool controlling = false;
    for (int index = 0; index < net::kMaxCjChildren; ++index) {
        const net::CjChild& child = frame.client.cjChildSlot(index);
        controlling |= child.active && child.kind == net::CjKind::Cannon && child.owner == frame.client.avatarId();
    }
    Input movement = frame.input;
    if ((!phaseIs(state, "BattleTwo") && !phaseIs(state, "BattleThree")) || controlling || frame.battle.active()) movement = {};
    frame.player.setCollision(&assets_.collision);
    frame.player.update(movement, frame.dt);
    BattleActorPose toonPose;
    if (frame.battle.localPose(&toonPose)) {
        frame.player.reset(toonPose.x, toonPose.y, toonPose.z + toonPose.hop, toonPose.h);
        if (toonPose.anim >= kBattleAnimClip0 && toonPose.anim < kBattleAnimClip0 + kNumBattleClips) {
            const ToonAnim clip = toonAnimByName(kBattleClipNames[toonPose.anim - kBattleAnimClip0]);
            frame.toon.playAction(clip, clip == kAnimVictory ? ClipMode::Loop : ClipMode::Once);
        } else {
            frame.toon.play(static_cast<ToonAnim>(toonPose.anim), 1);
        }
    } else if (!frame.toon.actionActive()) {
        float rate = 1;
        const ToonAnim animation = frame.player.animState(&rate);
        frame.toon.play(animation, rate);
    }
    for (int slot = 0; slot < net::kMaxCjChildren; ++slot) {
        const net::CjChild& child = frame.client.cjChildSlot(slot);
        const uint32_t current = child.active ? child.doId : 0;
        if (externalIds_[slot] && externalIds_[slot] != current) frame.suits.removeExternal(externalIds_[slot]);
        externalIds_[slot] = current;
        if (child.kind == net::CjKind::Lawyer &&
            (!phaseIs(state, "BattleThree") || !member(state.lawyers, state.numLawyers, current))) {
            frame.suits.removeExternal(current);
        }
    }
    if (phaseIs(state, "BattleTwo") && !cogRound) updateCannons(frame, state, nowMs);
    if (phaseIs(state, "BattleThree")) updateCourt(frame, state, nowMs);
    // A chair owns its visual Cog, not a network DistributedSuit. Bound this to
    // twelve external actor slots and remove it as soon as a toon replaces it.
    for (int slot = 0; slot < net::kMaxCjChildren; ++slot) {
        const net::CjChild& chair = frame.client.cjChildSlot(slot);
        if (!chair.active || chair.kind != net::CjKind::Chair || chair.bossId != bossId_) continue;
        const bool flying = chair.flyingRevision && nowMs - chair.flyingMs < 10000;
        if (chair.state == 'S' || flying) {
            Vec3 position = cjChairPosition(chair.index, phaseIs(state, "BattleTwo") ? elapsed : kCjJurySeconds);
            if (flying) position.x += 75 * (1 - (nowMs - chair.flyingMs) / 10000.0f);
            frame.suits.setExternal(chair.doId, "bw", 0, position.x, position.y, position.z, -90, false, true);
        } else {
            frame.suits.removeExternal(chair.doId);
        }
    }
    if (nowMs - moveMs_ >= 200) {
        frame.client.sendPosHpr(frame.player.x(), frame.player.y(), frame.player.z(), frame.player.headingDeg(), 0, 0);
        frame.client.sendAnimState(frame.player.airborne() ? "jumpAirborne" : frame.player.moving() ? "run" : "Happy", 1);
        frame.client.setOwnWorldPos(frame.player.x(), frame.player.y(), frame.player.z());
        moveMs_ = nowMs;
    }
    frame.toon.update(frame.dt);
    frame.remotes.update(frame.dt);
    frame.effects.update(frame.dt, nowMs, &frame.battle, frame.toon.look());
    loadNpcs(frame);
    for (ToonAvatar& npc : npcs_) {
        npc.play(kAnimNeutral, 1);
        npc.update(frame.dt);
    }
}

void CjBattle::updateCannons(BossFrame& frame, const net::BossState& state, uint32_t nowMs) {
    using Action = net::Client::CjCannonAction;
    const bool scripted = frame.test.active() && !std::strncmp(frame.test.label(), "cj: ", 4);
    const float juryAge = (nowMs - state.phaseMs) / 1000.0f;
    for (int slot = 0; slot < net::kMaxCjChildren; ++slot) {
        const net::CjChild& cannon = frame.client.cjChildSlot(slot);
        if (!cannon.active || cannon.kind != net::CjKind::Cannon || cannon.bossId != bossId_) continue;
        const Vec3 position{cannon.x, cannon.y, cannon.z};
        const bool ours = cannon.owner == frame.client.avatarId();
        if (!cannon.owner && nowMs - cannonRequestMs_ > 1500 &&
            ((distance(position, {frame.player.x(), frame.player.y(), frame.player.z()}) < 5 && (frame.input.down & KEY_A)) ||
             (scripted && cannon.index == 0))) {
            if (frame.client.sendCjCannon(cannon.doId, Action::Enter)) {
                cannonRequestMs_ = nowMs;
                rotation_ = 0;
                elevation_ = 25;
            }
        }
        if (ours && cannon.movie == 1 && !cannon.inFlight && !cannon.pendingFire) {
            if (frame.input.down & KEY_X) frame.client.sendCjCannon(cannon.doId, Action::Leave);
            rotation_ = std::clamp(rotation_ - frame.input.pad.dx / 156.0f * 15 * frame.dt, -55.0f, 50.0f);
            elevation_ = std::clamp(elevation_ + frame.input.pad.dy / 156.0f * 15 * frame.dt, 10.0f, 85.0f);
            bool fire = (frame.input.down & KEY_B) || ((frame.input.down & KEY_TOUCH) && frame.touch.py > 120);
            if (scripted && nowMs - throwMs_ > 2000) {
                // Predict a moving seat, then use the actual flight/collision path.
                for (int attempt = 0; attempt < 12; ++attempt) {
                    const int chair = (autoChair_ + attempt) % 12;
                    Vec3 target = cjChairPosition(chair, juryAge + 1.8f);
                    target.z += 2;
                    if (cjAimCannon(position, cannon.h, assets_.barrelPivot, target, &rotation_, &elevation_)) {
                        fire = true;
                        autoChair_ = (chair + 1) % 12;
                        break;
                    }
                }
            }
            if (nowMs - aimMs_ >= 500) {
                frame.client.sendCjCannon(cannon.doId, Action::Aim, rotation_, elevation_);
                aimMs_ = nowMs;
            }
            if (fire && frame.client.sendCjCannon(cannon.doId, Action::Fire, rotation_, elevation_)) throwMs_ = nowMs;
        }
        if (cannon.fireRevision && cannonFire_[cannon.index] != cannon.fireRevision) {
            cannonFire_[cannon.index] = cannon.fireRevision;
            for (JurorFlight& flight : flights_) {
                if (flight.active) continue;
                flight = {};
                flight.active = true;
                flight.cannonId = cannon.doId;
                flight.owner = cannon.owner;
                flight.npc = cannon.index;
                flight.startMs = cannon.fireMs + static_cast<uint32_t>(std::max(0.0f, cannon.fireDelay) * 1000);
                flight.trajectory = cjCannonFlight(position, cannon.h, cannon.rotation, cannon.elevation, assets_.barrelPivot);
                flight.previous = flight.trajectory.origin;
                flight.position = flight.previous;
                break;
            }
        }
    }
    for (JurorFlight& flight : flights_) {
        if (!flight.active) continue;
        if (flight.landedMs) {
            if (nowMs - flight.landedMs >= 1000) {
                if (flight.owner == frame.client.avatarId()) frame.client.sendCjCannon(flight.cannonId, Action::Landed);
                flight.active = false;
            }
            continue;
        }
        const int32_t milliseconds = static_cast<int32_t>(nowMs - flight.startMs);
        if (milliseconds < 0) continue;
        const float age = milliseconds / 1000.0f;
        flight.position = cjFlightPosition(flight.trajectory, age);
        bool landed = false;
        for (int slot = 0; slot < net::kMaxCjChildren; ++slot) {
            const net::CjChild& chair = frame.client.cjChildSlot(slot);
            if (!chair.active || chair.kind != net::CjKind::Chair || chair.bossId != bossId_) continue;
            const Vec3 seat = cjChairPosition(chair.index, juryAge);
            if (segmentBox(flight.previous, flight.position, {seat.x - 3, seat.y - 3, seat.z - 2},
                {seat.x + 3, seat.y + 3, seat.z + 5})) {
                if (flight.owner == frame.client.avatarId()) frame.client.sendCjChair(flight.cannonId, chair.index);
                flight.position = seat;
                landed = true;
                break;
            }
        }
        if (!landed) {
            bool floor = false;
            const float height = assets_.collision.groundHeight(flight.position.x, flight.position.y, std::max(flight.previous.z, flight.position.z) + 2, &floor, 0);
            if ((floor && flight.trajectory.velocity.z - kCjGravity * age < 0 && flight.position.z <= height + 1) ||
                flight.position.z < -15 || age > 8) landed = true;
            float hit = 0;
            landed |= assets_.collision.cameraRaycast(flight.previous, flight.position, &hit);
        }
        if (landed) flight.landedMs = nowMs;
        flight.previous = flight.position;
    }
}

Vec3 CjBattle::panCenter(int model, float damage) const {
    const Vec3 local = model == 8 ? assets_.defenseLocal : assets_.prosecutionLocal;
    Vec3 point = cjScalePan(assets_.beamPivot, local, damage);
    const render::Model& pan = assets_.models[model];
    const float* low = pan.boundsMin();
    const float* high = pan.boundsMax();
    // Counter-rotation keeps the pan level; its model-space centre is static.
    point.x -= (low[1] + high[1]) * .5f * 1.75f;
    point.y += (low[0] + high[0]) * .5f * 1.75f;
    point.z += (low[2] + high[2]) * .5f * 1.5f;
    return point;
}

void CjBattle::updateCourt(BossFrame& frame, const net::BossState& state, uint32_t nowMs) {
    using Hit = net::Client::CjEvidence;
    const bool scripted = frame.test.active() && !std::strcmp(frame.test.label(), "cj: court");
    const Vec3 toon{frame.player.x(), frame.player.y(), frame.player.z()};
    const float damage = net::currentBossDamage(state, nowMs);
    if (distance(toon, kCjWitnessPosition) < 7 && nowMs - witnessMs_ > 1500) {
        if (frame.client.sendCjEvidence(Hit::Witness)) witnessMs_ = nowMs;
    }
    pitch_ = std::clamp(pitch_ + ((frame.input.held & KEY_DUP) ? 40 : 0) * frame.dt -
        ((frame.input.held & KEY_DDOWN) ? 40 : 0) * frame.dt, -30.0f, 60.0f);
    const bool held = (frame.input.held & KEY_B) != 0;
    if (held && !charging_) {
        charging_ = true;
        charge_ = 0;
    }
    if (charging_) charge_ += frame.dt;
    bool throwing = (charging_ && !held) || ((frame.input.down & KEY_TOUCH) && frame.touch.py > 120);
    if (scripted) {
        if (!frame.client.bossPies()) {
            frame.player.reset(kCjWitnessPosition.x, kCjWitnessPosition.y - 3, 0, 0);
        } else {
            const Vec3 pan = panCenter(8, damage);
            const Vec3 origin{pan.x + 25, pan.y - 35, 2.24f};
            frame.player.reset(origin.x, origin.y, 0, std::atan2(pan.x - origin.x, -(pan.y - origin.y)) / kDegrees + 180);
            const float range = std::sqrt(25.0f * 25 + 35.0f * 35);
            const float height = pan.z - origin.z;
            // Toon.getTossPieInterval power zero has velocity (100,16.087), gravity 32.174.
            constexpr float gravity = 32.174f;
            constexpr float speedSquared = 10000 + 16.087f * 16.087f;
            const float discriminant = speedSquared * speedSquared - gravity *
                (gravity * range * range + 2 * height * speedSquared);
            if (discriminant > 0) {
                pitch_ = std::atan((speedSquared - std::sqrt(discriminant)) / (gravity * range)) / kDegrees -
                    std::atan(16.087f / 100) / kDegrees;
                throwing |= nowMs - throwMs_ > 1000;
            }
        }
    }
    if (throwing && nowMs - throwMs_ > 500) {
        for (Evidence& evidence : evidence_) {
            if (evidence.active) continue;
            if (!frame.client.spendBossPie()) break;
            evidence = {};
            evidence.active = true;
            evidence.origin = {frame.player.x(), frame.player.y(), frame.player.z() + 2.24f};
            evidence.previous = evidence.origin;
            evidence.heading = frame.player.headingDeg();
            evidence.pitch = pitch_;
            evidence.power = scripted ? 0 : charging_ ? piePower(charge_) : .5f;
            evidence.age = -16.0f / 24;
            frame.toon.playAction(kAnimThrow, ClipMode::Once, 1, 2, -1, true);
            throwMs_ = nowMs;
            break;
        }
        charging_ = false;
    }
    const Vec3 defense = panCenter(8, damage);
    const Vec3 prosecution = panCenter(9, damage);
    for (Evidence& evidence : evidence_) {
        if (!evidence.active) continue;
        evidence.age += frame.dt;
        if (evidence.age < 0) continue;
        const Vec3 current = piePosition(evidence.origin, evidence.heading, evidence.pitch, evidence.power, evidence.age);
        if (segmentBox(evidence.previous, current, {defense.x - 5, defense.y - 5, defense.z - 2},
            {defense.x + 5, defense.y + 5, defense.z + 2})) {
            frame.client.sendCjEvidence(Hit::Defense);
            evidence.active = false;
        } else if (segmentBox(evidence.previous, current, {prosecution.x - 5, prosecution.y - 5, prosecution.z - 2},
            {prosecution.x + 5, prosecution.y + 5, prosecution.z + 2})) {
            // Stock local pies on the prosecution pan are cosmetic, never healBoss.
            evidence.active = false;
        }
        for (int index = 0; evidence.active && index < state.numLawyers; ++index) {
            const net::CjChild* lawyer = frame.client.findCjChild(state.lawyers[index]);
            if (!lawyer) continue;
            if (segmentBox(evidence.previous, current, {lawyer->x - 2, lawyer->y - 2, lawyer->z},
                {lawyer->x + 2, lawyer->y + 2, lawyer->z + 10})) {
                frame.client.sendCjEvidence(Hit::Lawyer, lawyer->doId);
                evidence.active = false;
            }
        }
        for (int index = 0; evidence.active && index < state.numToons; ++index) {
            const uint32_t id = state.toons[index];
            if (id == frame.client.avatarId()) continue;
            float x = 0;
            float y = 0;
            float z = 0;
            if (frame.client.avatarPos(id, &x, &y, &z) && segmentBox(evidence.previous, current,
                {x - 1.4f, y - 1.4f, z}, {x + 1.4f, y + 1.4f, z + 5})) {
                frame.client.sendCjEvidence(Hit::Toon, id);
                evidence.active = false;
            }
        }
        evidence.previous = current;
        if (evidence.age > 3 || current.z < -15) evidence.active = false;
    }
    for (int index = 0; index < state.numLawyers; ++index) {
        const net::CjChild* lawyer = frame.client.findCjChild(state.lawyers[index]);
        if (!lawyer) continue;
        char code[4] = {};
        if (lawyer->dna[0] == 's') std::memcpy(code, lawyer->dna + 1, 3);
        if (!code[0]) std::strcpy(code, "bw");
        frame.suits.setExternal(lawyer->doId, code, 0, lawyer->x, lawyer->y, lawyer->z, lawyer->h, false, true);
        const bool stunned = lawyer->stunRevision && nowMs - lawyer->stunMs < 5000;
        frame.suits.overridePose(lawyer->doId, lawyer->x, lawyer->y, lawyer->z, lawyer->h, false,
            stunned ? .3f : 0, stunned ? kSuitAnimReactPie : 0);
        const float age = (nowMs - lawyer->attackMs) / 1000.0f - 3.15f; // BossSuit timeToRelease.
        if (!stunned && lawyer->attackRevision && age >= 0 && age < 3 && lawyerHit_[index] != lawyer->attackRevision) {
            const Vec3 start{lawyer->x, lawyer->y, 1.3f};
            const Vec3 end{lawyer->attack[3], lawyer->attack[4], lawyer->attack[5]};
            const float duration = 3; // DistributedLawbotBossSuit.enterPostThrowAttack.
            const float before = std::max(0.0f, (age - frame.dt) / duration);
            const float after = age / duration;
            const Vec3 previous{start.x + (end.x - start.x) * before, start.y + (end.y - start.y) * before,
                                start.z + (end.z - start.z) * before};
            const Vec3 current{start.x + (end.x - start.x) * after, start.y + (end.y - start.y) * after,
                               start.z + (end.z - start.z) * after};
            if (segmentBox(previous, current, {toon.x - 1.4f, toon.y - 1.4f, toon.z},
                {toon.x + 1.4f, toon.y + 1.4f, toon.z + 5})) {
                frame.client.sendCjZap(toon.x, toon.y, toon.z, frame.player.headingDeg(), 15);
                lawyerHit_[index] = lawyer->attackRevision;
            }
        }
    }
    for (int slot = 0; slot < net::kMaxCjChildren; ++slot) {
        const net::CjChild& gavel = frame.client.cjChildSlot(slot);
        if (!gavel.active || gavel.kind != net::CjKind::Gavel || gavel.state != 'N' || gavel.bossId != bossId_) continue;
        const CjGavelPose pose = cjGavelPose(gavel.index, (nowMs - gavel.stateMs) / 1000.0f);
        const CjGavelLayout& layout = kCjGavels[gavel.index];
        const float tipHeight = assets_.models[13].boundsMax()[2] * 3;
        const Vec3 offset = cjRotate({0, 0, tipHeight}, pose.heading, pose.pitch);
        const Vec3 tip{layout.position.x + offset.x, layout.position.y + offset.y, layout.position.z + offset.z};
        const bool head = distance(toon, tip) < 4 && pose.pitch < -65;
        const bool handle = distance(toon, layout.position) < 2.5f && std::fabs(toon.z - layout.position.z) < 5;
        if ((head || handle) && nowMs - gavelHit_[gavel.index] > 1500) {
            frame.client.sendCjZap(toon.x, toon.y, toon.z, frame.player.headingDeg(), head ? 13 : 14);
            gavelHit_[gavel.index] = nowMs;
        }
    }
    if (bossAttack_ != state.attackRevision) {
        bossAttack_ = state.attackRevision;
        bossAttackHit_ = false;
    }
    const float attackAge = (nowMs - state.attackMs) / 1000.0f;
    if (state.attack == net::kBossAreaAttack && attackAge >= 1.21f && attackAge < 1.6f &&
        !frame.player.airborne() && !bossAttackHit_) {
        bossAttackHit_ = frame.client.sendCjZap(toon.x, toon.y, toon.z, frame.player.headingDeg(), 4);
    }
}

void CjBattle::drawBoss(BossFrame& frame, const net::BossState& state) {
    if (phaseIs(state, "Reward") || phaseIs(state, "Epilogue")) return;
    const bool front = phaseIs(state, "WaitForToons") || phaseIs(state, "Elevator") ||
                       phaseIs(state, "Introduction") || phaseIs(state, "BattleOne");
    const Vec3 position = front ? Vec3{-2.798f, -60, 0} : Vec3{-2.798f, 89, 19.145f};
    C3D_Mtx world = transform(position);
    C3D_Mtx torso = world;
    C3D_Mtx head = world;
    for (int part = 0; part < 4; ++part) {
        render::Anim& animation = assets_.animations[part];
        if (!animation.loaded()) continue;
        C3D_Mtx partWorld = part == 1 ? torso : part == 2 ? head : world;
        u32 first = 0;
        u32 second = 0;
        float blend = 0;
        if (std::strstr(assets_.currentClip, "neutral") || !std::strcmp(assets_.currentClip, "Ff_speech")) {
            render::sampleClip(animation, assets_.clipTime, &first, &second, &blend);
        } else {
            const float sample = std::min(assets_.clipTime * animation.fps(), float(animation.numFrames() - 1));
            first = static_cast<u32>(sample);
            second = std::min(first + 1, animation.numFrames() - 1);
            blend = sample - first;
        }
        frame.renderer.drawMorph(assets_.models[part], animation, first, second, blend, partWorld);
        if (animation.hasAttach() && part < 2) {
            C3D_Mtx attachment;
            animation.attach(first, second, blend, &attachment);
            Mtx_Multiply(part == 0 ? &torso : &head, &partWorld, &attachment);
        }
    }
}

void CjBattle::draw(BossFrame& frame, const net::BossState& state, uint32_t nowMs) {
    const float damage = net::currentBossDamage(state, nowMs);
    C3D_FVec eye = frame.player.cameraEye();
    C3D_FVec target = frame.player.cameraTarget();
    if (frame.battle.inBattle()) {
        frame.battle.cameraEye(&eye.x, &eye.y, &eye.z);
        frame.battle.cameraTarget(&target.x, &target.y, &target.z);
    } else if (phaseIs(state, "BattleTwo")) {
        for (int slot = 0; slot < net::kMaxCjChildren; ++slot) {
            const net::CjChild& cannon = frame.client.cjChildSlot(slot);
            if (!cannon.active || cannon.kind != net::CjKind::Cannon || cannon.owner != frame.client.avatarId()) continue;
            eye = FVec3_New(cannon.x - 18, cannon.y - 8, cannon.z + 18);
            target = FVec3_New(55, cannon.y, 18);
            break;
        }
    }
    gfxSet3D(false);
    frame.stereoActive = false;
    frame.renderer.resetStats();
    frame.renderer.beginWorld(frame.renderer.topLeft(), eye, target, 0, 0);
    if (assets_.ready()) {
        C3D_Mtx identity = transform({0, 0, 0});
        frame.renderer.drawModel(assets_.models[4], identity);
        const Vec3 juryOffset = phaseIs(state, "BattleTwo") ?
            cjJuryOffset((nowMs - state.phaseMs) / 1000.0f) :
            phaseIs(state, "WaitForToons") || phaseIs(state, "Elevator") ||
            phaseIs(state, "Introduction") || phaseIs(state, "BattleOne") ||
            phaseIs(state, "RollToBattleTwo") || phaseIs(state, "PrepareBattleTwo") ? cjJuryOffset(0) : Vec3{};
        C3D_Mtx jury = transform(juryOffset);
        frame.renderer.drawModel(assets_.models[5], jury);
        for (int slot = 0; slot < net::kMaxCjChildren; ++slot) {
            const net::CjChild& child = frame.client.cjChildSlot(slot);
            if (!child.active || (child.bossId && child.bossId != bossId_)) continue;
            if (child.kind == net::CjKind::Cannon) {
                const bool ours = child.owner == frame.client.avatarId();
                C3D_Mtx base = transform({child.x, child.y, child.z}, child.h + (ours ? rotation_ : child.rotation));
                frame.renderer.drawModel(assets_.models[10], base);
                Mtx_Translate(&base, assets_.barrelPivot.x, assets_.barrelPivot.y, assets_.barrelPivot.z, true);
                Mtx_RotateX(&base, (ours ? elevation_ : child.elevation) * kDegrees, true);
                frame.renderer.drawModel(assets_.models[11], base);
            } else if (child.kind == net::CjKind::Chair && child.state != 'F') {
                const Vec3 position = cjChairPosition(child.index, phaseIs(state, "BattleTwo") ? (nowMs - state.phaseMs) / 1000.0f : juryOffset.x < 0 ? 0 : kCjJurySeconds);
                C3D_Mtx chair = transform(position, -90);
                frame.renderer.drawModel(assets_.models[12], chair);
                if (child.state == 'T' && child.juror >= 0 && child.juror < 8) {
                    C3D_Mtx juror = transform({position.x, position.y, position.z + 1.5f}, -90);
                    npcs_[child.juror].draw(frame.renderer, juror);
                }
            } else if (child.kind == net::CjKind::Gavel && child.state == 'N') {
                const CjGavelPose pose = cjGavelPose(child.index, (nowMs - child.stateMs) / 1000.0f);
                C3D_Mtx gavel = transform(kCjGavels[child.index].position, pose.heading, pose.pitch, 3);
                frame.renderer.drawModel(assets_.models[13], gavel);
            } else if (child.kind == net::CjKind::Lawyer && child.attackRevision) {
                const float age = (nowMs - child.attackMs) / 1000.0f - 3.15f;
                if (age >= 0 && age < 3) {
                    const float fraction = age / 3;
                    const Vec3 start{child.x, child.y, 1.3f};
                    const Vec3 end{child.attack[3], child.attack[4], child.attack[5]};
                    C3D_Mtx paper = transform({start.x + (end.x - start.x) * fraction,
                                              start.y + (end.y - start.y) * fraction,
                                              start.z + (end.z - start.z) * fraction});
                    frame.renderer.drawModel(assets_.models[15], paper);
                }
            }
        }
        const bool court = phaseIs(state, "PrepareBattleThree") || phaseIs(state, "BattleThree") ||
                           phaseIs(state, "Victory") || phaseIs(state, "Reward") || phaseIs(state, "Epilogue");
        if (court) {
            C3D_Mtx stand = scaleRoot();
            frame.renderer.drawModel(assets_.models[6], stand);
            Mtx_Translate(&stand, assets_.beamPivot.x, assets_.beamPivot.y, assets_.beamPivot.z, true);
            Mtx_RotateX(&stand, cjScaleTilt(damage) * kDegrees, true);
            frame.renderer.drawModel(assets_.models[7], stand);
            for (int pan = 0; pan < 2; ++pan) {
                C3D_Mtx matrix = stand;
                const Vec3 local = pan == 0 ? assets_.defenseLocal : assets_.prosecutionLocal;
                Mtx_Translate(&matrix, local.x, local.y, local.z, true);
                Mtx_RotateX(&matrix, -cjScaleTilt(damage) * kDegrees, true);
                frame.renderer.drawModel(assets_.models[8 + pan], matrix);
            }
            C3D_Mtx witness = phaseIs(state, "Epilogue") ? transform({-3, 0, 0}, 180) : transform(kCjWitnessPosition, -90);
            npcs_[8].draw(frame.renderer, witness);
        }
        for (const JurorFlight& flight : flights_) {
            if (!flight.active || nowMs < flight.startMs) continue;
            C3D_Mtx matrix = transform(flight.position, -90);
            npcs_[flight.npc].draw(frame.renderer, matrix);
        }
        for (const Evidence& evidence : evidence_) {
            if (!evidence.active || evidence.age < 0) continue;
            C3D_Mtx matrix = transform(evidence.previous, evidence.heading);
            frame.renderer.drawModel(assets_.models[14], matrix);
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
    frame.stats = frame.renderer.stats();
    ui::Context context{};
    context.stats = &frame.client.ownStats();
    context.infoTitle = assets_.failed() ? "CJ assets missing / over budget" : !assets_.ready() ? "Loading CJ courtroom" : state.phase;
    std::snprintf(status_, sizeof(status_), "Scales %.0f / 2700  Laff %d%s", damage, frame.client.ownStats().hp,
                  state.bonusRevision && nowMs - state.bonusMs < 20000 ? "  BONUS" : "");
    std::snprintf(details_, sizeof(details_), "Evidence %u  Cannon %.0f / %.0f  Aim %.0f", unsigned(frame.client.bossPies()), rotation_, elevation_, pitch_);
    if (phaseIs(state, "Reward") || phaseIs(state, "Epilogue")) {
        std::snprintf(details_, sizeof(details_), "Summon inventory %u / 96%s", frame.client.bossSummonsCount(),
                      state.promoted ? "  Promotion earned" : "");
    }
    context.infoLines[0] = status_;
    context.infoLines[1] = details_;
    context.infoLines[2] = assets_.failed() ? "Copy data/boss/cj_*; B: return" : (phaseIs(state, "Epilogue") || phaseIs(state, "Frolic")) ?
        "A: return; server owns rewards / defeat" : phaseIs(state, "BattleTwo") ?
        "A: cannon  Pad: aim  B: fire  X: leave" : "A: jump  B: evidence  D-pad: aim  Witness: refill";
    context.heapUsedKB = mallinfo().uordblks / 1024;
    context.linearFreeKB = linearSpaceFree() / 1024;
    context.vramFreeKB = vramSpaceFree() / 1024;
    frame.ui.draw(frame.bottom, frame.uiState, context);
    C2D_DrawRectSolid(12, 196, .8f, std::clamp(damage / 2700, 0.0f, 1.0f) * 296, 5, C2D_Color32(220, 80, 40, 255));
}

bool CjBattle::frame(BossFrame& frame) {
    const uint32_t nowMs = net::plat::nowMs();
    if (!active_) {
        board(frame, nowMs);
        const net::BossState* state = frame.client.boss();
        if (!state || state->kind != net::BossKind::Lawbot) return false;
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
        (frame.test.active() && !std::strcmp(frame.test.label(), "cj: end"));
    if (!state.active || state.doId != bossId_ || frame.client.ownStats().hp <= 0 ||
        (assets_.failed() && (frame.input.down & KEY_B)) ||
        ((phaseIs(state, "Epilogue") || phaseIs(state, "Frolic")) && ending && nowMs - state.phaseMs > 1500)) {
        leave(frame, defeated_ || frame.client.ownStats().hp <= 0);
        return false;
    }
    if (assets_.ready()) {
        update(frame, state, nowMs);
    } else {
        if (!entered_) entered_ = frame.client.sendBossEnter();
        if (phaseIs(state, "WaitForToons") && nowMs - state.phaseMs >= 500) {
            for (int index = 0; index < state.numBarriers; ++index) {
                const net::BossBarrier& barrier = state.barriers[index];
                if (barrier.waitingForUs && !std::strcmp(barrier.name, "WaitForToons") &&
                    (!acknowledged_[index] || barriers_[index] != barrier.context) && frame.client.sendBossBarrier(barrier.context)) {
                    acknowledged_[index] = true;
                    barriers_[index] = barrier.context;
                }
            }
        }
    }
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    frame.cache.pump();
    if (frame.shot) {
        char path[128];
        std::snprintf(path, sizeof(path), "sdmc:/3ds/toontown3ds/shot_%s_top.bin", frame.shot);
        shot::dumpTarget(frame.renderer.topLeft(), path);
        std::snprintf(path, sizeof(path), "sdmc:/3ds/toontown3ds/shot_%s_bottom.bin", frame.shot);
        shot::dumpTarget(frame.bottom, path);
        frame.shot = nullptr;
    }
    assets_.pump(cjClip(state, nowMs));
    assets_.clipTime += frame.dt;
    draw(frame, state, nowMs);
    frame.commandUsage = C3D_GetCmdBufUsage();
    C3D_FrameEnd(0);
    audio::update();
    return true;
}

} // namespace tt::world
