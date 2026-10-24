#include "world/level_games.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "game/panda_rng.h"

#ifdef __3DS__
#include "core/log.h"
#include "world/level.h"
#endif

namespace tt::world {

namespace games_logic {

void shuffleMazeRotations(uint32_t seed, int outRotations[4]) {
    int rotations[8] = {0, 0, 90, 90, 180, 180, 270, 270};
    game::RandomNumGen rng(seed);
    rng.shuffle(rotations, 8);
    // Python list.pop() takes from the end of the array:
    outRotations[0] = rotations[7];
    outRotations[1] = rotations[6];
    outRotations[2] = rotations[5];
    outRotations[3] = rotations[4];
}

int calcMoleSchedule(uint32_t seed, float gameDuration, int numMoles,
                     MolePopup* outSchedule, int maxSchedule) {
    if (!outSchedule || maxSchedule <= 0 || numMoles <= 0) return 0;

    game::RandomNumGen randOb(seed);
    game::RandomNumGen rngEligible(seed);

    float curMoveUpTime = 1.0f;
    float curMoveDownTime = 1.0f;
    float curStayUpTime = 7.0f;
    float curTimeBetweenPopup = 1.5f;
    float curTime = 3.0f;

    uint8_t eligibleMoles[16];
    const int totalMoles = numMoles > 16 ? 16 : numMoles;
    for (int i = 0; i < totalMoles; ++i) eligibleMoles[i] = static_cast<uint8_t>(i);
    int numEligible = totalMoles;
    rngEligible.shuffle(eligibleMoles, numEligible);

    uint8_t usedMoles[16];
    int numUsed = 0;

    int count = 0;
    float endingTime = 0.0f;

    while (endingTime < gameDuration && count < maxSchedule) {
        if (numEligible == 0) {
            for (int i = 0; i < numUsed; ++i) eligibleMoles[i] = usedMoles[i];
            numEligible = numUsed;
            numUsed = 0;
            rngEligible.shuffle(eligibleMoles, numEligible);
        }
        if (numEligible == 0) break;

        const uint8_t moleIndex = eligibleMoles[0];
        for (int i = 0; i < numEligible - 1; ++i) eligibleMoles[i] = eligibleMoles[i + 1];
        --numEligible;
        usedMoles[numUsed++] = moleIndex;

        // Ratio: 3 normal moles to 1 cog bomb (choice([0, 0, 0, 1]))
        const uint8_t moleType = (randOb.choice(4) == 3) ? 1 : 0;

        MolePopup& pop = outSchedule[count++];
        pop.start = curTime;
        pop.hole = moleIndex;
        pop.upTime = curMoveUpTime;
        pop.stayTime = curStayUpTime;
        pop.downTime = curMoveDownTime;
        pop.type = moleType;

        curTime += curTimeBetweenPopup;
        curMoveUpTime = std::max(0.5f, curMoveUpTime * 0.95f);
        curStayUpTime = std::max(3.0f, curStayUpTime * 0.95f);
        curMoveDownTime = std::max(0.5f, curMoveDownTime * 0.95f);
        curTimeBetweenPopup = std::max(0.25f, curTimeBetweenPopup * 0.95f);
        endingTime = curTime + curMoveUpTime + curStayUpTime + curMoveDownTime;
    }

    // Upstream pops the trailing element (self.schedule.pop())
    if (count > 1) --count;
    return count;
}

} // namespace games_logic

#ifdef __3DS__

namespace {

constexpr const char* kPropsDir = "sdmc:/3ds/toontown3ds/data/level/props/";
constexpr const char* kAudioDir = "sdmc:/3ds/toontown3ds/data/audio/";

// Math helpers
inline void toMtxLocal(const float w[12], C3D_Mtx* m) {
    Mtx_Identity(m);
    for (int r = 0; r < 3; ++r) {
        m->r[r].x = w[0 + r], m->r[r].y = w[3 + r], m->r[r].z = w[6 + r], m->r[r].w = w[9 + r];
    }
}

inline void toLocalLocal(const float w[12], float x, float y, float z, float out[3]) {
    const float d[3] = {x - w[9], y - w[10], z - w[11]};
    for (int r = 0; r < 3; ++r) {
        const float* axis = w + r * 3;
        const float len2 = axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2];
        out[r] = len2 > 1e-12f ? (d[0] * axis[0] + d[1] * axis[1] + d[2] * axis[2]) / len2 : 0.0f;
    }
}

inline void fromLocalLocal(const float w[12], const float p[3], float out[3]) {
    for (int c = 0; c < 3; ++c) out[c] = p[0] * w[c] + p[1] * w[3 + c] + p[2] * w[6 + c] + w[9 + c];
}

} // namespace

void LevelGames::loadProps() {
    if (propsLoaded_) return;
    propsLoaded_ = true;

    char path[128];

    // Models:
    snprintf(path, sizeof(path), "%scrate_wood.ttm", kPropsDir);
    crateWood_.load(path);
    snprintf(path, sizeof(path), "%scrate_cbwood.ttm", kPropsDir);
    crateCbWood_.load(path);
    snprintf(path, sizeof(path), "%sgrid_floor.ttm", kPropsDir);
    gridFloor_.load(path);

    snprintf(path, sizeof(path), "%smole_hole.ttm", kPropsDir);
    moleHole_.load(path);
    snprintf(path, sizeof(path), "%smole_norm.ttm", kPropsDir);
    moleNorm_.load(path);
    snprintf(path, sizeof(path), "%smole_cog.ttm", kPropsDir);
    moleCog_.load(path);
    snprintf(path, sizeof(path), "%smole_hit.ttm", kPropsDir);
    moleHit_.load(path);

    snprintf(path, sizeof(path), "%sgolf_hole.ttm", kPropsDir);
    golfHole_.load(path);
    snprintf(path, sizeof(path), "%sgolf_ball.ttm", kPropsDir);
    golfBall_.load(path);
    snprintf(path, sizeof(path), "%sgoon_cone.ttm", kPropsDir);
    goonCone_.load(path);

    // Crate box
    snprintf(path, sizeof(path), "%scrate.box", kPropsDir);
    if (FILE* f = fopen(path, "r")) {
        float b[6];
        if (fscanf(f, "%f %f %f %f %f %f", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) == 6) {
            memcpy(crateBox_, b, sizeof(crateBox_));
        }
        fclose(f);
    }

    // Audio SFX:
    static const char* const kSfxNames[kSfxCount] = {
        "LB_capacitor_discharge_3",
        "LB_sparks_1",
        "LB_camera_shutter_2",
        "LB_laser_beam_hum_2",
        "CHQ_FACT_crate_effort",
        "CHQ_FACT_crate_sliding",
        "Mole_Surprise",
        "Mole_Stomp",
        "MG_Tag_C",
        "CHQ_GOON_tractor_beam_alarmed"
    };

    for (int k = 0; k < kSfxCount; ++k) {
        snprintf(path, sizeof(path), "%s%s.tsnd", kAudioDir, kSfxNames[k]);
        sfx_[k] = audio::loadSfx(path);
    }
}

void LevelGames::shutdown() {
    propsLoaded_ = false;
    crateWood_.unload();
    crateCbWood_.unload();
    gridFloor_.unload();
    moleHole_.unload();
    moleNorm_.unload();
    moleCog_.unload();
    moleHit_.unload();
    golfHole_.unload();
    golfBall_.unload();
    goonCone_.unload();
    for (auto& s : slots_) s = SlotState{};
}

void LevelGames::playAt(SfxIndex idx, const float pos[3], float vol) {
    (void)vol;
    if (idx >= kSfxCount || !sfx_[idx]) return;
    const float dx = pos[0] - toonX_, dy = pos[1] - toonY_, dz = pos[2] - toonZ_;
    const float dist2 = dx * dx + dy * dy + dz * dz;
    if (dist2 > 120.0f * 120.0f) return;
    audio::playSfx(sfx_[idx]);
}

void LevelGames::addSolids(Level& level) {
    if (!client_) return;

    for (int i = 0; i < net::kMaxLevelEntities; ++i) {
        const net::LevelEntityObj& d = client_->levelEntitySlot(i);
        if (!d.active || d.classId != dc::DistributedCrate::kClassId) continue;
        const LevelEntity* e = level.specOf(d.levelDoId, d.entId);
        if (!e || !level.shown(e->room)) continue;

        SlotState& slot = slots_[i];
        const float scale = level.spec_.number(*e, "scale", 1.0f);

        // Compute crate's dynamic world transform with current lerp position:
        float crateWorld[12];
        memcpy(crateWorld, e->world, sizeof(crateWorld));
        // Add dynamic offset in entity space
        float localOffset[3] = {slot.curPos[0], slot.curPos[1], slot.curPos[2]};
        float worldPos[3];
        fromLocalLocal(e->world, localOffset, worldPos);
        crateWorld[9] = worldPos[0];
        crateWorld[10] = worldPos[1];
        crateWorld[11] = worldPos[2];

        float lo[3] = {crateBox_[0] * scale, crateBox_[1] * scale, crateBox_[2] * scale};
        float hi[3] = {crateBox_[3] * scale, crateBox_[4] * scale, crateBox_[5] * scale};

        level.addSolid(crateWorld, lo, hi, true, true);

        // If moving, carry toon standing on top:
        if (slot.moveProgress < 1.0f && level.numSolids_ > 0) {
            Level::Solid& last = level.solids_[level.numSolids_ - 1];
            float deltaPos[3] = {slot.curPos[0] - slot.prevPos[0], slot.curPos[1] - slot.prevPos[1], slot.curPos[2] - slot.prevPos[2]};
            last.motion[0] = deltaPos[0];
            last.motion[1] = deltaPos[1];
            last.motion[2] = deltaPos[2];
        }
    }
}

void LevelGames::update(net::Client& client, Level& level, float toonX, float toonY, float toonZ, float dt, uint32_t nowMs) {
    client_ = &client;
    level_ = &level;
    toonX_ = toonX;
    toonY_ = toonY;
    toonZ_ = toonZ;
    nowMs_ = nowMs;

    addSolids(level);

    updateLasers(client, level, toonX, toonY, toonZ, nowMs);
    updateCameras(client, level, toonX, toonY, toonZ, dt, nowMs);
    updateCrates(client, level, toonX, toonY, toonZ, dt, nowMs);
    updateMazes(client, level, toonX, toonY, toonZ, nowMs);
    updateMoles(client, level, toonX, toonY, toonZ, dt, nowMs);
    updateGolf(client, level, toonX, toonY, toonZ, nowMs);
}

void LevelGames::updateLasers(net::Client& client, Level& level, float toonX, float toonY, float toonZ, uint32_t nowMs) {
    for (int i = 0; i < net::kMaxLevelEntities; ++i) {
        const net::LevelEntityObj& d = client.levelEntitySlot(i);
        if (!d.active || d.classId != dc::DistributedLaserField::kClassId) continue;
        const LevelEntity* e = level.specOf(d.levelDoId, d.entId);
        if (!e || !level.shown(e->room)) continue;

        SlotState& slot = slots_[i];
        const float scaleX = level.spec_.number(*e, "gridScaleX", d.gridNumX > 0 ? float(d.gridNumX) : 8.0f);
        const float scaleY = level.spec_.number(*e, "gridScaleY", d.gridNumY > 0 ? float(d.gridNumY) : 8.0f);
        const uint8_t numX = d.gridNumX > 0 ? d.gridNumX : 8;
        const uint8_t numY = d.gridNumY > 0 ? d.gridNumY : 8;

        float local[3];
        toLocalLocal(e->world, toonX, toonY, toonZ, local);

        int8_t tileX = -1, tileY = -1;
        const bool inside = games_logic::calcLaserTile(local[0], local[1], scaleX, scaleY, numX, numY, &tileX, &tileY);

        if (inside && d.activeLF) {
            if (nowMs - slot.lastLaserHumMs > 3000) {
                playAt(kSfxLBLaserHum, e->world + 9, 0.4f);
                slot.lastLaserHumMs = nowMs;
            }

            if (tileX != slot.lastTileX || tileY != slot.lastTileY || !slot.toonInLaser) {
                const int8_t oldX = slot.lastTileX;
                const int8_t oldY = slot.lastTileY;
                slot.lastTileX = tileX;
                slot.lastTileY = tileY;
                slot.toonInLaser = true;

                client.sendLaserHit(d.doId, tileX, tileY, oldX, oldY);

                // Hazard symbol check in field data
                const int symbolIdx = tileX + tileY * numX;
                if (symbolIdx >= 0 && symbolIdx < d.fieldDataLen) {
                    const uint8_t sym = d.fieldData[symbolIdx];
                    if (games_logic::isLaserHazard(sym)) {
                        playAt(kSfxLBSparks, e->world + 9, 0.9f);
                        client.sendLaserTrapFire(d.doId);
                    }
                }
            }
        } else {
            if (slot.toonInLaser) {
                slot.toonInLaser = false;
                slot.lastTileX = -1;
                slot.lastTileY = -1;
            }
        }

        // On puzzle solved
        if (d.success == 1 && slot.lastTileX != -2) {
            playAt(kSfxLBCapacitor, e->world + 9, 0.9f);
            slot.lastTileX = -2; // mark played
        }
    }
}

void LevelGames::updateCameras(net::Client& client, Level& level, float toonX, float toonY, float toonZ, float dt, uint32_t nowMs) {
    for (int i = 0; i < net::kMaxLevelEntities; ++i) {
        const net::LevelEntityObj& d = client.levelEntitySlot(i);
        if (!d.active || d.classId != dc::DistributedSecurityCamera::kClassId) continue;
        const LevelEntity* e = level.specOf(d.levelDoId, d.entId);
        if (!e || !level.shown(e->room)) continue;

        SlotState& slot = slots_[i];
        const float radius = level.spec_.number(*e, "radius", 5.0f);
        const float accel = level.spec_.number(*e, "accel", 0.5f);
        const float maxVel = level.spec_.number(*e, "maxVel", 1.0f);

        // Smooth camera track target pursuit:
        float targetX = 0.0f, targetY = 0.0f;
        // Pursue toon if in room, or circle
        float localToon[3];
        toLocalLocal(e->world, toonX, toonY, toonZ, localToon);
        targetX = localToon[0];
        targetY = localToon[1];

        const float distX = targetX - slot.trackX;
        const float distY = targetY - slot.trackY;
        const float trigDist = std::sqrt(distX * distX + distY * distY);
        const float totalDist = std::abs(distX) + std::abs(distY);
        const float propX = std::abs(distX) / (totalDist + 0.01f);
        const float propY = std::abs(distY) / (totalDist + 0.01f);
        const float dirX = distX != 0.0f ? (distX / std::abs(distX)) : 0.0f;
        const float dirY = distY != 0.0f ? (distY / std::abs(distY)) : 0.0f;

        if (trigDist < radius * 0.5f + 1.0f) {
            float deccel = 1.0f - (dt * 7.0f);
            if (deccel < 0.0f) deccel = 0.0f;
            slot.vX *= deccel;
            slot.vY *= deccel;
        } else {
            slot.vX += dirX * accel * propX;
            slot.vY += dirY * accel * propY;
        }

        slot.vX = std::clamp(slot.vX, -maxVel, maxVel);
        slot.vY = std::clamp(slot.vY, -maxVel, maxVel);
        slot.trackX += slot.vX * dt;
        slot.trackY += slot.vY * dt;

        // Detection: distance from toon to spotlight center
        const float dx = localToon[0] - slot.trackX;
        const float dy = localToon[1] - slot.trackY;
        const float distToSpot = std::sqrt(dx * dx + dy * dy);

        if (distToSpot < radius && (nowMs - slot.lastCameraAlarmMs > 2000)) {
            slot.lastCameraAlarmMs = nowMs;
            playAt(kSfxCameraAlarm, e->world + 9, 0.9f);
            playAt(kSfxLBCameraShutter, e->world + 9, 0.8f);
            client.sendCameraTrapFire(d.doId);
        }
    }
}

void LevelGames::updateCrates(net::Client& client, Level& level, float toonX, float toonY, float toonZ, float dt, uint32_t nowMs) {
    (void)dt;
    for (int i = 0; i < net::kMaxLevelEntities; ++i) {
        const net::LevelEntityObj& d = client.levelEntitySlot(i);
        if (!d.active || d.classId != dc::DistributedCrate::kClassId) continue;
        const LevelEntity* e = level.specOf(d.levelDoId, d.entId);
        if (!e || !level.shown(e->room)) continue;

        SlotState& slot = slots_[i];
        slot.prevPos[0] = slot.curPos[0];
        slot.prevPos[1] = slot.curPos[1];
        slot.prevPos[2] = slot.curPos[2];

        // Moving lerp (setMoveTo)
        if (d.moving && d.moveStartMs > 0) {
            const float elapsed = int32_t(nowMs - d.moveStartMs) / 1000.0f;
            constexpr float kDuration = 1.5f;
            games_logic::calcCrateLerp(d.movePos, d.movePos + 3, elapsed, kDuration, slot.curPos);
            slot.moveProgress = elapsed / kDuration;

            if (elapsed >= kDuration && slot.pushing) {
                slot.pushing = false;
                client.sendCrateDone(d.doId);
            }
        }

        // Push detection: toon walking into crate
        float localToon[3];
        toLocalLocal(e->world, toonX, toonY, toonZ, localToon);
        const float scale = level.spec_.number(*e, "scale", 1.0f);
        const float w = 2.8f * scale;

        // Is toon near crate face?
        const float dx = localToon[0] - slot.curPos[0];
        const float dy = localToon[1] - slot.curPos[1];
        const float dz = localToon[2] - slot.curPos[2];

        if (dz >= -0.5f && dz <= 5.5f * scale) {
            const int side = games_logic::calcCratePushSide(dx, dy);
            const float distFromCenter = std::sqrt(dx * dx + dy * dy);
            if (side >= 0 && distFromCenter < w + 2.0f && !slot.pushing) {
                if (nowMs - slot.pushStartMs > 1000) {
                    slot.pushStartMs = nowMs;
                    slot.pushSide = side;
                    slot.pushing = true;
                    playAt(kSfxCrateEffort, e->world + 9, 0.8f);
                    client.sendCratePush(d.doId, static_cast<uint8_t>(side));
                }
            }
        }
    }
}

void LevelGames::updateMazes(net::Client& client, Level& level, float toonX, float toonY, float toonZ, uint32_t nowMs) {
    (void)nowMs;
    for (int i = 0; i < net::kMaxLevelEntities; ++i) {
        const net::LevelEntityObj& d = client.levelEntitySlot(i);
        if (!d.active || d.classId != dc::DistributedMaze::kClassId) continue;
        const LevelEntity* e = level.specOf(d.levelDoId, d.entId);
        if (!e || !level.shown(e->room)) continue;

        SlotState& slot = slots_[i];
        float localToon[3];
        toLocalLocal(e->world, toonX, toonY, toonZ, localToon);

        // Entrance trigger: sphere radius 8.0 at entrance
        const float distFromOrigin = std::sqrt(localToon[0] * localToon[0] + localToon[1] * localToon[1]);
        if (distFromOrigin < 8.0f && !slot.mazeTriggered) {
            slot.mazeTriggered = true;
            client.sendMazeTriggered(d.doId);
        }

        // Finish trigger: sphere radius 15.0 at exit (local Y ~ 100)
        const float distFromExit = std::sqrt(localToon[0] * localToon[0] + (localToon[1] - 90.0f) * (localToon[1] - 90.0f));
        if (distFromExit < 15.0f && slot.mazeTriggered && !slot.mazeFinished) {
            slot.mazeFinished = true;
            client.sendMazeFinished(d.doId);
        }
    }
}

void LevelGames::updateMoles(net::Client& client, Level& level, float toonX, float toonY, float toonZ, float dt, uint32_t nowMs) {
    (void)dt;
    for (int i = 0; i < net::kMaxLevelEntities; ++i) {
        const net::LevelEntityObj& d = client.levelEntitySlot(i);
        if (!d.active || d.classId != dc::DistributedMoleField::kClassId) continue;
        const LevelEntity* e = level.specOf(d.levelDoId, d.entId);
        if (!e || !level.shown(e->room)) continue;

        SlotState& slot = slots_[i];

        // Initialize schedule if game started and not yet scheduled
        if (!slot.scheduled && d.gameStartMs > 0) {
            slot.scheduled = true;
            slot.moleStartMs = d.gameStartMs;
            const float duration = d.gameDuration > 0 ? float(d.gameDuration) : 60.0f;
            slot.scheduleCount = games_logic::calcMoleSchedule(d.entId * d.levelDoId, duration, 16, slot.schedule, 64);
        }

        const float elapsed = d.gameStartMs > 0 ? (int32_t(nowMs - d.gameStartMs) / 1000.0f) : 0.0f;

        // Update mole elevations across 16 holes:
        for (int h = 0; h < 16; ++h) {
            slot.holeElevation[h] = 0.0f;
        }

        for (int s = 0; s < slot.scheduleCount; ++s) {
            const games_logic::MolePopup& pop = slot.schedule[s];
            if (pop.hole < 16) {
                const float elev = games_logic::calcMoleElevation(elapsed, pop.start, pop.upTime, pop.stayTime, pop.downTime);
                if (elev > slot.holeElevation[pop.hole]) {
                    slot.holeElevation[pop.hole] = elev;
                    slot.holeType[pop.hole] = pop.type;
                }
            }
        }

        // Check toon stepping on raised moles:
        float localToon[3];
        toLocalLocal(e->world, toonX, toonY, toonZ, localToon);

        constexpr float kSpacing = 8.0f;
        for (int h = 0; h < 16; ++h) {
            const int col = h % 4;
            const int row = h / 4;
            const float hx = (col - 1.5f) * kSpacing;
            const float hy = (row - 1.5f) * kSpacing;
            const float dx = localToon[0] - hx;
            const float dy = localToon[1] - hy;
            const float dist2 = dx * dx + dy * dy;

            if (dist2 < 2.5f * 2.5f && slot.holeElevation[h] > 0.4f && !slot.holeWhacked[h]) {
                slot.holeWhacked[h] = true;
                slot.whackedTimeMs[h] = nowMs;

                if (slot.holeType[h] == 0) {
                    playAt(kSfxMoleStomp, e->world + 9, 0.9f);
                    client.sendMoleWhacked(d.doId, static_cast<uint8_t>(h), 1);
                } else {
                    playAt(kSfxMoleSurprise, e->world + 9, 0.9f);
                    client.sendMoleBomb(d.doId, static_cast<uint8_t>(h), 1, nowMs);
                }
            }
            if (slot.holeElevation[h] <= 0.05f) {
                slot.holeWhacked[h] = false;
            }
        }
    }
}

void LevelGames::updateGolf(net::Client& client, Level& level, float toonX, float toonY, float toonZ, uint32_t nowMs) {
    (void)nowMs;
    for (int i = 0; i < net::kMaxLevelEntities; ++i) {
        const net::LevelEntityObj& d = client.levelEntitySlot(i);
        if (!d.active || d.classId != dc::DistributedGolfGreenGame::kClassId) continue;
        const LevelEntity* e = level.specOf(d.levelDoId, d.entId);
        if (!e || !level.shown(e->room)) continue;

        SlotState& slot = slots_[i];
        float localToon[3];
        toLocalLocal(e->world, toonX, toonY, toonZ, localToon);

        // Check 4 tee spots:
        int nearestSpot = -1;
        float minDist = 1e30f;
        for (int s = 0; s < 4; ++s) {
            float spotPos[3];
            games_logic::calcGolfSpot(s, spotPos);
            const float dx = localToon[0] - spotPos[0];
            const float dy = localToon[1] - spotPos[1];
            const float dist = std::sqrt(dx * dx + dy * dy);
            if (dist < minDist) {
                minDist = dist;
                nearestSpot = s;
            }
        }

        if (minDist < 3.0f && !slot.joinedGolf) {
            slot.joinedGolf = true;
            slot.currentSpot = nearestSpot;
            client.sendGolfJoin(d.doId);
        } else if (minDist > 6.0f && slot.joinedGolf) {
            slot.joinedGolf = false;
            slot.currentSpot = -1;
            client.sendGolfLeave(d.doId);
        }
    }
}

void LevelGames::draw(render::Renderer& renderer, const C3D_FVec& eye) const {
    (void)eye;
    if (!client_ || !level_) return;

    for (int i = 0; i < net::kMaxLevelEntities; ++i) {
        const net::LevelEntityObj& d = client_->levelEntitySlot(i);
        if (!d.active) continue;
        const LevelEntity* e = level_->specOf(d.levelDoId, d.entId);
        if (!e || !level_->shown(e->room)) continue;

        const SlotState& slot = slots_[i];

        // 1. Laser fields
        if (d.classId == dc::DistributedLaserField::kClassId) {
            if (d.activeLF && gridFloor_.loaded()) {
                C3D_Mtx m;
                toMtxLocal(e->world, &m);
                renderer.drawModel(const_cast<render::Model&>(gridFloor_), m);
            }
        }
        // 2. Security cameras
        else if (d.classId == dc::DistributedSecurityCamera::kClassId) {
            if (goonCone_.loaded()) {
                C3D_Mtx m;
                toMtxLocal(e->world, &m);
                Mtx_Translate(&m, slot.trackX, slot.trackY, 0.0f, true);
                Mtx_Scale(&m, 0.5f, 0.5f, 1.0f);
                renderer.drawModel(const_cast<render::Model&>(goonCone_), m);
            }
        }
        // 3. Crates
        else if (d.classId == dc::DistributedCrate::kClassId) {
            const bool mintStyle = level_->spec_.integer(*e, "modelType", 0) == 1;
            render::Model& mdl = mintStyle && crateCbWood_.loaded() ? const_cast<render::Model&>(crateCbWood_)
                                                                    : const_cast<render::Model&>(crateWood_);
            if (mdl.loaded()) {
                C3D_Mtx m;
                toMtxLocal(e->world, &m);
                Mtx_Translate(&m, slot.curPos[0], slot.curPos[1], slot.curPos[2], true);
                const float scale = level_->spec_.number(*e, "scale", 1.0f);
                Mtx_Scale(&m, scale, scale, scale * (d.crushed ? 0.1f : 1.0f));
                renderer.drawModel(mdl, m);
            }
        }
        // 4. Mole fields
        else if (d.classId == dc::DistributedMoleField::kClassId) {
            if (moleHole_.loaded()) {
                constexpr float kSpacing = 8.0f;
                for (int h = 0; h < 16; ++h) {
                    const int col = h % 4;
                    const int row = h / 4;
                    const float hx = (col - 1.5f) * kSpacing;
                    const float hy = (row - 1.5f) * kSpacing;

                    C3D_Mtx m;
                    toMtxLocal(e->world, &m);
                    Mtx_Translate(&m, hx, hy, 0.0f, true);
                    renderer.drawModel(const_cast<render::Model&>(moleHole_), m);

                    const float elev = slot.holeElevation[h];
                    if (elev > 0.05f) {
                        render::Model* moleMdl = &const_cast<render::Model&>(moleNorm_);
                        if (slot.holeWhacked[h] && moleHit_.loaded()) {
                            moleMdl = &const_cast<render::Model&>(moleHit_);
                        } else if (slot.holeType[h] == 1 && moleCog_.loaded()) {
                            moleMdl = &const_cast<render::Model&>(moleCog_);
                        }
                        if (moleMdl->loaded()) {
                            C3D_Mtx mm = m;
                            Mtx_Translate(&mm, 0.0f, 0.0f, elev * 2.2f, true);
                            renderer.drawModel(*moleMdl, mm);
                        }
                    }
                }
            }
        }
        // 5. Golf greens
        else if (d.classId == dc::DistributedGolfGreenGame::kClassId) {
            if (golfHole_.loaded()) {
                for (int s = 0; s < 4; ++s) {
                    float spot[3];
                    games_logic::calcGolfSpot(s, spot);
                    C3D_Mtx m;
                    toMtxLocal(e->world, &m);
                    Mtx_Translate(&m, spot[0], spot[1], spot[2], true);
                    renderer.drawModel(const_cast<render::Model&>(golfHole_), m);
                }
            }
        }
    }
}

bool LevelGames::nearestLaserField(const Level& level, float x, float y, float z, float* outX, float* outY, float* outZ) const {
    return level.nearestEntity(dc::DistributedLaserField::kClassId, x, y, z, outX, outY, outZ);
}

bool LevelGames::nearestCamera(const Level& level, float x, float y, float z, float* outX, float* outY, float* outZ) const {
    return level.nearestEntity(dc::DistributedSecurityCamera::kClassId, x, y, z, outX, outY, outZ);
}

bool LevelGames::nearestCrate(const Level& level, float x, float y, float z, float* outX, float* outY, float* outZ) const {
    return level.nearestEntity(dc::DistributedCrate::kClassId, x, y, z, outX, outY, outZ);
}

bool LevelGames::nearestMaze(const Level& level, float x, float y, float z, float* outX, float* outY, float* outZ) const {
    return level.nearestEntity(dc::DistributedMaze::kClassId, x, y, z, outX, outY, outZ);
}

bool LevelGames::nearestMoleField(const Level& level, float x, float y, float z, float* outX, float* outY, float* outZ) const {
    return level.nearestEntity(dc::DistributedMoleField::kClassId, x, y, z, outX, outY, outZ);
}

bool LevelGames::nearestGolfGreen(const Level& level, float x, float y, float z, float* outX, float* outY, float* outZ) const {
    return level.nearestEntity(dc::DistributedGolfGreenGame::kClassId, x, y, z, outX, outY, outZ);
}

#endif // __3DS__

} // namespace tt::world
