#include "world/racing.h"
#include "core/log.h"
#include "world/racing_tables.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace tt::world {
namespace {
constexpr float kRadians = .017453292519943295f;
constexpr uint32_t kProgressInterval = 500; // DistributedRace.raceWatcher
constexpr uint32_t kPoseInterval = 200;     // DistributedSmoothNode broadcast period
constexpr float kKartRadius = 4;            // DistributedVehicle sphere (0,0,4), radius 4
constexpr float kGravity = 32.174f * 3;     // DistributedVehicle.setupCollisions
} // namespace
void Racing::loadAssets(uint16_t track) {
    if (loadedTrack_ == track)
        return;
    unloadAssets();
    const char *name = racing::trackName(track);
    if (!name)
        return;
    loadedTrack_ = track;
    char path[160];
    std::snprintf(path, sizeof(path), "sdmc:/3ds/toontown3ds/data/racing/%s.trc", name);
    const bool metadata = metadata_.load(path, track);
    std::snprintf(path, sizeof(path), "sdmc:/3ds/toontown3ds/data/racing/%s.ttc", name);
    const bool collision = collision_.load(path);
    std::snprintf(path, sizeof(path), "sdmc:/3ds/toontown3ds/data/racing/%s.ttm", name);
    assetBatch_.load(track_, path);
    assetBatch_.load(gagBox_, "sdmc:/3ds/toontown3ds/data/racing/qbox.ttm");
    assetBatch_.load(banana_, "sdmc:/3ds/toontown3ds/data/racing/banana.ttm");
    assetBatch_.load(anvil_, "sdmc:/3ds/toontown3ds/data/racing/anvil.ttm");
    assetBatch_.load(pie_, "sdmc:/3ds/toontown3ds/data/racing/creampie.ttm");
    TT_LOG("racing: load track %u metadata %d collision %d collision heap %lu", track, metadata,
           collision, static_cast<unsigned long>(collision_.heapBytes()));
}
void Racing::unloadAssets() {
    if (track_.loaded())
        C3D_FrameSync();
    assetBatch_.clear();
    track_.unload();
    gagBox_.unload();
    banana_.unload();
    anvil_.unload();
    pie_.unload();
    collision_.unload();
    metadata_.unload();
    loadedTrack_ = 0xffff;
}
void Racing::pumpAssets() {
    assetBatch_.pump();
    for (auto &racer : racers_)
        racer.kart.pump();
}
void Racing::handleEvent(const net::Event &event, const net::Client &client) {
    if (event.type == net::EventType::RacingPadUp || event.type == net::EventType::RacingPadState) {
        for (int i = 0; i < net::kMaxRacingPads; ++i) {
            const auto &pad = client.racingPadSlot(i);
            if (pad.active && pad.doId == event.doId)
                TT_LOG("racing: pad %lu state %s track %u mode %u zone %lu",
                       static_cast<unsigned long>(pad.doId), pad.state, pad.trackIds[0],
                       pad.trackIds[1], static_cast<unsigned long>(pad.raceZone));
        }
    }
    if (event.type == net::EventType::RacingBlockState) {
        for (int i = 0; i < net::kMaxRacingBlocks; ++i) {
            const auto &block = client.racingBlockSlot(i);
            if (block.doId != event.doId)
                continue;
            if (block.rejection) {
                const char *message = block.rejection < 7 ? racing::kRefusal[block.rejection] : "";
                const char *rest = message;
                for (auto &line : hudLines_) {
                    size_t length = std::min<size_t>(34, std::strlen(rest));
                    if (rest[length] && length) {
                        while (length && rest[length] != ' ')
                            --length;
                        if (!length)
                            length = 34;
                    }
                    std::snprintf(line, sizeof(line), "%.*s", int(length), rest);
                    rest += length;
                    while (*rest == ' ')
                        ++rest;
                }
                refusalUntil_ = *message ? now_ + 10000 : 0;
                TT_LOG("racing: reject block %lu code %u %s",
                       static_cast<unsigned long>(block.doId), block.rejection, message);
            }
        }
    }
    if (event.type == net::EventType::RacingGone && race_ == event.doId)
        shutdown();
}
void Racing::board(net::Client &client, const Input &input, float dt, float x, float y, float z) {
    const auto previousSeat = seated_;
    seated_ = 0;
    for (int i = 0; i < net::kMaxRacingBlocks; ++i) {
        const auto &block = client.racingBlockSlot(i);
        if (!block.active || block.viewing)
            continue;
        const net::RacingPad *pad = nullptr;
        for (int j = 0; j < net::kMaxRacingPads; ++j) {
            const auto &candidate = client.racingPadSlot(j);
            if (candidate.active && candidate.doId == block.padDoId)
                pad = &candidate;
        }
        if (block.occupant == client.avatarId()) {
            seated_ = block.doId;
            if (previousSeat != seated_)
                boardingOrigin_ = {x, y, z};
            boardingSide_ = block.location % 2 ? 3 : -3;
            auto &displayed = racers_[0];
            displayed.avatar = client.avatarId();
            displayed.body = std::clamp(int(client.clerkProfile().kart[0]), 0, 2);
            displayed.lod = 0;
            displayed.kart.setRacingLod(0);
            displayed.kart.select(client.clerkProfile().kart);
            displayed.x = block.x;
            displayed.y = block.y;
            displayed.z = block.z;
            displayed.h = block.h + 180;
            physics_.position = {block.x, block.y, block.z};
            physics_.heading = block.h + 180; // StartingBlock.setPosHpr
            if (previousSeat != seated_ || movieRevision_ != block.revision) {
                movieRevision_ = block.revision;
                movieAge_ = 0;
                movieDone_ = false;
                movie_ = block.movie;
                TT_LOG("racing: block %lu movie %u", static_cast<unsigned long>(block.doId),
                       block.movie);
            }
            movieAge_ += dt;
            // DistributedStartingBlock's interval durations, including the original engine sound.
            const float jump =
                std::max({toonDuration(kAnimJump), 1.33f, 1 + toonDuration(kAnimSitStart)});
            const float enter =
                1.5f + std::max(racing::kKartAppearSeconds,
                                4.3f + jump + racing::kKartStartSeconds[displayed.body]);
            const float exit = std::max(toonDuration(kAnimJump), 1.0f) + 1.2f + 1.5f;
            if (block.movie && !movieDone_ && movieAge_ >= (block.movie == 1 ? enter : exit))
                movieDone_ = client.sendRacingMovieDone(block.doId);
            if ((input.down & KEY_B) && block.movie == 1 && movieDone_ && pad &&
                !std::strcmp(pad->state, "WaitCountdown"))
                client.sendRacingExit(block.doId);
            if (pad && pad->raceZone && pad->raceZone != lastDestination_)
                destination_ = pad->raceZone;
            const int seconds = pad ? std::max(0, 30 - int((now_ - pad->stateMs) / 1000)) : 0;
            std::snprintf(hudLines_[0], sizeof(hudLines_[0]), "%s", pad ? pad->state : "Boarding");
            std::snprintf(hudLines_[1], sizeof(hudLines_[1]), "Race in %d seconds", seconds);
            std::snprintf(hudLines_[2], sizeof(hudLines_[2]), "B: leave starting block");
        } else if (!block.occupant && (input.down & KEY_A)) {
            const float dx = x - block.x, dy = y - block.y, dz = z - block.z;
            if (dx * dx + dy * dy + dz * dz < 16 && pad &&
                (!std::strcmp(pad->state, "WaitEmpty") ||
                 !std::strcmp(pad->state, "WaitCountdown"))) {
                client.sendRacingEnter(block.doId, false);
            }
        }
    }
    if (previousSeat && !seated_ && movie_ == 2) {
        const float angle = physics_.heading * kRadians;
        exitPosition_ = {physics_.position.x + std::cos(angle) * boardingSide_,
                         physics_.position.y + std::sin(angle) * boardingSide_,
                         physics_.position.z};
        exitPosePending_ = true;
    }
}
void Racing::update(net::Client &client, const Input &input, float dt, float x, float y, float z,
                    uint32_t now) {
    now_ = now;
    const auto *race = client.racingRace();
    if (!race) {
        if (refusalUntil_ > now && (input.down & (KEY_A | KEY_B))) {
            refusalUntil_ = 0;
            return;
        }
        if (race_)
            shutdown();
        board(client, input, dt, x, y, z);
        return;
    }
    if (race_ != race->doId) {
        shutdown();
        race_ = race->doId;
        now_ = now;
        lastTickets_ = client.ownBook().tickets;
        loadAssets(race->track);
        TT_LOG("racing: generated race %lu track %u racers %u", static_cast<unsigned long>(race_),
               race->track, race->numRacers);
    }
    if (phase_ != race->phase) {
        phase_ = race->phase;
        phaseMs_ = now;
        TT_LOG("racing: phase %u", phase_);
    }
    if (lastTickets_ != client.ownBook().tickets) {
        TT_LOG("racing: authoritative tickets %lu -> %lu", static_cast<unsigned long>(lastTickets_),
               static_cast<unsigned long>(client.ownBook().tickets));
        lastTickets_ = client.ownBook().tickets;
    }
    int local = -1;
    bool linksReady = race->numRacers > 0;
    for (int i = 0; i < race->numRacers; ++i) {
        const auto &entry = race->results[i];
        auto &displayed = racers_[i];
        displayed.seated.update(dt);
        displayed.avatar = entry.avatar;
        displayed.vehicle = entry.kart;
        if (entry.avatar == client.avatarId())
            local = i;
        const net::RacingVehicle *vehicle = nullptr;
        for (int j = 0; j < net::kMaxRacingVehicles; ++j) {
            const auto &candidate = client.racingVehicleSlot(j);
            if (candidate.active && candidate.doId == entry.kart)
                vehicle = &candidate;
        }
        if (!vehicle) {
            linksReady = false;
            continue;
        }
        const int8_t dna[] = {vehicle->body,       vehicle->bodyColor, vehicle->accessoryColor,
                              vehicle->engine,     vehicle->spoiler,   vehicle->frontWheels,
                              vehicle->backWheels, vehicle->rims,      vehicle->decal};
        displayed.body = std::clamp(int(vehicle->body), 0, 2);
        const float dx = displayed.x - physics_.position.x, dy = displayed.y - physics_.position.y;
        const float distance = std::sqrt(dx * dx + dy * dy);
        // LOD hysteresis prevents repeated streaming near the thresholds.
        if (i == local || distance < 25)
            displayed.lod = 0;
        else if (distance > 90)
            displayed.lod = 2;
        else if (distance > 35 && distance < 75)
            displayed.lod = 1;
        displayed.kart.setRacingLod(displayed.lod);
        displayed.kart.select(dna);
        if (i != local) {
            const float blend = displayed.initialized ? std::min(1.0f, dt / .2f) : 1;
            const racing::Vector previous{displayed.x, displayed.y, displayed.z};
            displayed.x += (vehicle->x - displayed.x) * blend;
            displayed.y += (vehicle->y - displayed.y) * blend;
            displayed.z += (vehicle->z - displayed.z) * blend;
            displayed.h += std::remainder(vehicle->h - displayed.h, 360.0f) * blend;
            if (displayed.initialized && dt > 0)
                displayed.velocity = {(displayed.x - previous.x) / dt,
                                      (displayed.y - previous.y) / dt,
                                      (displayed.z - previous.z) / dt};
            displayed.initialized = true;
        } else
            vehicle_ = vehicle->doId;
    }
    bool kartsReady = linksReady;
    for (int i = 0; i < race->numRacers; ++i)
        kartsReady &= racers_[i].kart.ready();
    const bool ready = metadata_.loaded() && collision_.loaded() && track_.loaded();
    if (local >= 0 && ready && !spawned_) {
        const int place = race->results[local].startingPlace;
        if (place < metadata_.numStarts) {
            const auto *spawn = metadata_.starts[place];
            physics_.position = {spawn[0], spawn[1], spawn[2]};
            physics_.heading = spawn[3] + (race->track % 2 ? 180 : 0);
            physics_.pitch = spawn[4];
            physics_.roll = spawn[5];
            progress_.reset(metadata_.points, metadata_.count, physics_.position, metadata_.start);
            spawned_ = true;
            TT_LOG("racing: original start %d %.2f %.2f %.2f heading %.2f", place, double(spawn[0]),
                   double(spawn[1]), double(spawn[2]), double(physics_.heading));
        }
    }
    if (race->phase >= 2 && linksReady && spawned_ && !requestedKart_)
        requestedKart_ = client.sendRaceKart();
    for (int i = 0; i < race->numBarriers; ++i) {
        const auto &barrier = race->barriers[i];
        bool done = false;
        for (int j = 0; j < acknowledgedCount_; ++j)
            done |= acknowledged_[j] == barrier.context;
        if (done || !barrier.waiting)
            continue;
        bool accept = false;
        if (!std::strcmp(barrier.name, "waitingForJoin"))
            accept = ready;
        else if (!std::strcmp(barrier.name, "waitingForPrep"))
            accept = ready && linksReady;
        else if (!std::strcmp(barrier.name, "waitingForReady"))
            accept = spawned_ && requestedKart_ && kartsReady;
        else if (!std::strcmp(barrier.name, "readRules"))
            accept = now - phaseMs_ >= 9000;
        else if (!std::strcmp(barrier.name, "waitingForExit"))
            accept = race->returning || (race->finished && (input.down & KEY_A));
        if (accept && acknowledgedCount_ < 8 && client.sendRaceBarrier(barrier.context))
            acknowledged_[acknowledgedCount_++] = barrier.context;
    }
    if (race->nextZone && race->nextZone != lastDestination_) {
        if (!left_)
            client.sendRaceLeave();
        left_ = true;
        destination_ = race->nextZone;
        TT_LOG("racing: authoritative next circuit zone %lu",
               static_cast<unsigned long>(race->nextZone));
    }
    if (race->returning && !left_ && client.sendRaceLeave()) {
        left_ = true;
        destination_ = 8000;
        TT_LOG("racing: authoritative return reason %u", race->returnReason);
    }
    finished_ = race->finished;
    if (local < 0 || !spawned_) {
        std::snprintf(hudLines_[0], sizeof(hudLines_[0]), "Loading race assets and racer links");
        return;
    }
    for (const auto &shot : race->shots) {
        if (!shot.serial || shot.serial <= shotSerial_)
            continue;
        const Racer *source = nullptr;
        for (const auto &racer : racers_)
            if (racer.avatar == shot.source)
                source = &racer;
        if (!source)
            continue;
        auto &pie = pies_[(shot.serial - 1) % 16];
        pie.target = shot.target;
        const racing::Vector velocity =
            shot.source == client.avatarId() ? physics_.velocity : source->velocity;
        pie.physics.launch({source->x, source->y, source->z}, velocity, source->h,
                           shot.target != 0);
    }
    shotSerial_ = race->shotSerial;
    for (auto &pie : pies_) {
        if (!pie.physics.active)
            continue;
        racing::Vector target{};
        float targetSpeed = 0;
        bool exists = false;
        for (const auto &racer : racers_)
            if (racer.avatar && racer.avatar == pie.target) {
                target = {racer.x, racer.y, racer.z};
                targetSpeed = pie.target == client.avatarId()
                                  ? physics_.speed()
                                  : std::sqrt(racer.velocity.x * racer.velocity.x +
                                              racer.velocity.y * racer.velocity.y +
                                              racer.velocity.z * racer.velocity.z);
                exists = true;
            }
        pie.physics.targeted &= exists;
        if (pie.physics.update(dt, target, targetSpeed)) {
            // Piejectile.splatTarget is a cosmetic splat, not hitPie's vehicle slowdown.
            TT_LOG("racing: pie splat target %lu", static_cast<unsigned long>(pie.target));
        }
        auto &pos = pie.physics.position;
        bool found = false;
        const float floor = collision_.groundHeight(pos.x, pos.y, 40000, &found);
        if (found && pos.z < floor + 40)
            pos.z = std::max(pos.z, floor + kFloorOffset);
    }
    if (hitRevision_ != race->hitRevision) {
        hitRevision_ = race->hitRevision;
        if (race->hitAvatar == client.avatarId()) {
            // DistributedVehicle dropOnHim / hitPie: inhibit controls and damp velocity.
            hitStart_ =
                now + 1000; // DistributedVehicle.dropOnMe: stopCar follows the one-second drop.
            hitUntil_ = now + 2500;
            physics_.hitMultiplier = race->hitType == 3 ? .9f : .99f;
            turboStart_ = 0;
            TT_LOG("racing: hit type %u", race->hitType);
        }
    }
    if (int32_t(now - hitUntil_) >= 0)
        physics_.hitMultiplier = 1;
    physics_.stopped = (int32_t(now - hitUntil_) < 0 && int32_t(now - hitStart_) >= 0) ||
                       !race->started || int32_t(now - race->startMs) < 0 || finished_ ||
                       progress_.laps >= race->laps;
    physics_.turbo = turboStart_ && now - turboStart_ >= 2000 && now - turboStart_ < 5000;
    physics_.surface =
        metadata_.surface(physics_.position.x, physics_.position.y, physics_.position.z);
    const auto old = physics_.position;
    float horizontal = input.pad.dx / 156.0f;
    float vertical = input.pad.dy / 156.0f;
    if (input.held & KEY_A)
        vertical = 1;
    if (input.held & KEY_B)
        vertical = -1;
    physics_.update(dt, horizontal, vertical);
    const auto desired = physics_.position;
    const auto resolved =
        collision_.slideCapsule({old.x, old.y, old.z}, kKartRadius, kKartRadius * 2,
                                {desired.x - old.x, desired.y - old.y, desired.z - old.z});
    // The native collision solver resolves the same sphere with bounded substeps.
    physics_.wallShove(resolved.x - desired.x, resolved.y - desired.y);
    physics_.position = {resolved.x, resolved.y, resolved.z};
    bool ground = false;
    const float height = collision_.groundHeight(resolved.x, resolved.y, 40000, &ground);
    if (ground && physics_.position.z <= height + 4) {
        physics_.position.z = height + kFloorOffset;
        gravityVelocity_ = 0;
    } else {
        gravityVelocity_ -= kGravity * dt;
        physics_.position.z += gravityVelocity_ * dt;
    }
    // Four original wheel-base rays drive DistributedVehicle.stickCarToGround.
    const auto &anchors = racing::kKartAnchors[0][racers_[local].body];
    C3D_Mtx kartMatrix;
    Mtx_Identity(&kartMatrix);
    Mtx_Translate(&kartMatrix, physics_.position.x, physics_.position.y, physics_.position.z, true);
    Mtx_RotateZ(&kartMatrix, physics_.heading * kRadians, true);
    Mtx_RotateX(&kartMatrix, physics_.pitch * kRadians, true);
    Mtx_RotateY(&kartMatrix, physics_.roll * kRadians, true);
    float offsets[4] = {};
    bool wheelsGrounded = true;
    for (int i = 0; i < 4; ++i) {
        const auto *wheel = anchors.wheels[i];
        const C3D_FVec world =
            Mtx_MultiplyFVec4(&kartMatrix, FVec4_New(wheel[0], wheel[1], wheel[2], 1));
        bool found = false;
        const float floor = collision_.groundHeight(world.x, world.y, world.z + 20, &found);
        offsets[i] = world.z - floor;
        wheelsGrounded &= found;
    }
    if (wheelsGrounded) {
        const float right = (offsets[0] + offsets[2]) * .5f;
        const float left = (offsets[1] + offsets[3]) * .5f;
        const float front = (offsets[0] + offsets[1]) * .5f;
        const float rear = (offsets[2] + offsets[3]) * .5f;
        physics_.roll += std::clamp(right - left, -1.0f, 1.0f) * 2;
        physics_.pitch -= std::clamp(front - rear, -1.0f, 1.0f);
    }
    const bool wrong = progress_.wrongWay;
    const bool lapChanged = progress_.update(metadata_.points, metadata_.count, physics_.position,
                                             physics_.heading, metadata_.start);
    if (lapChanged)
        TT_LOG("racing: lap %d t %.6f", progress_.laps, double(progress_.fraction));
    if (wrong != progress_.wrongWay)
        TT_LOG("racing: wrong way %d", progress_.wrongWay);
    place_ = 1;
    const float ownProgress = progress_.laps + progress_.fraction;
    for (int i = 0; i < race->numRacers; ++i) {
        const auto &result = race->results[i];
        if (i != local && !result.disconnected &&
            (result.finished || result.laps + result.fraction > ownProgress))
            ++place_;
    }
    if (race->started && !finished_) {
        if (now - lastPose_ >= kPoseInterval &&
            client.sendRaceVehiclePose(vehicle_, physics_.position.x, physics_.position.y,
                                       physics_.position.z, physics_.heading, physics_.pitch,
                                       physics_.roll, now))
            lastPose_ = now;
        if ((lapChanged || now - lastProgress_ >= kProgressInterval) &&
            client.sendRaceProgress(int8_t(std::clamp(progress_.laps, -128, 127)),
                                    progress_.fraction, now))
            lastProgress_ = now;
    }
    auto &displayed = racers_[local];
    displayed.x = physics_.position.x;
    displayed.y = physics_.position.y;
    displayed.z = physics_.position.z;
    displayed.h = physics_.heading;
    if (!physics_.stopped && !heldGag_) {
        for (int i = 0; i < metadata_.numGags; ++i) {
            const auto &gag = race->gags[i];
            const auto &position = metadata_.gags[i];
            const float dx = position.x - displayed.x, dy = position.y - displayed.y;
            if (gag.active && dx * dx + dy * dy < 16 &&
                std::fabs(position.z + 3 - displayed.z) < 10) {
                const auto type =
                    racing::kGagFrequency[std::clamp(int(place_) - 1, 0, 3)][gag.index];
                if (client.sendRacingGag(race_, uint8_t(i), type, gag.index)) {
                    heldGag_ = type;
                    heldSince_ = now;
                    TT_LOG("racing: gag pickup slot %d type %u", i, type);
                    break;
                }
            }
        }
    }
    for (int i = 0; i < 32; ++i) {
        const auto &hazard = client.racingHazardSlot(i);
        if (!hazard.active || hazard.projectile || hazard.race != race_ ||
            hazard.owner == client.avatarId() || consumedHazards_[i] == hazard.doId ||
            now - hazard.initMs < 800)
            continue;
        const float dx = hazard.x - displayed.x, dy = hazard.y - displayed.y;
        if (dx * dx + dy * dy < 36 && std::fabs(hazard.z - displayed.z) < 6 &&
            client.sendRaceHazardHit(hazard.doId, now)) {
            consumedHazards_[i] = hazard.doId;
            hitStart_ = now;
            hitUntil_ = now + (hazard.type == 0 ? 1000 : 500);
            physics_.hitMultiplier = .99f;
            turboStart_ = 0;
            TT_LOG("racing: hazard hit %lu type %u", static_cast<unsigned long>(hazard.doId),
                   hazard.type);
        }
    }
    if (heldGag_ && now - heldSince_ >= 2000 && (input.down & KEY_R)) {
        const bool positioned = heldGag_ == 1 || heldGag_ == 4;
        if (client.sendRacingThrow(race_, positioned ? displayed.x : 0,
                                   positioned ? displayed.y : 0, positioned ? displayed.z : 0)) {
            if (heldGag_ == 2)
                turboStart_ = now;
            TT_LOG("racing: gag throw type %u", heldGag_);
            heldGag_ = 0;
        }
    }
    if (finished_) {
        const auto &result = race->results[local];
        place_ = result.place;
        std::snprintf(hudLines_[0], sizeof(hudLines_[0]), "Place %u  Time %.2f  Qualified %s",
                      place_, double(result.time), result.qualify ? "yes" : "no");
        std::snprintf(
            hudLines_[1], sizeof(hudLines_[1]), "Fee %lu  Won %lu  Bonus %lu  Tickets %ld",
            static_cast<unsigned long>(result.entryFee),
            static_cast<unsigned long>(result.winnings), static_cast<unsigned long>(result.bonus),
            static_cast<long>(client.ownBook().tickets));
        std::snprintf(hudLines_[2], sizeof(hudLines_[2]), "%s",
                      race->circuitCount && race->raceType == 2 ? "A: continue circuit"
                                                                : "A: return to Goofy Speedway");
    } else {
        const int countdown =
            race->started ? std::max<int32_t>(0, int32_t(race->startMs - now) / 1000 + 1) : -1;
        std::snprintf(hudLines_[0], sizeof(hudLines_[0]), "Lap %u/%u  Place %u/%u  Speed %.0f",
                      lap(), race->laps, place_, race->numRacers, double(speed()));
        std::snprintf(hudLines_[1], sizeof(hudLines_[1]), "%s  Time %.1f  Start %d",
                      progress_.wrongWay ? "WRONG WAY" : "",
                      race->started ? std::max(0.0, int32_t(now - race->startMs) / 1000.0) : 0,
                      countdown);
        std::snprintf(hudLines_[2], sizeof(hudLines_[2]),
                      "Gag %u  R: use  A: accelerate  B: reverse", heldGag_);
    }
    if (!left_ && (race->returning || (finished_ && (input.down & KEY_A)))) {
        if (client.sendRaceLeave()) {
            left_ = true;
            if (!race->circuitCount || race->raceType != 2 || race->returning)
                destination_ = 8000;
            // Circuit intermediate rounds wait for the AI's setRaceZone.
            TT_LOG("racing: leave acknowledged, destination %lu tickets %lu",
                   static_cast<unsigned long>(destination_),
                   static_cast<unsigned long>(client.ownBook().tickets));
        }
    }
}
uint32_t Racing::popDestination() {
    const auto value = destination_;
    destination_ = 0;
    if (value)
        lastDestination_ = value;
    return value;
}
bool Racing::spawnPose(float *x, float *y, float *z, float *h) {
    if (exitPosePending_) {
        exitPosePending_ = false;
        *x = exitPosition_.x;
        *y = exitPosition_.y;
        *z = exitPosition_.z;
        *h = physics_.heading + (boardingSide_ > 0 ? 90 : -90);
        return true;
    }
    if (!driving() || (active() && !spawned_))
        return false;
    *x = physics_.position.x;
    *y = physics_.position.y;
    *z = physics_.position.z;
    *h = physics_.heading;
    return true;
}
void Racing::camera(C3D_FVec *eye, C3D_FVec *target) const {
    const float h = physics_.heading * kRadians;
    const auto p = physics_.position;
    // DistributedVehicle normal camera; StartingBlock cameraPos is (0,-23,10).
    const float back = active() ? 33 : 23;
    const float up = active() ? 16 : 10;
    const Vec3 from{p.x, p.y, p.z + 4};
    Vec3 to{p.x + std::sin(h) * back, p.y - std::cos(h) * back, p.z + up};
    float hit = 1;
    if (collision_.cameraRaycast(from, to, &hit)) {
        hit = std::max(0.0f, hit - .03f);
        to = {from.x + (to.x - from.x) * hit, from.y + (to.y - from.y) * hit,
              from.z + (to.z - from.z) * hit};
    }
    *eye = FVec3_New(to.x, to.y, to.z);
    *target = FVec3_New(from.x, from.y, from.z);
}
void Racing::draw(render::Renderer &renderer, const net::Client &client) {
    if (!driving())
        return;
    C3D_Mtx matrix;
    Mtx_Identity(&matrix);
    if (active() && track_.loaded())
        renderer.drawModel(track_, matrix);
    for (auto &racer : racers_) {
        if (!racer.avatar)
            continue;
        const bool own = racer.avatar == client.avatarId();
        racer.kart.draw(renderer, racer.x, racer.y, racer.z, racer.h, own ? physics_.pitch : 0,
                        own ? physics_.roll : 0,
                        active() ? 2 : std::clamp((movieAge_ - 3.6f) / 1.2f, .1f, 1.0f));
    }
    const auto *race = client.racingRace();
    if (race && gagBox_.loaded())
        for (int i = 0; i < metadata_.numGags; ++i) {
            if (!race->gags[i].active)
                continue;
            const auto &pos = metadata_.gags[i];
            Mtx_Identity(&matrix);
            Mtx_Translate(&matrix, pos.x, pos.y, pos.z - 1, true);
            Mtx_Scale(&matrix, 4, 4, 4); // RaceGag.genGag
            renderer.drawModel(gagBox_, matrix);
        }
    if (banana_.loaded())
        for (int i = 0; i < 32; ++i) {
            const auto &hazard = client.racingHazardSlot(i);
            if (!hazard.active || hazard.race != race_ || hazard.projectile ||
                consumedHazards_[i] == hazard.doId)
                continue;
            Mtx_Identity(&matrix);
            Mtx_Translate(&matrix, hazard.x, hazard.y, hazard.z, true);
            const float scale =
                hazard.type == 0 ? 1.8f : 8.0f; // DistributedGag uses banana for both types.
            Mtx_Scale(&matrix, scale, scale, scale);
            renderer.drawModel(banana_, matrix);
        }
    if (pie_.loaded())
        for (const auto &pie : pies_) {
            if (!pie.physics.active)
                continue;
            const auto pos = pie.physics.position;
            Mtx_Identity(&matrix);
            Mtx_Translate(&matrix, pos.x, pos.y, pos.z, true);
            renderer.drawModel(pie_, matrix);
        }
    if (anvil_.loaded() && race && race->hitType == 3 && race->hitAvatar == client.avatarId() &&
        int32_t(hitUntil_ - now_) > 0) {
        const float elapsed = 2.5f - (hitUntil_ - now_) / 1000.0f;
        Mtx_Identity(&matrix);
        Mtx_Translate(&matrix, physics_.position.x, physics_.position.y,
                      physics_.position.z + 100 * (1 - std::clamp(elapsed, 0.0f, 1.0f)) +
                          16.087f * std::clamp(elapsed, 0.0f, 1.0f) *
                              (1 - std::clamp(elapsed, 0.0f, 1.0f)),
                      true);
        Mtx_Scale(&matrix, 7, 9, 7);
        renderer.drawModel(anvil_, matrix);
    }
}
void Racing::drawToons(render::Renderer &renderer, ToonAvatar &local, const RemoteToons &remote,
                       uint32_t own) {
    for (auto &racer : racers_) {
        if (!racer.avatar)
            continue;
        const auto *avatar = racer.avatar == own ? &local : remote.avatar(racer.avatar);
        if (!avatar)
            continue;
        const auto *seat = racing::kKartAnchors[racer.lod][racer.body].seat;
        C3D_Mtx matrix;
        Mtx_Identity(&matrix);
        Mtx_Translate(&matrix, racer.x, racer.y, racer.z, true);
        Mtx_RotateZ(&matrix, racer.h * kRadians, true);
        if (racer.avatar == own && active()) {
            Mtx_RotateX(&matrix, physics_.pitch * kRadians, true);
            Mtx_RotateY(&matrix, physics_.roll * kRadians, true);
        }
        if (!active()) {
            const float endX = seat[0] * .5f;
            const float endY = seat[1] * .5f + .225f;
            const float endZ = seat[2] * .5f - .125f;
            float x = endX, y = endY, z = endZ, h = 0, scale = .5f;
            if (movie_ == 1 && !movieDone_) {
                if (movieAge_ < 1.5f) {
                    const float angle = racer.h * kRadians;
                    const float dx = boardingOrigin_.x - racer.x, dy = boardingOrigin_.y - racer.y;
                    const float startX = std::cos(angle) * dx + std::sin(angle) * dy;
                    const float startY = -std::sin(angle) * dx + std::cos(angle) * dy;
                    const float t = std::clamp(movieAge_ - .5f, 0.0f, 1.0f);
                    x = startX + (boardingSide_ - startX) * t;
                    y = startY * (1 - t);
                    z = 0;
                    scale = 1;
                } else if (movieAge_ < 5.8f + std::max({toonDuration(kAnimJump), 1.33f,
                                                        1 + toonDuration(kAnimSitStart)})) {
                    // generateToonJumpTrack: wait .43 then a .9-second ProjectileInterval.
                    const float t = std::clamp((movieAge_ - 5.8f - .43f) / .9f, 0.0f, 1.0f);
                    x = boardingSide_ + (seat[0] * .5f - boardingSide_) * t;
                    y = seat[1] * .5f * t;
                    z = seat[2] * .5f * t + .5f * 32.174f * .9f * .9f * t * (1 - t);
                    h = (boardingSide_ > 0 ? 90 : -90) * (1 - t);
                    scale = 1;
                }
            } else if (movie_ == 2) {
                const float t = std::clamp((movieAge_ - .1f) / .9f, 0.0f, 1.0f);
                x = endX + (boardingSide_ - endX) * t;
                y = endY * (1 - t);
                z = endZ * (1 - t) + .5f * 32.174f * .9f * .9f * t * (1 - t);
                h = (boardingSide_ > 0 ? 90 : -90) * t;
                scale = 1;
            }
            Mtx_Translate(&matrix, x, y, z, true);
            Mtx_RotateZ(&matrix, h * kRadians, true);
            Mtx_Scale(&matrix, scale, scale, scale);
        } else
            Mtx_Translate(&matrix, seat[0], seat[1], seat[2], true);
        if (racer.avatar == own)
            avatar->draw(renderer, matrix);
        else {
            racer.seated.setLook(avatar->look());
            racer.seated.play(kAnimSit, 1);
            racer.seated.draw(renderer, matrix);
        }
    }
}
void Racing::animate(ToonAvatar &avatar) const {
    if (active() || movieDone_) {
        avatar.play(kAnimSit, 1);
        return;
    }
    if (movie_ == 1) {
        const float jumpStart = 1.5f + 4.3f;
        if (movieAge_ < 1.5f)
            avatar.play(kAnimWalk, 1);
        else if (movieAge_ < jumpStart)
            avatar.play(kAnimNeutral, 1);
        else if (movieAge_ < jumpStart + 1) {
            avatar.playAction(kAnimJump, ClipMode::Once);
            avatar.seek(movieAge_ - jumpStart);
        } else if (movieAge_ < jumpStart + 1 + toonDuration(kAnimSitStart)) {
            avatar.playAction(kAnimSitStart, ClipMode::Once);
            avatar.seek(movieAge_ - jumpStart - 1);
        } else
            avatar.play(kAnimSit, 1);
    } else if (movie_ == 2 && movieAge_ < toonDuration(kAnimJump)) {
        avatar.playAction(kAnimJump, ClipMode::Once);
        avatar.seek(movieAge_);
    } else
        avatar.play(kAnimNeutral, 1);
}
void Racing::autopilot(Input *input) const {
    if (!spawned_ || !metadata_.loaded() || finished_) {
        if (finished_)
            input->down |= KEY_A;
        return;
    }
    const auto control =
        racing::diagnosticControls(metadata_.points, metadata_.count, progress_.pole, physics_);
    input->pad.dx = int16_t(control.horizontal * 156);
    input->pad.dy = int16_t(control.vertical * 156);
    input->held &= ~(KEY_A | KEY_B);
}
void Racing::fillView(ui::RacingView *view, const net::Client &client) const {
    *view = ui::RacingView{};
    if (!metadata_.loaded())
        return;
    float maxX = -1e30f, maxY = -1e30f;
    view->minX = view->minY = 1e30f;
    for (int i = 0; i < 64; ++i) {
        const auto &point = metadata_.points[i * metadata_.count / 64];
        view->curve[i][0] = point.x;
        view->curve[i][1] = point.y;
        view->minX = std::min(view->minX, point.x);
        view->minY = std::min(view->minY, point.y);
        maxX = std::max(maxX, point.x);
        maxY = std::max(maxY, point.y);
    }
    view->spanX = std::max(1.0f, maxX - view->minX);
    view->spanY = std::max(1.0f, maxY - view->minY);
    for (const auto &racer : racers_) {
        if (!racer.avatar)
            continue;
        const int i = view->count++;
        view->racers[i][0] = racer.x;
        view->racers[i][1] = racer.y;
        const auto *toon = client.findToon(racer.avatar);
        std::snprintf(view->names[i], sizeof(view->names[i]), "%s",
                      racer.avatar == client.avatarId() ? "You"
                      : toon                            ? toon->name
                                                        : "Racer");
    }
}
void Racing::addBalloons(ui::Balloons &balloons, const C3D_Mtx &proj, const C3D_Mtx &view,
                         const net::Client &client) const {
    for (const auto &racer : racers_) {
        if (!racer.avatar || racer.avatar == client.avatarId())
            continue;
        const auto *toon = client.findToon(racer.avatar);
        float x, y, depth;
        if (toon && ui::projectToScreen(proj, view, racer.x, racer.y, racer.z + 6, &x, &y, &depth))
            balloons.addSpeaker(x, y, toon->name, nullptr, 99, depth);
    }
}
void Racing::shutdown() {
    bool resident = active();
    for (const auto &racer : racers_)
        resident |= racer.kart.active();
    if (resident)
        C3D_FrameSync();
    for (auto &racer : racers_) {
        racer.kart.shutdown();
        racer.avatar = racer.vehicle = 0;
        racer.initialized = false;
    }
    race_ = vehicle_ = seated_ = destination_ = lastDestination_ = 0;
    physics_ = racing::Vehicle{};
    progress_ = racing::Progress{};
    spawned_ = requestedKart_ = finished_ = left_ = exitPosePending_ = false;
    acknowledgedCount_ = 0;
    phase_ = 255;
    heldGag_ = 0;
    hitStart_ = hitUntil_ = hitRevision_ = turboStart_ = shotSerial_ = 0;
    for (auto &pie : pies_)
        pie.physics = racing::Pie{};
    for (auto &id : consumedHazards_)
        id = 0;
    unloadAssets();
}
} // namespace tt::world
