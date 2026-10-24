#include "world/cfo_mechanics.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "world/boss_battle.h"

namespace tt::world {
namespace {
constexpr float kDegrees = 0.01745329252f;
constexpr const char* kGoonClips[] = {"walk", "collapse", "recovery"};
constexpr uint32_t kGoonClipBudget = 1536 * 1024;
float length(Vec3 value) {
    return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}
Vec3 subtract(Vec3 left, Vec3 right) {
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}
C3D_Mtx matrix(Vec3 position, float heading = 0, float scale = 1) {
    C3D_Mtx result;
    Mtx_Identity(&result);
    Mtx_Translate(&result, position.x, position.y, position.z, true);
    Mtx_RotateZ(&result, heading * kDegrees, true);
    Mtx_Scale(&result, scale, scale, scale);
    return result;
}
}

void CfoMechanics::reset() {
    for (Crane& crane : cranes_) {
        crane = {};
        crane.cable.reset();
    }
    for (Object& object : objects_) object = {};
    magnetOn_ = false;
    requestMs_ = 0;
    attackRevision_ = 0;
    attackActive_ = false;
}

void CfoMechanics::release() {
    if (goonTicket_) AssetLoader::cancel(goonTicket_);
    goonTicket_ = 0;
    for (render::Anim& clip : goonClips_) clip.unload();
    goonIndex_ = 0;
    goonFailed_ = false;
    reset();
}

bool CfoMechanics::loadGoons(const render::Model& model) {
    if (goonFailed_) return false;
    if (goonIndex_ == 3) return true;
    if (goonTicket_) {
        const LoadStatus status = AssetLoader::poll(goonTicket_);
        if (status == LoadStatus::Pending) return false;
        if (status != LoadStatus::Ready || !AssetLoader::takeAnim(goonTicket_, goonClips_[goonIndex_])) {
            AssetLoader::cancel(goonTicket_);
            goonTicket_ = 0;
            goonFailed_ = true;
            return false;
        }
        goonTicket_ = 0;
        ++goonIndex_;
        if (goonIndex_ == 3) return true;
    }
    char path[160];
    std::snprintf(path, sizeof(path), "sdmc:/3ds/toontown3ds/data/level/props/goon_pg_full_%s.tta", kGoonClips[goonIndex_]);
    // Validate all metadata before allocation, including the aggregate reserve.
    FILE* file = std::fopen(path, "rb");
    uint32_t header[16] = {};
    const bool read = file && std::fread(header, 1, 64, file) == 64;
    long size = 0;
    if (file) {
        if (!std::fseek(file, 0, SEEK_END)) size = std::ftell(file);
        std::fclose(file);
    }
    uint16_t shorts[32];
    float floats[16];
    std::memcpy(shorts, header, 64);
    std::memcpy(floats, header, 64);
    uint64_t total = uint64_t(header[2]) * shorts[6] * 8;
    for (int index = 0; index < goonIndex_; ++index) total += goonClips_[index].linearBytes();
    if (!read || std::memcmp(header, "TTA1", 4) || shorts[2] != 1 || shorts[3] ||
        header[2] != model.numVertices() || !std::isfinite(floats[4]) || floats[4] <= 0 || floats[4] > 24 || !shorts[6] || shorts[6] > 120 ||
        header[12] != header[2] * 8 || header[11] < 64 ||
        uint64_t(header[11]) + uint64_t(shorts[6]) * header[12] > uint64_t(std::max(0L, size)) ||
        total > kGoonClipBudget) {
        goonFailed_ = true;
        return false;
    }
    goonTicket_ = AssetLoader::requestAnim(path);
    return false;
}

bool CfoMechanics::controlling(const net::Client& client) const {
    for (int index = 0; index < net::kMaxCfoChildren; ++index) {
        const net::CfoChild& child = client.cfoChildSlot(index);
        if (child.active && child.kind == net::CfoKind::Crane && child.owner == client.avatarId() && child.state == 'C') return true;
    }
    return false;
}

void CfoMechanics::push(float* x, float* y, float z, float radius) const {
    for (const Object& object : objects_) {
        if (!object.doId || object.kind != net::CfoKind::Goon || object.state == 'O' ||
            object.state == 'G' || object.state == 'D' || z >= object.position.z + 3.8f * object.scale - .6f) continue;
        const float dx = *x - object.position.x;
        const float dy = *y - object.position.y;
        const float distance = std::sqrt(dx * dx + dy * dy);
        const float limit = object.scale + radius;
        if (distance < limit && std::fabs(z - object.position.z) < 4 * object.scale) {
            *x = object.position.x + (distance > .001f ? dx / distance : 1) * limit;
            *y = object.position.y + (distance > .001f ? dy / distance : 0) * limit;
        }
    }
}

bool CfoMechanics::floor(float x, float y, float z, float* height) const {
    bool found = false;
    float highest = -10000;
    for (const Object& object : objects_) {
        if (!object.doId || object.kind != net::CfoKind::Goon ||
            (object.state != 'W' && object.state != 'B' && object.state != 'a' && object.state != 'b')) continue;
        const float dx = x - object.position.x;
        const float dy = y - object.position.y;
        const float top = object.position.z + 3.8f * object.scale;
        if (dx * dx + dy * dy < (object.scale + .5f) * (object.scale + .5f) && z >= top - 1 && top > highest) {
            highest = top;
            found = true;
        }
    }
    if (found) *height = highest;
    return found;
}

void CfoMechanics::update(BossFrame& frame, uint32_t nowMs) {
    using Action = net::Client::CfoAction;
    const net::BossState* boss = frame.client.boss();
    if (!boss || std::strcmp(boss->phase, "BattleThree")) {
        magnetOn_ = false;
        attackActive_ = false;
        return;
    }
    const bool scripted = frame.test.active() && !std::strcmp(frame.test.label(), "cfo: cranes");
    const Vec3 toon{frame.player.x(), frame.player.y(), frame.player.z()};
    const float dt = std::clamp(frame.dt, .0001f, 1.0f / 15);
    for (int slot = 0; slot < net::kMaxCfoChildren; ++slot) {
        const net::CfoChild& child = frame.client.cfoChildSlot(slot);
        if (!child.active || child.kind != net::CfoKind::Crane || child.bossId != boss->doId) continue;
        Crane& crane = cranes_[child.index];
        if (crane.doId != child.doId || crane.owner != child.owner) {
            crane = {};
            crane.doId = child.doId;
            crane.owner = child.owner;
            crane.cable.reset();
            crane.previousMagnet = crane.cable.magnet(child.index);
            magnetOn_ = false;
            if (child.owner == frame.client.avatarId()) frame.client.sendCfoClear(child.doId);
        }
        if (child.owner == frame.client.avatarId() && child.state == 'C') {
            if (frame.input.down & KEY_X) frame.client.sendCfoControl(child.doId, false);
            if (frame.input.down & KEY_B) magnetOn_ = !magnetOn_;
            bool safe = false;
            bool holding = false;
            for (int objectSlot = 0; objectSlot < net::kMaxCfoChildren; ++objectSlot) {
                const net::CfoChild& object = frame.client.cfoChildSlot(objectSlot);
                if (object.active && object.state == 'G' && object.crane == child.doId) {
                    holding = true;
                    safe = object.kind == net::CfoKind::Safe;
                }
            }
            float slide = frame.input.pad.dy / 156.0f;
            float turn = -frame.input.pad.dx / 156.0f;
            if (scripted) {
                // Exercise ordinary crane movement and collision; never inject hits.
                const float time = (nowMs - boss->phaseMs) / 1000.0f;
                slide = std::sin(time * 1.4f) > 0 ? 1 : -1;
                turn = std::sin(time * .4f);
                magnetOn_ = !holding || slide < 0;
            }
            crane.cable.update(slide, turn, dt, safe);
            const Vec3 magnet = crane.cable.magnet(child.index);
            const Vec3 delta = subtract(magnet, crane.previousMagnet);
            crane.velocity = {delta.x / dt, delta.y / dt, delta.z / dt};
            crane.previousMagnet = magnet;
            if (nowMs - crane.sendMs >= 200) {
                float links[3][3];
                for (int link = 0; link < 3; ++link) {
                    links[link][0] = crane.cable.links[link].x;
                    links[link][1] = crane.cable.links[link].y;
                    links[link][2] = crane.cable.links[link].z;
                }
                frame.client.sendCfoCable(child.doId, crane.sequence++, crane.cable.y, crane.cable.heading, links);
                crane.sendMs = nowMs;
            }
        } else if (child.state == 'F' && nowMs - requestMs_ > 1500) {
            const Vec3 controls = cfoToWorld({0, -5, 0}, kCfoCranes[child.index]);
            if ((length(subtract(toon, controls)) < 6 && (frame.input.down & KEY_A)) ||
                (scripted && child.index == 0)) {
                if (frame.client.sendCfoControl(child.doId, true)) {
                    frame.player.reset(controls.x, controls.y, controls.z, kCfoCranes[child.index].heading);
                    requestMs_ = nowMs;
                }
            }
        }
        if (child.owner != frame.client.avatarId()) {
            if (child.cableMs != crane.cableMs) {
                crane.cableMs = child.cableMs;
                crane.remoteY = child.armY;
                crane.remoteH = child.armH > 180 ? child.armH - 360 : child.armH;
                for (int link = 0; link < 3; ++link) {
                    crane.remoteLinks[link] = {child.links[link][0], child.links[link][1], child.links[link][2]};
                }
            }
            const float blend = std::min(1.0f, dt / .2f);
            crane.cable.y += (crane.remoteY - crane.cable.y) * blend;
            crane.cable.heading += (crane.remoteH - crane.cable.heading) * blend;
            if (child.cableMs) {
                for (int link = 0; link < 3; ++link) {
                    Vec3& point = crane.cable.links[link];
                    const Vec3 target = crane.remoteLinks[link];
                    point = {point.x + (target.x - point.x) * blend,
                             point.y + (target.y - point.y) * blend,
                             point.z + (target.z - point.z) * blend};
                }
            }
        }
    }
    for (int slot = 0; slot < net::kMaxCfoChildren; ++slot) {
        const net::CfoChild& child = frame.client.cfoChildSlot(slot);
        Object& object = objects_[slot];
        if (!child.active || child.kind == net::CfoKind::Crane ||
            (child.kind != net::CfoKind::Treasure && child.bossId != boss->doId)) {
            object = {};
            continue;
        }
        if (object.doId != child.doId) {
            object = {};
            object.doId = child.doId;
            object.position = {child.x, child.y, child.z};
        }
        object.kind = child.kind;
        object.scale = child.scale;
        if (object.state != child.state) {
            if (child.state == 'G') object.grabbedStunned = object.state == 'S';
            object.state = child.state;
            object.hit = false;
            object.floorHit = false;
            if (child.state == 'D') {
                for (int index = 0; index < 4; ++index) {
                    if (cranes_[index].doId == child.crane) {
                        const Vec3 velocity = cranes_[index].velocity;
                        object.velocity = {velocity.x * 1.5f, velocity.y * 1.5f, velocity.z * 1.5f};
                    }
                }
            }
        }
        if (child.kind == net::CfoKind::Safe && child.state == 'I') {
            object.position = kCfoSafes[child.index].position;
            object.heading = kCfoSafes[child.index].heading;
        } else if (child.state == 'G') {
            if (child.crane == boss->doId) {
                object.position = {120, -315, 26}; // Helmet, server-owned safe.
            } else {
                for (int index = 0; index < 4; ++index) {
                    Crane& crane = cranes_[index];
                    if (crane.doId != child.crane) continue;
                    const Vec3 magnet = crane.cable.magnet(index);
                    object.position = {magnet.x, magnet.y, magnet.z - 4 -
                        (child.kind == net::CfoKind::Safe ? 8.2f : (object.grabbedStunned ? 2.2f : 3.6f) * child.scale)};
                    if (child.owner == frame.client.avatarId() &&
                        (!magnetOn_ || (child.kind == net::CfoKind::Goon && !object.grabbedStunned && nowMs - child.stateMs > 2000)) &&
                        nowMs - object.actionMs > 500) {
                        if (frame.client.sendCfoObject(child.doId, Action::Drop)) object.actionMs = nowMs;
                    }
                }
            }
        } else if (child.owner == frame.client.avatarId() && (child.state == 'D' || child.state == 's')) {
            const Vec3 previous = object.position;
            object.velocity.z -= kCfoGravity * dt;
            object.position = {previous.x + object.velocity.x * dt, previous.y + object.velocity.y * dt,
                               previous.z + object.velocity.z * dt};
            // Swept CFO body, not a timer-driven damage request.
            if (!object.hit && child.state == 'D' && child.crane != boss->doId &&
                segmentBox(previous, object.position, {108, -327, 0}, {132, -303, 32})) {
                float heading = 0;
                for (int index = 0; index < 4; ++index) {
                    if (cranes_[index].doId == child.crane) heading = kCfoCranes[index].heading;
                }
                const float impact = cfoImpact(object.velocity, heading);
                if (impact >= (child.kind == net::CfoKind::Goon ? .1f : 0)) {
                    object.hit = frame.client.sendCfoObject(child.doId, Action::Hit, impact);
                }
            }
            if (child.kind == net::CfoKind::Safe && child.state == 'D') {
                for (const Object& goon : objects_) {
                    if (!goon.doId || goon.kind != net::CfoKind::Goon || goon.state == 'O' || goon.state == 'G') continue;
                    const float radius = 2 * goon.scale;
                    if (segmentBox(previous, object.position,
                        {goon.position.x - radius, goon.position.y - radius, goon.position.z},
                        {goon.position.x + radius, goon.position.y + radius, goon.position.z + 5 * goon.scale})) {
                        frame.client.sendCfoObject(goon.doId, Action::Destroy);
                    }
                }
            }
            if (object.position.z <= 0) {
                object.position.z = 0;
                object.velocity.z = 0;
                object.velocity.x *= std::max(0.0f, 1 - dt * 3);
                object.velocity.y *= std::max(0.0f, 1 - dt * 3);
                if (!object.floorHit && child.state == 'D') {
                    object.floorHit = frame.client.sendCfoObject(child.doId, Action::Floor);
                }
                if (length(object.velocity) < .5f && nowMs - object.actionMs > 1000) {
                    frame.client.sendCfoObject(child.doId, Action::Free, 0, object.position.x,
                                               object.position.y, 0, object.heading);
                    object.actionMs = nowMs;
                }
            }
        } else if (child.kind == net::CfoKind::Goon &&
                   (child.state == 'W' || child.state == 'a' || child.state == 'b')) {
            if (object.targetMs != child.targetMs) {
                object.start = object.position;
                object.targetMs = child.targetMs;
            }
            const int32_t duration = static_cast<int32_t>(child.arrivalMs - child.targetMs);
            const float fraction = duration > 0 ? std::clamp(float(nowMs - child.targetMs) / duration, 0.0f, 1.0f) : 1;
            object.position = {object.start.x + (child.targetX - object.start.x) * fraction,
                               object.start.y + (child.targetY - object.start.y) * fraction, 0};
            object.heading = child.targetH;
        } else if (child.revision != object.revision && child.state != 'S' && child.state != 'R' && child.state != 'B') {
            object.position = {child.x, child.y, child.z};
            object.heading = child.h;
        }
        object.revision = child.revision;
        if (child.kind == net::CfoKind::Treasure) {
            if (!child.grabbed && frame.client.ownStats().hp < frame.client.ownStats().maxHp &&
                length(subtract(toon, object.position)) < 3 && nowMs - object.actionMs > 1000) {
                frame.client.sendCfoObject(child.doId, Action::Treasure);
                object.actionMs = nowMs;
            }
            continue;
        }
        if (child.owner == frame.client.avatarId() && nowMs - object.sendMs >= 200) {
            frame.client.sendCfoPose(child.doId, object.position.x, object.position.y, object.position.z, object.heading);
            object.sendMs = nowMs;
        }
        if (child.kind == net::CfoKind::Goon && (child.state == 'W' || child.state == 'B' || child.state == 'a' || child.state == 'b')) {
            const bool head = cfoGoonStun(child, object.position, toon);
            const float pause = (nowMs - child.targetMs) / 1000.0f;
            net::CfoChild pose = child;
            pose.h = object.heading;
            if (nowMs - object.actionMs > 1500) {
                if (head && !object.onHead) {
                    frame.client.sendCfoObject(child.doId, Action::Stun, pause);
                    object.actionMs = nowMs;
                } else if (child.state == 'W' && cfoGoonDetect(pose, object.position, toon)) {
                    frame.client.sendCfoObject(child.doId, Action::Battle, pause);
                    object.actionMs = nowMs;
                }
            }
            object.onHead = head;
        }
        // Magnet sniffer. Grab remains a request until the AI publishes ownership.
        if (magnetOn_ && child.state != 'G' && child.state != 'D' && child.state != 'O' &&
            !(child.kind == net::CfoKind::Safe && child.index == 0 && child.state == 'I') &&
            nowMs - object.actionMs > 500) {
            for (int index = 0; index < 4; ++index) {
                const Crane& crane = cranes_[index];
                if (crane.owner != frame.client.avatarId()) continue;
                Vec3 magnet = crane.cable.magnet(index);
                magnet.z -= 10;
                Vec3 center = object.position;
                center.z += child.kind == net::CfoKind::Safe ? 4 : 2 * child.scale;
                if (length(subtract(center, magnet)) < 10 && frame.client.sendCfoObject(child.doId, Action::Grab)) {
                    object.actionMs = nowMs;
                }
            }
        }
    }
    // CFO slow directed gear attacks use attack code 11; AI remains damage authority.
    if (attackRevision_ != boss->attackRevision) {
        attackRevision_ = boss->attackRevision;
        attackActive_ = boss->attack == net::kBossSlowDirectedAttack || boss->attack == net::kBossDirectedAttack;
        attackOrigin_ = {120, -315, 12};
        attackPosition_ = attackOrigin_;
        float x = toon.x;
        float y = toon.y;
        float z = toon.z;
        frame.client.avatarPos(boss->attackToon, &x, &y, &z);
        Vec3 direction = subtract({x, y, z + 2}, attackOrigin_);
        const float distance = std::max(1.0f, length(direction));
        const float speed = boss->attack == net::kBossSlowDirectedAttack ? 25 : 40;
        attackVelocity_ = {direction.x / distance * speed, direction.y / distance * speed, direction.z / distance * speed};
        attackTime_ = 0;
    }
    if (attackActive_) {
        const Vec3 previous = attackPosition_;
        attackTime_ += dt;
        attackPosition_ = {attackOrigin_.x + attackVelocity_.x * attackTime_,
                           attackOrigin_.y + attackVelocity_.y * attackTime_,
                           attackOrigin_.z + attackVelocity_.z * attackTime_};
        if (segmentBox(previous, attackPosition_, {toon.x - 1.4f, toon.y - 1.4f, toon.z},
                       {toon.x + 1.4f, toon.y + 1.4f, toon.z + 5})) {
            frame.client.sendBossZap(toon.x, toon.y, toon.z, frame.player.headingDeg(), 0, 0, boss->attack);
            attackActive_ = false;
        }
        if (attackTime_ > 5) attackActive_ = false;
    }
    if (length(subtract(toon, kCfoBoss)) < 13 && nowMs - attackHitMs_ > 1500) {
        const Vec3 direction = subtract(toon, kCfoBoss);
        const float distance = std::max(1.0f, length(direction));
        frame.client.sendBossZap(toon.x, toon.y, toon.z, frame.player.headingDeg(),
                                 direction.x / distance, direction.y / distance, net::kBossElectricFence);
        attackHitMs_ = nowMs;
    }
}

void CfoMechanics::draw(BossFrame& frame, render::Model* models, uint32_t nowMs) {
    const net::BossState* boss = frame.client.boss();
    if (!boss) return;
    if (!std::strcmp(boss->phase, "Victory")) {
        const float age = (nowMs - boss->phaseMs) / 1000.0f;
        if (age >= 9.4f && age < 13.7f) {
            // DistributedCashbotBoss.makeBossFleeMovie: 15 cars, .2667 s spacing.
            for (int car = 0; car < 15; ++car) {
                const float time = age - 9.4f - car * (80.0f / 300);
                if (time < 0 || time > .5f) continue;
                C3D_Mtx world = matrix({150 * (1 - time / .5f), -242, 0});
                frame.renderer.drawModel(models[car == 0 ? 10 : car % 2 ? 12 : 11], world);
            }
        }
        return;
    }
    if (std::strcmp(boss->phase, "BattleThree") && std::strcmp(boss->phase, "PrepareBattleThree")) return;
    for (int index = 0; index < 4; ++index) {
        const Crane& crane = cranes_[index];
        const CfoPlacement placement = kCfoCranes[index];
        const CfoCable& cable = crane.cable;
        C3D_Mtx controls = matrix(cfoToWorld({0, -4.9f, 0}, placement), placement.heading);
        frame.renderer.drawModel(models[6], controls);
        C3D_Mtx stick = controls;
        frame.renderer.drawModel(models[7], stick);
        C3D_Mtx arm = matrix(cfoToWorld({0, -17.6f, 38.5f}, placement), placement.heading + cable.heading);
        Mtx_Translate(&arm, 0, cable.y, 0, true);
        frame.renderer.drawModel(models[5], arm);
        Vec3 previous = cfoToWorld({-std::sin(cable.heading * kDegrees) * cable.y,
            std::cos(cable.heading * kDegrees) * cable.y - 17.6f, 37.5f}, placement);
        for (int link = 0; link < 3; ++link) {
            const Vec3 local = cable.links[link];
            const Vec3 current = cfoToWorld({local.x, local.y - 17.6f, local.z + 38.5f}, placement);
            const Vec3 delta = subtract(current, previous);
            C3D_Mtx segment = matrix(previous, std::atan2(delta.y, delta.x) / kDegrees);
            Mtx_RotateY(&segment, std::atan2(std::sqrt(delta.x * delta.x + delta.y * delta.y), delta.z), true);
            Mtx_Scale(&segment, 1, 1, length(delta));
            frame.renderer.drawModel(models[17], segment);
            previous = current;
        }
        C3D_Mtx magnet = matrix(cable.magnet(index));
        Mtx_RotateZ(&magnet, 90 * kDegrees, true);
        Mtx_RotateX(&magnet, 45 * kDegrees, true);
        Mtx_RotateY(&magnet, 90 * kDegrees, true);
        frame.renderer.drawModel(models[8], magnet);
    }
    for (int slot = 0; slot < net::kMaxCfoChildren; ++slot) {
        const net::CfoChild& child = frame.client.cfoChildSlot(slot);
        const Object& object = objects_[slot];
        if (!child.active || !object.doId || child.state == 'O' || child.grabbed ||
            (child.kind == net::CfoKind::Safe && child.index == 0 && child.state == 'I')) continue;
        C3D_Mtx world = matrix(object.position, object.heading, child.kind == net::CfoKind::Goon ? child.scale : 1);
        if (child.kind == net::CfoKind::Safe) frame.renderer.drawModel(models[9], world);
        if (child.kind == net::CfoKind::Treasure) frame.renderer.drawModel(models[14], world);
        if (child.kind == net::CfoKind::Goon) {
            const bool stunned = child.state == 'S' || child.state == 'G' || child.state == 'D';
            const int clipIndex = stunned ? 1 : child.state == 'R' ? 2 : 0;
            render::Anim& clip = goonClips_[clipIndex];
            if (!clip.loaded()) continue;
            u32 first = 0;
            u32 second = 0;
            float blend = 0;
            const float seconds = (nowMs - child.stateMs) / 1000.0f;
            if (clipIndex) {
                const float sample = std::min(seconds * clip.fps(), float(clip.numFrames() - 1));
                first = static_cast<u32>(sample);
                second = std::min(first + 1, clip.numFrames() - 1);
                blend = sample - first;
            } else {
                render::sampleClip(clip, seconds * child.velocity / std::max(.1f, 4 * child.scale), &first, &second, &blend);
            }
            frame.renderer.drawMorph(models[15], clip, first, second, blend, world);
            if (child.state == 'W') {
                C3D_Mtx cone = matrix({object.position.x, object.position.y, object.position.z + 3 * child.scale}, object.heading);
                Mtx_Scale(&cone, child.radius / 20.0f, child.radius / 25.0f, .3f);
                frame.renderer.setBaseTint(1, 1, 0, .2f);
                frame.renderer.drawModel(models[16], cone);
                frame.renderer.setBaseTint(1, 1, 1, 1);
            }
        }
    }
    if (attackActive_) {
        C3D_Mtx gear = matrix(attackPosition_, attackTime_ * 720, .1f);
        frame.renderer.drawModel(models[13], gear);
    }
}
} // namespace tt::world
