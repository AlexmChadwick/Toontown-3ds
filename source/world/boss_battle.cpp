#include "world/boss_battle.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <malloc.h>

#include "audio/audio.h"
#include "core/log.h"
#include "core/memory.h"
#include "core/screenshot.h"
#include "net/net_platform.h"
#include "ui/sos_tables.h"

namespace tt::world {
namespace {
constexpr float kDegrees = 3.14159265359f / 180.0f;
constexpr const char* kDirectory = "sdmc:/3ds/toontown3ds/data/boss/";
const char* const kModelNames[] = {
    "vp_legs", "vp_torso", "vp_head", "vp_treads", "vp_arena", "vp_cage",
    "vp_cage_door", "vp_ramp_a", "vp_ramp_b", "vp_ramp_c", "vp_gear", "vp_pie"
};
const char* const kCfoModelNames[] = {
    "cfo_legs", "cfo_torso", "cfo_head", "cfo_treads", "cfo_arena", "cfo_crane_arm",
    "cfo_controls", "cfo_stick", "cfo_magnet", "cfo_safe", "cfo_locomotive",
    "cfo_boxcar", "cfo_tankcar", "cfo_gear", "cfo_treasure", "goon_pg_full", "goon_cone", "cfo_cable"
};
const char* modelName(bool cashbot, int index) {
    return cashbot ? kCfoModelNames[index] : kModelNames[index];
}
void modelPath(char* path, size_t capacity, bool cashbot, int index) {
    const char* directory = cashbot && (index == 15 || index == 16) ? "sdmc:/3ds/toontown3ds/data/level/props/" : kDirectory;
    std::snprintf(path, capacity, "%s%s.ttm", directory, modelName(cashbot, index));
}
constexpr uint32_t kClipLimit = 2800000;
constexpr uint32_t kCfoClipLimit = 2004480;
constexpr uint32_t kCfoGoonReserve = 1536 * 1024;
uint32_t clipLimit(bool cashbot) {
    return cashbot ? kCfoClipLimit : kClipLimit;
}
constexpr uint32_t kGeometryLimit = 6 * 1024 * 1024;

bool participant(const uint32_t* ids, int count, uint32_t id) {
    for (int i = 0; i < count; ++i) {
        if (ids[i] == id) return true;
    }
    return false;
}

// Validate the complete clip before handing it to the existing background loader.
// A pair of clips plus all model geometry stays below the zone's 6 MiB pool.
bool clipBudget(const char* name, const render::Model* models, bool cashbot) {
    uint64_t total = 0;
    for (int part = 0; part < 4; ++part) {
        char path[160];
        std::snprintf(path, sizeof(path), "%s%s_%s.tta", kDirectory, modelName(cashbot, part), name);
        FILE* file = std::fopen(path, "rb");
        uint32_t header[16] = {};
        const bool read = file && std::fread(header, 1, 64, file) == 64;
        long size = 0;
        if (file) {
            if (std::fseek(file, 0, SEEK_END) == 0) size = std::ftell(file);
            std::fclose(file);
        }
        uint16_t shorts[32];
        float floats[16];
        std::memcpy(shorts, header, sizeof(header));
        std::memcpy(floats, header, sizeof(header));
        const uint32_t vertices = header[2];
        const uint32_t frames = shorts[6];
        const uint64_t end = uint64_t(header[11]) + uint64_t(frames) * header[12];
        const bool attachment = (shorts[3] & 1) != 0;
        if (!read || std::memcmp(header, "TTA1", 4) || shorts[2] != 1 || shorts[3] > 1 ||
            shorts[7] || vertices != models[part].numVertices() || !vertices || vertices > 4096 ||
            !frames || frames > 72 || !std::isfinite(floats[4]) || floats[4] <= 0 || floats[4] > 24 ||
            header[12] != vertices * 8 || header[11] < 64 || size < 0 || end > uint64_t(size) ||
            (attachment && (header[13] < end || uint64_t(header[13]) + frames * 48 > uint64_t(size))) ||
            (part < 2 && !attachment)) return false;
        for (int axis = 0; axis < 3; ++axis) {
            if (!std::isfinite(floats[5 + axis]) || floats[5 + axis] <= 0 ||
                !std::isfinite(floats[8 + axis])) return false;
        }
        total += uint64_t(frames) * vertices * 8;
    }
    return total <= clipLimit(cashbot);
}

bool modelBudgets(bool cashbot) {
    uint64_t geometry = 0;
    uint64_t textures = 0;
    for (int index = 0; index < (cashbot ? 18 : 12); ++index) {
        char path[160];
        modelPath(path, sizeof(path), cashbot, index);
        FILE* file = std::fopen(path, "rb");
        uint32_t header[16] = {};
        if (!file) return false;
        bool valid = std::fread(header, 1, 64, file) == 64;
        uint16_t version = 0;
        uint16_t textureCount = 0;
        uint16_t batchCount = 0;
        std::memcpy(&version, reinterpret_cast<const uint8_t*>(header) + 4, 2);
        std::memcpy(&textureCount, reinterpret_cast<const uint8_t*>(header) + 8, 2);
        std::memcpy(&batchCount, reinterpret_cast<const uint8_t*>(header) + 10, 2);
        valid &= !std::memcmp(header, "TTM1", 4) && version == 1 && textureCount <= 64 && batchCount <= 150;
        valid &= header[3] > 0 && header[3] <= 65535 && header[4] % 3 == 0;
        geometry += uint64_t(header[3]) * 16 + uint64_t(header[4]) * 2;
        if (std::fseek(file, 0, SEEK_END)) valid = false;
        const long size = std::ftell(file);
        valid &= size >= 64 && uint64_t(header[14]) + uint64_t(header[4]) * 2 <= uint64_t(std::max(0L, size));
        if (std::fseek(file, header[11], SEEK_SET)) valid = false;
        for (int index = 0; valid && index < textureCount; ++index) {
            render::TexEntry texture{};
            valid = std::fread(&texture, 1, sizeof(texture), file) == sizeof(texture);
            valid &= texture.width > 0 && texture.width <= 128 && texture.height > 0 && texture.height <= 128;
            valid &= uint64_t(texture.dataOffset) + texture.dataSize <= uint64_t(size);
            textures += texture.dataSize;
        }
        std::fclose(file);
        if (!valid) return false;
    }
    return geometry + 2 * clipLimit(cashbot) + (cashbot ? kCfoGoonReserve : 0) <= kGeometryLimit && textures <= 8 * 1024 * 1024;
}

C3D_Mtx transform(Vec3 position, float heading = 0, float scale = 1) {
    C3D_Mtx matrix;
    Mtx_Identity(&matrix);
    Mtx_Translate(&matrix, position.x, position.y, position.z, true);
    Mtx_RotateZ(&matrix, heading * kDegrees, true);
    Mtx_Scale(&matrix, scale, scale, scale);
    return matrix;
}
}

BossBattle::~BossBattle() {
    release();
}

void BossBattle::release() {
    cfo_.release();
    if (modelTicket_) AssetLoader::cancel(modelTicket_);
    modelTicket_ = 0;
    for (int part = 0; part < kParts; ++part) {
        if (clipTickets_[part]) AssetLoader::cancel(clipTickets_[part]);
        clipTickets_[part] = 0;
        animations_[part].unload();
    }
    for (render::Model& model : models_) model.unload();
    collision_.unload();
    for (CollisionWorld& collision : rampCollision_) collision.unload();
    ready_ = false;
    budgetChecked_ = false;
    modelIndex_ = 0;
    currentClip_[0] = pendingClip_[0] = failedClip_[0] = 0;
}

void BossBattle::leave(BossFrame& frame) {
    // All GPU users have completed before resources or the underlying place change.
    C3D_FrameSync();
    frame.client.sendBossNear(false);
    frame.client.sendBossExit();
    frame.cache.release(&rescuedLook_);
    rescuedToon_.setLook(nullptr);
    rescuedNpc_ = 0;
    release();
    frame.battle.reset();
    frame.remotes.clear();
    frame.ui.setBattle(nullptr, {});
    if (frame.client.ownStats().hp <= 0) home_ = 5000;
    if (!frame.place.load(home_)) {
        home_ = cashbot_ ? 12000 : 11000;
        frame.place.load(home_);
    }
    if (frame.client.zoneId() != home_) frame.client.changeZone(home_);
    float x = 0;
    float y = 0;
    float z = 0;
    float heading = 0;
    frame.place.arrival(0, &x, &y, &z, &heading);
    frame.place.finishLoading(x, y, z, nullptr, nullptr);
    frame.player.setDynamic(nullptr);
    frame.player.setCollision(&frame.place.collision());
    frame.player.reset(x, y, z, heading);
    frame.client.sendSetParent(net::kParentRender);
    active_ = false;
    bossId_ = 0;
}

void BossBattle::board(BossFrame& frame, uint32_t nowMs) {
    for (int slot = 0; slot < net::kMaxCogElevators; ++slot) {
        const net::CogElevator& elevator = frame.client.cogElevatorSlot(slot);
        if (!elevator.active || (elevator.classId != dc::DistributedVPElevator::kClassId && elevator.classId != dc::DistributedCFOElevator::kClassId) || !elevator.destZone) continue;
        if (!participant(elevator.slots, 8, frame.client.avatarId()) && !elevator.forcedBossDestination) continue;
        if (elevator.destZone == frame.client.zoneId()) continue;
        frame.client.sendSetParent(1);
        frame.client.changeZone(elevator.destZone);
        frame.remotes.clear();
        return;
    }
    const bool cfoScript = frame.test.active() && !std::strcmp(frame.test.label(), "cfo: board");
    const bool scripted = cfoScript || (frame.test.active() && !std::strcmp(frame.test.label(), "boss: board"));
    const bool cashbot = cfoScript || frame.place.zoneId() == 12100;
    const uint32_t lobby = cashbot ? 12100 : 11100;
    const uint16_t elevatorClass = cashbot ? dc::DistributedCFOElevator::kClassId : dc::DistributedVPElevator::kClassId;
    if (scripted && frame.place.zoneId() != lobby) {
        frame.uiState.teleportRequest = lobby;
        return;
    }
    if (frame.place.zoneId() != lobby || !frame.client.inZone()) return;
    if (anchorLobby_ != lobby) {
        anchorLobby_ = lobby;
        anchorValid_ = false;
        char path[128];
        std::snprintf(path, sizeof(path), "%s%s_lobby_anchor.txt", kDirectory, cashbot ? "cfo" : "vp");
        if (FILE* file = std::fopen(path, "r")) {
            unsigned version = 0;
            anchorValid_ = std::fscanf(file, "BPA1 %u %f %f %f", &version, &anchor_[0], &anchor_[1], &anchor_[2]) == 4 &&
                           version == 1 && std::isfinite(anchor_[0]) && std::isfinite(anchor_[1]) &&
                           std::isfinite(anchor_[2]);
            std::fclose(file);
        }
        TT_LOG("boss: lobby %u elevator anchor %s", unsigned(lobby), anchorValid_ ? "loaded" : "missing");
    }
    if (!anchorValid_) return;
    const float x = anchor_[0];
    const float y = anchor_[1];
    const float z = anchor_[2];
    if (scripted) frame.player.reset(x, y, z + 0.1f, 180);
    const float dx = frame.player.x() - x;
    const float dy = frame.player.y() - y;
    const float dz = frame.player.z() - z;
    if (dx * dx + dy * dy + dz * dz > 25 || nowMs - boardMs_ < 3000) return;
    for (int slot = 0; slot < net::kMaxCogElevators; ++slot) {
        const net::CogElevator& elevator = frame.client.cogElevatorSlot(slot);
        if (!elevator.active || elevator.classId != elevatorClass) continue;
        if (participant(elevator.slots, 8, frame.client.avatarId())) {
            if (frame.input.down & KEY_B) frame.client.sendCogElevatorExit(elevator.doId);
        } else if (scripted || (frame.input.down & KEY_A)) {
            if (frame.client.sendCogElevatorBoard(elevator.doId)) boardMs_ = nowMs;
            TT_LOG("boss: requestBoard elevator %u (state %s)", unsigned(elevator.doId), elevator.state);
        }
    }
}

void BossBattle::pushBoss(void* user, float* x, float* y, float z, float radius) {
    const BossBattle& boss = *static_cast<const BossBattle*>(user);
    if (boss.cashbot_) boss.cfo_.push(x, y, z, radius);
    if (!boss.bossSolid_ || z > boss.bossPosition_.z + 26 || z + 5 < boss.bossPosition_.z) return;
    float dx = *x - boss.bossPosition_.x;
    float dy = *y - boss.bossPosition_.y;
    const float distance = std::sqrt(dx * dx + dy * dy);
    const float limit = 11 + radius;
    if (distance >= limit) return;
    if (distance < .001f) {
        dx = 0;
        dy = -1;
    } else {
        dx /= distance;
        dy /= distance;
    }
    *x = boss.bossPosition_.x + dx * limit;
    *y = boss.bossPosition_.y + dy * limit;
}

bool BossBattle::rampFloor(void* user, float x, float y, float z, float* top) {
    const BossBattle& boss = *static_cast<const BossBattle*>(user);
    if (boss.cashbot_) return boss.cfo_.floor(x, y, z, top);
    bool found = false;
    float highest = -10000;
    for (int index = 0; index < 3; ++index) {
        const Vec3 offset = boss.rampOffset_[index];
        bool hit = false;
        const float height = boss.rampCollision_[index].groundHeight(x - offset.x, y - offset.y,
                                                                    z - offset.z, &hit) + offset.z;
        if (hit && height > highest) {
            highest = height;
            found = true;
        }
    }
    if (found) *top = highest;
    return found;
}

void BossBattle::loadAssets() {
    if (failed_ || ready_) return;
    if (!budgetChecked_) {
        budgetChecked_ = true;
        if (!modelBudgets(cashbot_)) {
            failed_ = true;
            TT_LOG("boss: model header / aggregate memory budget rejected");
            return;
        }
    }
    if (modelTicket_) {
        const LoadStatus status = AssetLoader::poll(modelTicket_);
        if (status == LoadStatus::Pending) return;
        if (status != LoadStatus::Ready || !AssetLoader::takeModel(modelTicket_, models_[modelIndex_])) {
            AssetLoader::cancel(modelTicket_);
            modelTicket_ = 0;
            failed_ = true;
            return;
        }
        modelTicket_ = 0;
        ++modelIndex_;
    }
    if (modelIndex_ == (cashbot_ ? 18 : 12)) {
        if (cashbot_) {
            if (!cfo_.loadGoons(models_[15])) {
                failed_ = cfo_.failed();
                return;
            }
            char path[128];
            std::snprintf(path, sizeof(path), "%scfo_arena.ttc", kDirectory);
            ready_ = collision_.load(path);
            failed_ = !ready_;
            solids_.floor = rampFloor;
            solids_.push = pushBoss;
            solids_.user = this;
            return;
        }
        char path[128];
        std::snprintf(path, sizeof(path), "%svp_arena.ttc", kDirectory);
        ready_ = collision_.load(path);
        for (int index = 0; index < 3; ++index) {
            std::snprintf(path, sizeof(path), "%svp_ramp_%c.ttc", kDirectory, 'a' + index);
            ready_ = rampCollision_[index].load(path) && ready_;
        }
        std::snprintf(path, sizeof(path), "%svp_scene.bin", kDirectory);
        FILE* scene = std::fopen(path, "rb");
        char magic[4] = {};
        uint16_t version = 0;
        uint16_t count = 0;
        bool valid = scene && std::fread(magic, 1, 4, scene) == 4 && !std::memcmp(magic, "BSC1", 4) &&
                     std::fread(&version, 2, 1, scene) == 1 && version == 1 &&
                     std::fread(&count, 2, 1, scene) == 1 && count == 3 &&
                     std::fread(rampRetract_, sizeof(Vec3), 3, scene) == 3 &&
                     std::fread(&doorHinge_, sizeof(Vec3), 1, scene) == 1;
        if (scene) std::fclose(scene);
        for (const Vec3& offset : rampRetract_) {
            valid &= std::isfinite(offset.x) && std::isfinite(offset.y) && std::isfinite(offset.z);
        }
        valid &= std::isfinite(doorHinge_.x) && std::isfinite(doorHinge_.y) && std::isfinite(doorHinge_.z);
        ready_ &= valid;
        solids_.floor = rampFloor;
        solids_.push = pushBoss;
        solids_.user = this;
        failed_ = !ready_;
        uint32_t resident = 0;
        for (const render::Model& model : models_) resident += model.linearBytes();
        if (resident + 2 * kClipLimit > kGeometryLimit) {
            failed_ = true;
            ready_ = false;
        }
        TT_LOG("boss: assets %lu linear bytes, collision %lu heap, clip pair cap %lu", 
               static_cast<unsigned long>(resident), static_cast<unsigned long>(collision_.heapBytes()),
               static_cast<unsigned long>(2 * kClipLimit));
        return;
    }
    char path[128];
    modelPath(path, sizeof(path), cashbot_, modelIndex_);
    modelTicket_ = AssetLoader::requestModel(path);
}

void BossBattle::selectClip(const char* name) {
    if (!ready_ || pendingClip_[0] || !std::strcmp(currentClip_, name) || !std::strcmp(failedClip_, name)) return;
    if (!clipBudget(name, models_, cashbot_)) {
        std::snprintf(failedClip_, sizeof(failedClip_), "%s", name);
        TT_LOG("boss: clip missing, malformed or over budget: %s", name);
        return;
    }
    std::snprintf(pendingClip_, sizeof(pendingClip_), "%s", name);
}

void BossBattle::publishClip() {
    if (!pendingClip_[0]) return;
    bool allReady = true;
    for (int part = 0; part < kParts; ++part) {
        if (!clipTickets_[part]) {
            char path[160];
            std::snprintf(path, sizeof(path), "%s%s_%s.tta", kDirectory, modelName(cashbot_, part), pendingClip_);
            clipTickets_[part] = AssetLoader::requestAnim(path);
        }
        const LoadStatus status = AssetLoader::poll(clipTickets_[part]);
        if (status == LoadStatus::Failed) {
            std::snprintf(failedClip_, sizeof(failedClip_), "%s", pendingClip_);
            for (LoadTicket& ticket : clipTickets_) {
                if (ticket) AssetLoader::cancel(ticket);
                ticket = 0;
            }
            pendingClip_[0] = 0;
            return;
        }
        allReady &= status == LoadStatus::Ready;
    }
    if (!allReady) return;
    for (int part = 0; part < kParts; ++part) {
        animations_[part].unload();
        AssetLoader::takeAnim(clipTickets_[part], animations_[part]);
        clipTickets_[part] = 0;
    }
    std::snprintf(currentClip_, sizeof(currentClip_), "%s", pendingClip_);
    pendingClip_[0] = 0;
    clipTime_ = 0;
}

void BossBattle::updateFight(BossFrame& frame, const net::BossState& state, uint32_t nowMs) {
    const BossPhase phase = bossPhase(state.phase);
    const bool piePhase = phase == BossPhase::Pies || phase == BossPhase::FinalPie;
    bossPosition_ = cashbot_ ? kCfoBoss : vpPosition(net::currentBossDamage(state, nowMs));
    bossSolid_ = piePhase;
    const float elapsed = (nowMs - state.phaseMs) / 1000.0f;
    for (int index = 0; !cashbot_ && index < 3; ++index) {
        // DistributedSellbotBoss ramp FSM: local X=18 retracted, X=0 extended.
        bool retracted = phase == BossPhase::Round || index == 2;
        if (phase == BossPhase::Pies || phase == BossPhase::FinalPie ||
            phase == BossPhase::Victory || phase == BossPhase::Reward || phase == BossPhase::Epilogue ||
            !std::strcmp(state.phase, "PrepareBattleThree")) retracted = index != 2;
        if (!std::strcmp(state.phase, "RollToBattleTwo") || !std::strcmp(state.phase, "PrepareBattleTwo")) {
            retracted = index == 2 || (index == 0 ? state.arenaSide != 0 : state.arenaSide == 0);
        }
        const float direction = retracted ? 1.0f : -1.0f;
        rampFraction_[index] = std::clamp(rampFraction_[index] + direction * frame.dt / 2, 0.0f, 1.0f);
        const Vec3 vector = rampRetract_[index];
        rampOffset_[index] = {vector.x * rampFraction_[index], vector.y * rampFraction_[index], vector.z * rampFraction_[index]};
    }
    if (std::strcmp(phase_, state.phase)) {
        std::snprintf(phase_, sizeof(phase_), "%s", state.phase);
        for (bool& acknowledged : haveAcknowledged_) acknowledged = false;
        charging_ = false;
        for (Projectile& pie : pies_) pie.active = false;
        for (Projectile& gear : gears_) gear.active = false;
        if (piePhase) {
            if (cashbot_) frame.player.reset(105, -285, .1f, 208);
            else frame.player.reset(-20, 42, 18.1f, -90);
        }
        TT_LOG("boss: phase %s", phase_);
    }
    const uint32_t npcId = cashbot_ ? 146392 : state.cagedNpc;
    if (npcId && rescuedNpc_ != npcId) {
        rescuedNpc_ = npcId;
        char path[128];
        std::snprintf(path, sizeof(path), "%s%s_npcs.bin", kDirectory, cashbot_ ? "cfo" : "vp");
        FILE* file = std::fopen(path, "rb");
        char magic[4] = {};
        uint16_t version = 0;
        uint16_t count = 0;
        bool valid = file && std::fread(magic, 1, 4, file) == 4 && std::fread(&version, 2, 1, file) == 1 &&
                     std::fread(&count, 2, 1, file) == 1 && !std::memcmp(magic, "BND1", 4) && version == 1 && count <= 64;
        for (int index = 0; valid && index < count; ++index) {
            uint32_t npc = 0;
            uint8_t dna[15];
            valid = std::fread(&npc, 4, 1, file) == 1 && std::fread(dna, 1, 15, file) == 15;
            if (valid && npc == rescuedNpc_) {
                ToonDna parsed;
                if (parseNetString(dna, 15, &parsed)) frame.cache.acquire(parsed, ToonLod::Low, &rescuedLook_);
                break;
            }
        }
        if (file) std::fclose(file);
        rescuedToon_.setLook(&rescuedLook_, &frame.cache.fallback());
    }
    rescuedToon_.play(phase == BossPhase::Epilogue ? kAnimWave : kAnimNeutral, 1);
    rescuedToon_.update(frame.dt);
    if (!entered_) {
        frame.client.sendSetParent(net::kParentRender);
        entered_ = frame.client.sendBossEnter();
    }
    for (int index = 0; index < state.numBarriers; ++index) {
        const net::BossBarrier& barrier = state.barriers[index];
        float delay = bossBarrierDelay(barrier.name);
        if (cashbot_ && !std::strcmp(barrier.name, "Victory")) delay = 15;
        if (!barrier.waitingForUs || delay < 0 || elapsed < delay || std::strcmp(barrier.name, state.phase)) continue;
        if (!cashbot_ && !std::strcmp(barrier.name, "Victory") &&
            (std::strcmp(currentClip_, "Fb_fall") || clipTime_ < 5.8f)) continue;
        if (haveAcknowledged_[index] && acknowledged_[index] == barrier.context) continue;
        if (frame.client.sendBossBarrier(barrier.context)) {
            acknowledged_[index] = barrier.context;
            haveAcknowledged_[index] = true;
        }
    }
    if (phase == BossPhase::Round && !frame.battle.active()) {
        const bool sideA = participant(state.sideA, state.numA, frame.client.avatarId());
        const uint32_t battle = sideA ? state.battleA : state.battleB;
        if (battle && frame.client.findBattle(battle)) {
            frame.battle.requestJoin(frame.client, battle, frame.player.x(), frame.player.y(),
                                     frame.player.z(), frame.player.headingDeg(), nowMs);
        }
    }
    if (const net::Battle* battle = frame.client.localBattle()) {
        const int32_t elapsedMs = static_cast<int32_t>(nowMs - battle->stateMs);
        for (int index = 0; index < battle->numBossBarriers; ++index) {
            const net::BossBarrier& barrier = battle->bossBarriers[index];
            if (elapsedMs < 3500 || std::strcmp(barrier.name, "ReservesJoining") || !barrier.waitingForUs) continue;
            if (reserveAcknowledged_ && reserveBattle_ == battle->doId && reserveContext_ == barrier.context) continue;
            if (frame.client.sendBossBattleBarrier(battle->doId, barrier.context)) {
                reserveBattle_ = battle->doId;
                reserveContext_ = barrier.context;
                reserveAcknowledged_ = true;
            }
        }
    }
    frame.battle.setAutoPlay(frame.test.active());
    frame.battle.update(frame.client, nowMs, frame.dt, {SuitActors::poseThunk, &frame.suits});
    frame.suits.update(frame.dt, nowMs, &collision_);
    for (int index = 0; index < frame.battle.numSuitPoses(); ++index) {
        const BattleActorPose& pose = frame.battle.suitPose(index);
        frame.suits.overridePose(pose.doId, pose.x, pose.y, pose.z + pose.hop, pose.h,
                                 pose.hidden, pose.flash, pose.anim, pose.clipArg, pose.clipStartMs);
    }
    const net::OwnStats& stats = frame.client.ownStats();
    const ui::BattleGags gags{stats.inventory, stats.experience, stats.trackAccess};
    frame.battle.fillView(frame.client, nowMs, &battleView_);
    frame.ui.setBattle(frame.battle.active() ? &battleView_ : nullptr, gags);
    if (frame.battle.active()) {
        frame.ui.handleInput(frame.input.down, frame.input.held, frame.touch, frame.uiState);
        BattleAction action;
        while (frame.ui.battle().popAction(action)) frame.battle.perform(frame.client, action, nowMs);
    }
    Input movement = frame.input;
    if (!piePhase || frame.battle.active() || (cashbot_ && cfo_.controlling(frame.client))) movement = {};
    frame.player.setCollision(&collision_);
    frame.player.setDynamic(&solids_);
    frame.player.setPlatform(nullptr, 0, 0, 0, 0);
    frame.player.update(movement, frame.dt);
    BattleActorPose toonPose;
    if (frame.battle.localPose(&toonPose)) {
        frame.player.reset(toonPose.x, toonPose.y, toonPose.z + toonPose.hop, toonPose.h);
        if (toonPose.anim >= kBattleAnimClip0 && toonPose.anim < kBattleAnimClip0 + kNumBattleClips) {
            const ToonAnim animation = toonAnimByName(kBattleClipNames[toonPose.anim - kBattleAnimClip0]);
            frame.toon.playAction(animation, animation == kAnimVictory ? ClipMode::Loop : ClipMode::Once);
        } else {
            frame.toon.play(static_cast<ToonAnim>(toonPose.anim), 1);
        }
    } else if (!frame.toon.actionActive()) {
        float rate = 1;
        const ToonAnim animation = frame.player.animState(&rate);
        frame.toon.play(animation, rate);
    }
    if (piePhase && !cashbot_) {
        pitch_ = std::clamp(pitch_ + ((frame.input.held & KEY_DUP) ? 40 : 0) * frame.dt -
                            ((frame.input.held & KEY_DDOWN) ? 40 : 0) * frame.dt, -30.0f, 60.0f);
        const bool held = (frame.input.held & KEY_B) != 0;
        bool touchThrow = (frame.input.down & KEY_TOUCH) && frame.touch.py > 120 && frame.touch.py < 210;
        const bool scripted = frame.test.active() && !std::strncmp(frame.test.label(), "boss: ", 6);
        if (scripted && !frame.client.bossPies()) frame.player.reset(0, 73, 18.1f, 0);
        if (scripted && frame.client.bossPies()) {
            const Vec3 boss = vpPosition(net::currentBossDamage(state, nowMs));
            const bool forward = state.strafeSide != 0;
            frame.player.reset(boss.x, boss.y + (forward ? -30 : 30), boss.z + 0.1f, forward ? 0 : 180);
            // Aim a genuine ballistic shot at the hatch, or the body while stunned.
            // Test positioning is artificial; the normal swept hit test and AI still
            // decide every hit. No autotest code writes a boss phase or damage value.
            const float height = (state.attack == net::kBossDizzyNow || phase == BossPhase::FinalPie ? 14.0f : 2.0f) - 2.34f;
            constexpr float speedSquared = 10000 + 16.087f * 16.087f;
            constexpr float gravity = 32.174f;
            const float discriminant = speedSquared * speedSquared - gravity * (gravity * 900 + 2 * height * speedSquared);
            if (discriminant > 0) {
                pitch_ = std::atan((speedSquared - std::sqrt(discriminant)) / (gravity * 30)) / kDegrees -
                         std::atan(16.087f / 100) / kDegrees;
            }
            touchThrow |= nowMs - autoPieMs_ > 400 && (state.attack == net::kBossDizzyNow || vpHatchOpen(state, nowMs) ||
                                                       phase == BossPhase::FinalPie);
            if (touchThrow) autoPieMs_ = nowMs;
        }
        if (held && !charging_) {
            charging_ = true;
            charge_ = 0;
        }
        if (charging_) charge_ += frame.dt;
        if ((charging_ && !held) || touchThrow) {
            for (Projectile& pie : pies_) {
                if (pie.active) continue;
                if (!frame.client.spendBossPie()) break;
                pie = {};
                pie.active = true;
                pie.origin = {frame.player.x(), frame.player.y(), frame.player.z() + 2.24f};
                pie.previous = pie.origin;
                pie.age = -16.0f / 24.0f; // Toon.getTossPieInterval release frame.
                pie.heading = frame.player.headingDeg();
                pie.pitch = pitch_;
                pie.power = scripted ? 0 : charging_ ? piePower(charge_) : 0.5f;
                frame.toon.playAction(kAnimThrow, ClipMode::Once, 1, 2, -1, true);
                break;
            }
            charging_ = false;
        }
        // DistributedSellbotBoss cage trigger is around the lowered cage at y=78.
        const float cageX = frame.player.x();
        const float cageY = frame.player.y() - 78;
        if (!frame.client.bossPies() && cageX * cageX + cageY * cageY < 100 &&
            nowMs - cageMs_ > 2000 && frame.client.sendVpTouchCage()) cageMs_ = nowMs;
        const Vec3 boss = vpPosition(net::currentBossDamage(state, nowMs));
        const float dx = frame.player.x() - boss.x;
        const float dy = frame.player.y() - boss.y;
        const float distance = std::sqrt(dx * dx + dy * dy);
        const bool near = distance < 35;
        if (near != near_ && frame.client.sendBossNear(near)) near_ = near;
        for (Projectile& pie : pies_) {
            if (!pie.active) continue;
            pie.age += frame.dt;
            if (pie.age < 0) continue;
            const Vec3 current = piePosition(pie.origin, pie.heading, pie.pitch, pie.power, pie.age);
            const VpPieHit hit = vpPieHit(pie.previous, current, boss, vpHatchOpen(state, nowMs));
            if (hit != VpPieHit::None) {
                if (phase == BossPhase::FinalPie) frame.client.sendVpFinalPie();
                else if (hit == VpPieHit::Insides) frame.client.sendVpHitInsides();
                else frame.client.sendVpHit();
                pie.active = false;
            }
            if (pie.active) {
                for (int index = 0; index < state.numToons; ++index) {
                    const uint32_t toon = state.toons[index];
                    if (toon == frame.client.avatarId()) continue;
                    float x = 0;
                    float y = 0;
                    float z = 0;
                    if (!frame.client.avatarPos(toon, &x, &y, &z)) continue;
                    if (segmentBox(pie.previous, current, {x - 1.4f, y - 1.4f, z}, {x + 1.4f, y + 1.4f, z + 5})) {
                        frame.client.sendVpHitToon(toon);
                        pie.active = false;
                        break;
                    }
                }
            }
            if (pie.age > 3 || current.z < -30) pie.active = false;
            pie.previous = current;
        }
        if (attackRevision_ != state.attackRevision) {
            attackRevision_ = state.attackRevision;
            attackHit_ = false;
            if (state.attack == net::kBossDirectedAttack || state.attack == net::kBossSlowDirectedAttack) {
                Projectile& gear = gears_[0];
                gear = {};
                gear.active = true;
                gear.origin = {boss.x, boss.y, boss.z + 12};
                gear.previous = gear.origin;
                float targetX = frame.player.x();
                float targetY = frame.player.y();
                float targetZ = frame.player.z();
                frame.client.avatarPos(state.attackToon, &targetX, &targetY, &targetZ);
                const float targetDx = targetX - gear.origin.x;
                const float targetDy = targetY - gear.origin.y;
                const float targetDistance = std::sqrt(targetDx * targetDx + targetDy * targetDy);
                gear.heading = std::atan2(-targetDx, targetDy) / kDegrees;
                gear.pitch = std::atan2(targetZ + 2 - gear.origin.z, targetDistance) / kDegrees;
                gear.power = 0;
            }
        }
        if (strafeRevision_ != state.strafeRevision) {
            strafeRevision_ = state.strafeRevision;
            const float damage = std::clamp(net::currentBossDamage(state, nowMs) / 100, 0.0f, 1.0f);
            const int count = static_cast<int>(4 + 6 * damage + .5f);
            for (int index = 0; index < count; ++index) {
                Projectile& gear = gears_[index + 1];
                gear = {};
                gear.active = true;
                gear.origin = {boss.x, boss.y + (state.strafeSide ? -7 : 7), boss.z + 3};
                gear.previous = gear.origin;
                gear.heading = (state.strafeSide ? 180.0f : 0.0f) +
                               (float(index) / (count - 1) - .5f) * 60 * (state.strafeDirection ? -1 : 1);
                gear.age = -0.7f - index * (5 - 4 * damage) / count;
            }
        }
        for (int index = 0; index < kProjectiles; ++index) {
            Projectile& gear = gears_[index];
            if (!gear.active) continue;
            gear.age += frame.dt;
            if (gear.age < 0) continue;
            const float speed = index ? 50 : 40;
            Vec3 current{gear.origin.x - std::sin(gear.heading * kDegrees) * speed * gear.age,
                         gear.origin.y + std::cos(gear.heading * kDegrees) * speed * gear.age,
                         gear.origin.z + std::sin(gear.pitch * kDegrees) * speed * gear.age};
            if (segmentBox(gear.previous, current, {frame.player.x() - 1.4f, frame.player.y() - 1.4f, frame.player.z()},
                           {frame.player.x() + 1.4f, frame.player.y() + 1.4f, frame.player.z() + 5})) {
                const float length = std::max(1.0f, distance);
                frame.client.sendBossZap(frame.player.x(), frame.player.y(), frame.player.z(),
                                         frame.player.headingDeg(), dx / length, dy / length, index ? net::kBossStrafeAttack : net::kBossDirectedAttack);
                gear.active = false;
            }
            gear.previous = current;
            if (gear.age > (index ? 1.0f : 3.0f)) gear.active = false;
        }
        const float attackAge = (nowMs - state.attackMs) / 1000.0f;
        bool hit = state.attack == net::kBossAreaAttack && attackAge >= 1.21f && attackAge < 1.5f && !frame.player.airborne();
        hit |= (state.attack == net::kBossSwatLeft || state.attack == net::kBossSwatRight) && attackAge >= 1 && attackAge < 1.3f && distance < 18;
        hit |= (state.attack == net::kBossFrontAttack || state.attack == net::kBossRecoverDizzy) && attackAge >= 1.9f && attackAge < 2.2f && distance < 22;
        if (hit && !attackHit_) {
            attackHit_ = frame.client.sendBossZap(frame.player.x(), frame.player.y(), frame.player.z(),
                frame.player.headingDeg(), dx / std::max(1.0f, distance), dy / std::max(1.0f, distance), state.attack);
        }
        if (distance < 13 && nowMs - contactMs_ > 1500) {
            frame.client.sendBossZap(frame.player.x(), frame.player.y(), frame.player.z(),
                                     frame.player.headingDeg(), dx / std::max(1.0f, distance),
                                     dy / std::max(1.0f, distance), net::kBossElectricFence);
            contactMs_ = nowMs;
        }
    }
    if (cashbot_) cfo_.update(frame, nowMs);
    if (nowMs - moveMs_ >= 200) {
        frame.client.sendPosHpr(frame.player.x(), frame.player.y(), frame.player.z(), frame.player.headingDeg(), 0, 0);
        frame.client.sendAnimState(frame.player.airborne() ? "jumpAirborne" : frame.player.moving() ? "run" : "Happy", 1);
        frame.client.setOwnWorldPos(frame.player.x(), frame.player.y(), frame.player.z());
        moveMs_ = nowMs;
    }
    frame.toon.update(frame.dt);
    frame.remotes.update(frame.dt);
    frame.effects.update(frame.dt, nowMs, &frame.battle, frame.toon.look());
}

void BossBattle::draw(BossFrame& frame, const net::BossState& state, uint32_t nowMs) {
    const BossPhase phase = bossPhase(state.phase);
    const float damage = net::currentBossDamage(state, nowMs);
    Vec3 position = vpPosition(damage);
    float heading = 180;
    if (phase == BossPhase::Arrival || phase == BossPhase::Intro ||
        (phase == BossPhase::Round && !std::strcmp(state.phase, "BattleOne"))) {
        position = {0, -35, 0};
        heading = -90;
    } else if (phase == BossPhase::Round || phase == BossPhase::Transition) {
        position = {0, 60, 18};
        heading = -90;
        if (!std::strcmp(state.phase, "RollToBattleTwo")) {
            const float fraction = std::clamp((nowMs - state.phaseMs) / 8000.0f, 0.0f, 1.0f);
            position = {0, -35 + 95 * fraction, 18 * fraction};
        }
    }
    if (cashbot_) {
        position = phase == BossPhase::Round || phase == BossPhase::Intro || phase == BossPhase::Arrival ?
                   Vec3{120, -230, 0} : kCfoBoss;
        heading = phase == BossPhase::Round ? 90 : 180;
        if (phase == BossPhase::Victory) {
            const float age = (nowMs - state.phaseMs) / 1000.0f;
            if (age >= 5.5f) position = {120, -280 + 30 * std::clamp((age - 5.5f) / 3, 0.0f, 1.0f), 0};
        }
    }
    C3D_FVec eye = frame.player.cameraEye();
    C3D_FVec target = frame.player.cameraTarget();
    if (frame.battle.inBattle()) {
        frame.battle.cameraEye(&eye.x, &eye.y, &eye.z);
        frame.battle.cameraTarget(&target.x, &target.y, &target.z);
    } else if (phase != BossPhase::Pies && phase != BossPhase::FinalPie) {
        eye = FVec3_New(position.x + 35, position.y - 55, position.z + 24);
        target = FVec3_New(position.x, position.y, position.z + 15);
    }
    if (cashbot_ && cfo_.controlling(frame.client) && phase == BossPhase::Pies) {
        eye = FVec3_New(120, -375, 52);
        target = FVec3_New(120, -315, 8);
    }
    gfxSet3D(false); // One rendering pass preserves the Old 3DS boss budget.
    frame.stereoActive = false;
    frame.renderer.resetStats();
    frame.renderer.beginWorld(frame.renderer.topLeft(), eye, target, 0, 0);
    C3D_Mtx identity = transform({0, 0, 0});
    if (ready_) {
        frame.renderer.drawModel(models_[4], identity);
        if (!cashbot_) {
            // Upstream cageHeights: 100,81,63,44,25,18; the baked pivot is z=100.
            const float cageHeight = vpCageHeight(state, nowMs);
            C3D_Mtx cage = transform({0, 0, cageHeight - 100});
            frame.renderer.drawModel(models_[5], cage);
            // CageOpenMovie rotates the door around its original hinge, then releases the NPC.
            C3D_Mtx door = cage;
            if (phase == BossPhase::Epilogue) {
                door = transform({doorHinge_.x, doorHinge_.y, doorHinge_.z + cageHeight - 100});
                Mtx_RotateX(&door, vpCageDoorAngle(state, nowMs) * kDegrees, true);
                Mtx_Translate(&door, -doorHinge_.x, -doorHinge_.y, -doorHinge_.z, true);
            }
            frame.renderer.drawModel(models_[6], door);
            C3D_Mtx rescued = transform({0, 78, cageHeight}, 180);
            rescuedToon_.draw(frame.renderer, rescued);
            for (int index = 0; index < 3; ++index) {
                C3D_Mtx ramp = transform(rampOffset_[index]);
                frame.renderer.drawModel(models_[7 + index], ramp);
            }
        } else {
            cfo_.draw(frame, models_, nowMs);
            if (phase == BossPhase::Intro || phase == BossPhase::Transition || phase == BossPhase::Epilogue) {
                C3D_Mtx resistance = transform(phase == BossPhase::Intro ? Vec3{94, -220, 0} : Vec3{120, -315, 0}, 180);
                rescuedToon_.draw(frame.renderer, resistance);
            }
        }
        C3D_Mtx world = transform(position, heading);
        C3D_Mtx torso = world;
        C3D_Mtx head = world;
        for (int part = 0; part < kParts && phase != BossPhase::Reward && phase != BossPhase::Epilogue &&
             !(cashbot_ && phase == BossPhase::Victory && nowMs - state.phaseMs > 9500); ++part) {
            C3D_Mtx partWorld = part == 1 ? torso : part == 2 ? head : world;
            render::Anim& animation = animations_[part];
            if (animation.loaded()) {
                u32 first = 0;
                u32 second = 0;
                float blend = 0;
                const bool looping = std::strstr(currentClip_, "neutral") || !std::strcmp(currentClip_, "Ff_speech") ||
                                     !std::strcmp(currentClip_, "wave");
                if (looping) {
                    render::sampleClip(animation, clipTime_, &first, &second, &blend);
                } else {
                    const float sample = std::min(clipTime_ * animation.fps(), float(animation.numFrames() - 1));
                    first = static_cast<u32>(sample);
                    second = std::min(first + 1, animation.numFrames() - 1);
                    blend = sample - first;
                }
                frame.renderer.drawMorph(models_[part], animation, first, second, blend, partWorld);
                if (animation.hasAttach() && part < 2) {
                    C3D_Mtx attachment;
                    animation.attach(first, second, blend, &attachment);
                    Mtx_Multiply(part == 0 ? &torso : &head, &partWorld, &attachment);
                }
            }
        }
        for (const Projectile& pie : pies_) {
            if (!pie.active || pie.age < 0) continue;
            C3D_Mtx matrix = transform(pie.previous, pie.heading, 1.5f);
            frame.renderer.drawModel(models_[11], matrix);
        }
        for (const Projectile& gear : gears_) {
            if (!gear.active || gear.age < 0) continue;
            C3D_Mtx matrix = transform(gear.previous, gear.age * 720, 0.1f);
            frame.renderer.drawModel(models_[10], matrix);
        }
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
    context.infoTitle = failed_ ? "VP assets missing / over budget" : !ready_ ? "Loading the VP arena" : state.phase;
    std::snprintf(status_, sizeof(status_), "VP %.0f / 100  %s  Laff %d", damage,
                  state.attack == net::kBossDizzyNow ? "STUNNED" : vpHatchOpen(state, nowMs) ? "HATCH OPEN" : "", frame.client.ownStats().hp);
    std::snprintf(details_, sizeof(details_), "Pies %u  Power %.0f%%  Aim %.0f", unsigned(frame.client.bossPies()),
                  charging_ ? piePower(charge_) * 100 : 0, pitch_);
    if (phase == BossPhase::Reward || phase == BossPhase::Epilogue) {
        const char* name = "Toon rescued";
        for (const ui::SosCardDef& card : ui::kSosCards) {
            if (card.npcId == state.cagedNpc) name = card.name;
        }
        std::snprintf(details_, sizeof(details_), "%s%s", name, state.promoted ? " - promotion earned" : "");
    }
    context.infoLines[0] = status_;
    context.infoLines[1] = details_;
    context.infoLines[2] = phase == BossPhase::Epilogue ? "SOS awarded by server. A: return to HQ" :
                           failed_ ? "Copy data/boss from the bake. B: return" :
                           "A jump; B hold/release pie; D-pad aim; cage refills";
    if (cashbot_) {
        context.infoTitle = failed_ ? "CFO assets missing / over budget" : !ready_ ? "Loading CFO arena and goons" : state.phase;
        std::snprintf(status_, sizeof(status_), "CFO %.0f / 500  Laff %d", damage, frame.client.ownStats().hp);
        std::snprintf(details_, sizeof(details_), "Magnet %s   %s", cfo_.magnetOn() ? "ON" : "OFF",
                      state.attack == net::kBossDizzy ? "STUNNED: throw safes" : "Jump on goons; knock helmet off");
        context.infoLines[2] = failed_ ? "Copy data/boss and level/props goons. B: return" :
            "A: crane/jump  Pad: arm  B: magnet/drop  X: exit";
        if (phase == BossPhase::Epilogue) {
            std::snprintf(details_, sizeof(details_), "Resistance reward %u", unsigned(state.rewardId));
            context.infoLines[2] = rewardConfirmed_ ? "Reward requested. A: return to Cashbot HQ" : "A: confirm and apply Resistance reward";
        }
    }
    context.heapUsedKB = mallinfo().uordblks / 1024;
    context.linearFreeKB = linearSpaceFree() / 1024;
    context.vramFreeKB = vramSpaceFree() / 1024;
    frame.ui.draw(frame.bottom, frame.uiState, context);
    C2D_DrawRectSolid(12, 196, .8f, std::clamp(damage / (cashbot_ ? 500 : 100), 0.0f, 1.0f) * 296, 5,
                      C2D_Color32(220, 80, 40, 255));
}

bool BossBattle::frame(BossFrame& frame) {
    const uint32_t nowMs = net::plat::nowMs();
    if (!active_) {
        board(frame, nowMs);
        const net::BossState* snapshot = frame.client.boss();
        if (!snapshot || (snapshot->kind != net::BossKind::Sellbot && snapshot->kind != net::BossKind::Cashbot)) return false;
        const net::BossState& state = *snapshot;
        cashbot_ = state.kind == net::BossKind::Cashbot;
        cfo_.reset();
        rewardConfirmed_ = false;
        active_ = true;
        failed_ = false;
        entered_ = false;
        near_ = false;
        home_ = cashbot_ ? 12000 : frame.place.zoneId() == 11100 ? 11100 : 11000;
        bossId_ = state.doId;
        phase_[0] = 0;
        attackRevision_ = 0;
        strafeRevision_ = 0;
        reserveAcknowledged_ = false;
        frame.battle.reset();
        frame.ui.setBattle(nullptr, {});
        frame.remotes.clear();
        frame.suits.setZone(frame.client.zoneId());
        C3D_FrameSync();
        frame.place.unload(); // The lobby and arena never consume the zone pools together.
        if (cashbot_) frame.player.reset(88, -214, 0, 210);
        else frame.player.reset(-10, -80, 0, 0);
        frame.ui.book().close();
    }
    frame.client.update(nowMs);
    net::Event event;
    while (frame.client.pollEvent(event)) {
        frame.battle.handleEvent(event, frame.client, nowMs);
        frame.remotes.handleEvent(event, frame.client);
        frame.suits.handleEvent(event, frame.client, nowMs);
    }
    const net::BossState state = frame.client.boss() ? *frame.client.boss() : net::BossState{};
    const BossPhase phase = bossPhase(state.phase);
    if (!state.active && frame.client.inZone() && Place::available(frame.client.zoneId())) {
        home_ = frame.client.zoneId(); // a new login may already have selected a safe place
    }
    const bool endingInput = (frame.input.down & KEY_A) ||
        (frame.test.active() && !std::strcmp(frame.test.label(), cashbot_ ? "cfo: end" : "boss: end"));
    const bool couldLeave = !cashbot_ || rewardConfirmed_;
    if (cashbot_ && phase == BossPhase::Epilogue && endingInput && !rewardConfirmed_) {
        rewardConfirmed_ = frame.client.sendCfoReward();
    }
    if (!state.active || state.doId != bossId_ || frame.client.ownStats().hp <= 0 ||
        (failed_ && (frame.input.down & KEY_B)) ||
        (phase == BossPhase::Epilogue && nowMs - state.phaseMs >= 1500 && endingInput && couldLeave)) {
        leave(frame);
        return false;
    }
    if (!ready_ && !failed_) {
        // WaitForToons is only five seconds. Register and acknowledge arrival while
        // the SD loader fills the arena, then use Elevator's longer movie window.
        if (!entered_) entered_ = frame.client.sendBossEnter();
        if (nowMs - state.phaseMs >= 500 && !std::strcmp(state.phase, "WaitForToons")) {
            for (int index = 0; index < state.numBarriers; ++index) {
                const net::BossBarrier& barrier = state.barriers[index];
                if (barrier.waitingForUs && !std::strcmp(barrier.name, "WaitForToons") &&
                    (!haveAcknowledged_[index] || acknowledged_[index] != barrier.context) &&
                    frame.client.sendBossBarrier(barrier.context)) {
                    acknowledged_[index] = barrier.context;
                    haveAcknowledged_[index] = true;
                }
            }
        }
    }
    if (ready_) updateFight(frame, state, nowMs);
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
    loadAssets();
    selectClip(cashbot_ ? cfoClip(state, nowMs) : vpClip(state, nowMs));
    publishClip();
    clipTime_ += frame.dt;
    draw(frame, state, nowMs);
    frame.commandUsage = C3D_GetCmdBufUsage();
    C3D_FrameEnd(0);
    audio::update();
    return true;
}

} // namespace tt::world
