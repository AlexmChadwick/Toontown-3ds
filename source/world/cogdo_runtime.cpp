#include "world/cogdo_runtime.h"
#include "audio/audio.h"
#include "core/log.h"
#include "core/nearest.h"
#include "core/profile.h"
#include "core/screenshot.h"
#include "net/net_platform.h"
#include "ui/sos_tables.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <malloc.h>
namespace tt::world {
namespace {
constexpr float kDegrees = 0.01745329252f;
// CogdoMazeGameGlobals: ToonRunSpeed=11.2, gagDistance=18, gagDuration=.5.
constexpr int kMazePadLimit = 94;
constexpr float kGagDistance = 18;
constexpr float kGagDuration = .5f;
constexpr uint32_t kMazeActorBase = 0xe0000000;
// CogdoBarrelRoomConsts.BarrelProps (model space).
const CogdoPlacement kBarrels[10] = {
    {{-10, -66, 0}, 9},        {{-7.8f, -54.5f, 0}, 12}, {{-10.5f, -44, 0}, 166}, {{-8.9f, -33.5f, 0}, 142},
    {{-9.6f, -19.8f, 0}, 94},  {{7.8f, -63.9f, 0}, 169}, {{10, -44.5f, 0}, 120},  {{7.4f, -36.6f, 0}, 127},
    {{10.6f, -27.5f, 0}, 141}, {{10, -14.4f, 0}, 2}};
float distanceSquared(Vec3 a, Vec3 b) {
    const float x = a.x - b.x, y = a.y - b.y, z = a.z - b.z;
    return x * x + y * y + z * z;
}
Vec3 playerPosition(const Player &player) {
    return {player.x(), player.y(), player.z()};
}
C3D_Mtx transform(Vec3 p, float heading = 0, float scale = 1) {
    C3D_Mtx matrix;
    Mtx_Identity(&matrix);
    Mtx_Translate(&matrix, p.x, p.y, p.z, true);
    Mtx_RotateZ(&matrix, heading * kDegrees, true);
    Mtx_Scale(&matrix, scale, scale, scale);
    return matrix;
}
bool scripted(const BossFrame &frame) {
    return frame.test.active() && !std::strncmp(frame.test.label(), "cogdo: ", 7);
}
void walkToward(Player &player, Vec3 goal, Input *input) {
    const float x = goal.x - player.x(), y = goal.y - player.y();
    if (x * x + y * y > .2f) {
        player.setHeading(std::atan2(-x, y) / kDegrees);
        input->pad.dx = 0;
        input->pad.dy = kMazePadLimit;
    }
}
} // namespace
CogdoScene CogdoRuntime::select(const net::CogdoState &interior, const net::CogdoState *game) const {
    if (std::strstr(interior.state, "Barrel"))
        return CogdoScene::Barrels;
    if (interior.floor >= interior.numFloors)
        return CogdoScene::Suite;
    if (!std::strncmp(interior.state, "Battle", 6) || !std::strcmp(interior.state, "ReservesJoining"))
        return CogdoScene::BattleFloor;
    if (game) {
        if (game->kind == net::CogdoKind::Maze)
            return CogdoScene::Maze;
        if (game->kind == net::CogdoKind::CraneGame)
            return CogdoScene::Crane;
        if (game->kind == net::CogdoKind::Boardroom)
            return CogdoScene::Boardroom;
        if (game->kind == net::CogdoKind::Flying)
            return CogdoScene::None;
    }
    return CogdoScene::Maze;
}
void CogdoRuntime::board(BossFrame &frame) {
    if (victor_ && frame.client.sendCogdoVictor(victor_)) {
        TT_LOG("cogdo: victor ready building %u", unsigned(victor_));
        victor_ = 0;
    }
    const bool autoBoard = scripted(frame) && !std::strcmp(frame.test.label(), "cogdo: board");
    if (autoBoard && frame.place.zoneId() / 100 != 21) {
        frame.uiState.teleportRequest = 2100;
        return;
    }
    for (int index = 0; index < net::kMaxCogBuildings; ++index) {
        const net::CogBuilding &building = frame.client.cogBuildingSlot(index);
        if (!building.active || building.state != net::CogBuildingState::Cogdo || building.cogdoVictory)
            continue;
        const Place::Point *anchor = frame.place.buildingPoint(building.block);
        if (!anchor)
            continue;
        // Offset from DistributedBuilding.loadCogdo (also in points.bin).
        const float angle = anchor->h * kDegrees;
        exit_ = {anchor->x + 15.463f * std::cos(angle), anchor->y + 15.463f * std::sin(angle), anchor->z};
        if (!autoBoard && distanceSquared(playerPosition(frame.player), exit_) > 100)
            continue;
        exterior_ = frame.place.zoneId();
        building_ = building.doId;
        exitHeading_ = anchor->h;
        exitValid_ = true;
        if (autoBoard)
            frame.player.reset(exit_.x, exit_.y, exit_.z + .1f, exitHeading_);
        return; // Ordinary CogBuildings boarding remains authoritative.
    }
}
void CogdoRuntime::leave(BossFrame &frame) {
    C3D_FrameSync();
    frame.cache.release(&ownerLook_);
    ownerToon_.setLook(nullptr);
    ownerNpc_ = 0;
    assets_.release();
    for (int index = 0; index < kCogdoMazeSuits; ++index)
        frame.suits.removeExternal(kMazeActorBase + index);
    frame.battle.reset();
    frame.ui.setBattle(nullptr, {});
    frame.remotes.clear();
    const bool sad = frame.client.ownStats().hp <= 0;
    if (sad)
        exterior_ = hoodOf(frame.client.ownStats().defaultZone);
    if (!Place::available(exterior_) || !frame.place.load(exterior_)) {
        exterior_ = 2000;
        frame.place.load(exterior_);
    }
    float x = 0, y = 0, z = 0, h = 0;
    frame.place.arrival(0, &x, &y, &z, &h);
    if (!sad && exitValid_) {
        x = exit_.x + 8 * std::sin(exitHeading_ * kDegrees);
        y = exit_.y - 8 * std::cos(exitHeading_ * kDegrees);
        z = exit_.z;
        h = exitHeading_ + 180;
    }
    frame.place.finishLoading(x, y, z, nullptr, nullptr);
    frame.player.setCollision(&frame.place.collision());
    frame.player.setDynamic(nullptr);
    frame.player.reset(x, y, z, h);
    frame.suits.setZone(exterior_);
    if (frame.client.inZone() && frame.client.zoneId() != exterior_)
        frame.client.changeZone(exterior_);
    frame.client.sendSetParent(net::kParentRender);
    victor_ = !sad && !std::strcmp(state_, "Reward") ? building_ : 0;
    TT_LOG("cogdo: return zone %u reward pending %u", unsigned(exterior_), unsigned(victor_));
    active_ = false;
    interior_ = game_ = 0;
}
void CogdoRuntime::maze(BossFrame &frame, const net::CogdoState &game, uint32_t now, Input *movement) {
    if (!mazeReady_) {
        mazeReady_ = maze_.load(game.doId, game.suitCounts,
                                cogdoDifficulty(game.difficulty, game.exterior, game.playground));
        logicFailed_ = !mazeReady_;
        if (!mazeReady_)
            return;
        TT_LOG("cogdo: maze seed %u suits %d coolers %d", unsigned(game.doId), maze_.numSuits,
               maze_.numCoolers);
    }
    if (game.phase != net::CogdoGamePhase::Playing) {
        *movement = {};
        return;
    }
    const float elapsed = std::max(0.0f, int32_t(now - game.startMs) / 1000.0f);
    maze_.update(elapsed, game.suitHp);
    const Vec3 player = playerPosition(frame.player);
    // Network death order assigns memo IDs. Positions are cosmetic; IDs and claims
    // always come from stock suitHitByGag / pickUp messages.
    while (deaths_ < game.numDeaths) {
        const int serial = game.deaths[deaths_++];
        if (serial >= maze_.numSuits)
            continue;
        const auto &suit = maze_.suits[serial];
        const int count = suit.type == 0 ? 0 : suit.type == 1 ? 3 : 2;
        for (int item = 0; item < count && numMemos_ < 256; ++item) {
            memos_[numMemos_++].position = {suit.position.x + float(item - 1) * 1.2f, suit.position.y, 0};
        }
    }
    int nearest = -1;
    float best = 1e12f;
    for (int index = 0; index < maze_.numSuits; ++index) {
        const auto &suit = maze_.suits[index];
        if (!game.suitHp[index])
            continue;
        const float distance = distanceSquared(player, suit.position);
        if ((nearest < 0 || suit.type == 0 || maze_.suits[nearest].type != 0) && distance < best) {
            nearest = index;
            best = distance;
        }
        const float suitScale = suit.type == 0 ? 2.5f : suit.type == 1 ? 1.3f : 1.33f;
        const float contactRadius = 1 + 1.5f * suitScale;
        if (distance < contactRadius * contactRadius && now - contactMs_ >= 4250 &&
            frame.client.sendCogdoSuit(suit.type, index, false))
            contactMs_ = now;
    }
    if (scripted(frame)) {
        *movement = {};
        Vec3 goal = assets_.mazeExit;
        if (!game.doorOpen && game.hasGag && nearest >= 0)
            goal = maze_.suits[nearest].position;
        if (!game.doorOpen && !game.hasGag) {
            float closest = 1e12f;
            for (int index = 0; index < maze_.numCoolers; ++index) {
                const float distance = distanceSquared(player, maze_.coolers[index].trigger);
                if (distance < closest) {
                    closest = distance;
                    goal = maze_.coolers[index].trigger;
                }
            }
        }
        Vec3 next;
        if (maze_.route(player, goal, &next))
            walkToward(frame.player, next, movement);
        // Fire only down a clear corridor. The travelling balloon below decides hits.
        if (game.hasGag && nearest >= 0 && best < kGagDistance * kGagDistance) {
            const Vec3 target = maze_.suits[nearest].position;
            bool clear = true;
            for (int step = 1; step < 16; ++step) {
                int x, y;
                CogdoMaze::cell({player.x + (target.x - player.x) * step / 16,
                                 player.y + (target.y - player.y) * step / 16, 0},
                                &x, &y);
                clear &= maze_.accessible(x, y);
            }
            if (clear) {
                *movement = {};
                frame.player.setHeading(std::atan2(player.x - target.x, target.y - player.y) / kDegrees);
                movement->down |= KEY_B;
            }
        }
    }
    if (game.hitMs && now - game.hitMs < 2250) {
        *movement = {};
        frame.toon.playAction(toonAnimByName("slip-backward"), ClipMode::Once);
    }
    movement->pad.dy = std::clamp<int>(movement->pad.dy, -kMazePadLimit, kMazePadLimit);
    movement->held &= ~KEY_B; // B is throw, never the ordinary slow-walk modifier.
    if (!game.hasGag && now - actionMs_ >= 1000) {
        for (int index = 0; index < maze_.numCoolers; ++index) {
            if (distanceSquared(player, maze_.coolers[index].trigger) > 10.8f)
                continue;
            frame.client.sendPosHpr(player.x, player.y, player.z, frame.player.headingDeg(), 0, 0);
            if (frame.client.sendCogdoCooler(index))
                actionMs_ = now;
            break;
        }
    }
    if ((movement->down & KEY_B) && game.hasGag && !balloon_ && now - actionMs_ >= 1200 &&
        frame.client.sendCogdoGag(player.x, player.y, frame.player.headingDeg())) {
        balloon_ = true;
        balloonOrigin_ = player;
        balloonOrigin_.z += 3;
        balloonPosition_ = balloonOrigin_;
        balloonHeading_ = frame.player.headingDeg();
        balloonAge_ = 0;
        actionMs_ = now;
        frame.toon.playAction(kAnimThrow, ClipMode::Once);
    }
    if (balloon_) {
        const Vec3 previous = balloonPosition_;
        balloonAge_ += frame.dt;
        const float fraction = std::min(1.0f, balloonAge_ / kGagDuration);
        balloonPosition_ = {balloonOrigin_.x - kGagDistance * fraction * std::sin(balloonHeading_ * kDegrees),
                            balloonOrigin_.y + kGagDistance * fraction * std::cos(balloonHeading_ * kDegrees),
                            balloonOrigin_.z + 2 * fraction * (1 - fraction)};
        for (int sample = 1; sample <= 4 && balloon_; ++sample) {
            const float t = sample / 4.0f;
            const Vec3 point{previous.x + (balloonPosition_.x - previous.x) * t,
                             previous.y + (balloonPosition_.y - previous.y) * t, 0};
            int x, y;
            CogdoMaze::cell(point, &x, &y);
            if (!maze_.accessible(x, y)) {
                balloon_ = false;
                break;
            }
            for (int index = 0; index < maze_.numSuits && balloon_; ++index) {
                const auto &suit = maze_.suits[index];
                const float radius = 1.5f * (suit.type == 0 ? 2.5f : suit.type == 1 ? 1.3f : 1.33f) + .5f;
                if (game.suitHp[index] && distanceSquared(point, suit.position) < radius * radius) {
                    frame.client.sendCogdoSuit(suit.type, index, true);
                    balloon_ = false;
                }
            }
        }
        if (balloonAge_ >= kGagDuration)
            balloon_ = false;
    }
    for (int index = 0; index < numMemos_; ++index) {
        auto &memo = memos_[index];
        if (!game.pickups[index] && now - memo.requested >= 1000 &&
            distanceSquared(player, memo.position) < 9 && frame.client.sendCogdoPickup(index))
            memo.requested = now;
    }
    if (game.doorOpen && !enteredDoor_ && distanceSquared(player, assets_.mazeExit) < 36) {
        frame.client.sendPosHpr(player.x, player.y, player.z, frame.player.headingDeg(), 0, 0);
        enteredDoor_ = frame.client.sendCogdoAction(0);
        if (enteredDoor_)
            TT_LOG("cogdo: entered open maze exit");
    }
    // Draw only the six nearest actors. Simulation and hit tests keep every suit.
    bool visible[kCogdoMazeSuits] = {};
    int nearestActors[6];
    nearestIndices(
        maze_.numSuits, [&](int index) { return game.suitHp[index] != 0; },
        [&](int index) { return distanceSquared(player, maze_.suits[index].position); }, 1e12f, nearestActors);
    for (int index : nearestActors)
        if (index >= 0)
            visible[index] = true;
    for (int index = 0; index < maze_.numSuits; ++index) {
        const auto &suit = maze_.suits[index];
        if (!visible[index]) {
            frame.suits.removeExternal(kMazeActorBase + index);
            continue;
        }
        frame.suits.setExternal(kMazeActorBase + index,
                                suit.type == 0   ? "ms"
                                : suit.type == 1 ? "nd"
                                                 : "cc",
                                0, suit.position.x, suit.position.y, suit.position.z, suit.heading, true, true,
                                false,
                                suit.type == 0   ? 2.5f
                                : suit.type == 1 ? 1.3f
                                                 : 1.33f);
    }
}
void CogdoRuntime::crane(BossFrame &frame, const net::CogdoState &game, uint32_t now) {
    if (game.phase != net::CogdoGamePhase::Playing)
        return;
    int own = -1;
    const net::CogdoState *crane = nullptr;
    for (int slot = 0; slot < net::kMaxCogdoObjects; ++slot) {
        const auto &object = frame.client.cogdoSlot(slot);
        if (!object.active || object.parent != game.doId || object.kind != net::CogdoKind::Crane ||
            object.index >= 4)
            continue;
        if (object.owner == frame.client.avatarId()) {
            own = object.index;
            crane = &object;
        } else {
            auto &cable = cables_[object.index];
            cable.y = object.armY;
            cable.heading = object.armH;
            for (int link = 0; link < 3; ++link)
                cable.links[link] = {object.links[link][0], object.links[link][1], object.links[link][2]};
        }
    }
    if (own < 0 || !crane)
        return;
    auto &cable = cables_[own];
    cable.update(frame.input.pad.dy / 150.0f, -frame.input.pad.dx / 150.0f, frame.dt);
    if (frame.input.down & KEY_B)
        magnet_ = !magnet_;
    const Vec3 magnet = cable.magnet(own);
    const bool publish = now - moveMs_ >= 100;
    if (publish) {
        float links[3][3];
        for (int link = 0; link < 3; ++link) {
            links[link][0] = cable.links[link].x;
            links[link][1] = cable.links[link].y;
            links[link][2] = cable.links[link].z;
        }
        frame.client.sendCogdoCable(crane->doId, static_cast<uint8_t>(now / 100), cable.y, cable.heading,
                                    links);
    }
    for (int slot = 0; slot < net::kMaxCogdoObjects; ++slot) {
        const auto &object = frame.client.cogdoSlot(slot);
        if (!object.active || object.parent != game.doId || object.kind != net::CogdoKind::MoneyBag ||
            object.index >= 8)
            continue;
        auto &bag = bags_[object.index];
        if (bag.id != object.doId) {
            bag = Bag{};
            bag.id = object.doId;
            bag.position = kCogdoBags[object.index].position;
        }
        if (bag.state != object.state[0]) {
            bag.state = object.state[0];
            bag.hitFloor = bag.freed = false;
        }
        if (object.owner != frame.client.avatarId()) {
            if (object.revision != bag.revision)
                bag.position = {object.x, object.y, object.z};
            if (magnet_ && !object.owner && now - actionMs_ > 500 &&
                distanceSquared(magnet, bag.position) < 36 && frame.client.sendCogdoBag(object.doId, true))
                actionMs_ = now;
        } else if (object.state[0] == 'G') {
            const Vec3 previous = bag.position;
            bag.position = {magnet.x, magnet.y, magnet.z - 4.1f};
            const float dt = std::max(.001f, frame.dt);
            bag.velocity = {(bag.position.x - previous.x) / dt, (bag.position.y - previous.y) / dt,
                            (bag.position.z - previous.z) / dt};
            if (!magnet_ && now - actionMs_ > 500 && frame.client.sendCogdoBag(object.doId, false))
                actionMs_ = now;
        } else if (object.state[0] == 'D' || object.state[0] == 's') {
            bag.velocity.z -= kCogdoGravity * frame.dt;
            bag.position.x += bag.velocity.x * frame.dt;
            bag.position.y += bag.velocity.y * frame.dt;
            bag.position.z += bag.velocity.z * frame.dt;
            if (bag.position.z <= 6) {
                bag.position.z = 6;
                bag.velocity.z = 0;
                bag.velocity.x *= std::max(0.0f, 1 - 4 * frame.dt);
                bag.velocity.y *= std::max(0.0f, 1 - 4 * frame.dt);
                if (!bag.hitFloor)
                    bag.hitFloor =
                        frame.client.sendCogdoBagMotion(object.doId, net::Client::CogdoBagAction::Floor,
                                                        bag.position.x, bag.position.y, bag.position.z, 0);
                if (!bag.freed && object.state[0] == 's' && distanceSquared(bag.velocity, {0, 0, 0}) < 1)
                    bag.freed =
                        frame.client.sendCogdoBagMotion(object.doId, net::Client::CogdoBagAction::Free,
                                                        bag.position.x, bag.position.y, bag.position.z, 0);
            }
        }
        if (publish && object.owner == frame.client.avatarId())
            frame.client.sendCogdoBagMotion(object.doId, net::Client::CogdoBagAction::Pose, bag.position.x,
                                            bag.position.y, bag.position.z, 0);
        bag.revision = object.revision;
    }
    if (publish)
        moveMs_ = now;
}
void CogdoRuntime::simulate(BossFrame &frame, const net::CogdoState &interior, const net::CogdoState *game,
                            uint32_t now) {
    if (std::strcmp(state_, interior.state) || stateMs_ != interior.stateMs) {
        std::snprintf(state_, sizeof(state_), "%s", interior.state);
        stateMs_ = interior.stateMs;
        acknowledged_ = false;
        TT_LOG("cogdo: floor %d/%d state %s", int(interior.floor), int(interior.numFloors), state_);
    }
    const int32_t age = int32_t(now - interior.stateMs);
    if (!acknowledged_) {
        using Action = net::Client::CogdoInteriorAction;
        if (!std::strcmp(state_, "WaitForAllToonsInside") && age >= 300)
            acknowledged_ = frame.client.sendCogdoInterior(Action::Joined);
        else if (!std::strcmp(state_, "Elevator") && age >= 6000)
            acknowledged_ = frame.client.sendCogdoInterior(Action::Elevator);
        else if (!std::strcmp(state_, "ReservesJoining") && age >= 3500)
            acknowledged_ = frame.client.sendCogdoInterior(Action::Reserves);
        else if (!std::strcmp(state_, "BarrelRoomIntro") && age >= 5000 && assets_.ready())
            acknowledged_ = frame.client.sendCogdoInterior(Action::BarrelIntro);
        else if (!std::strcmp(state_, "BarrelRoomReward") && age >= 5000)
            acknowledged_ = frame.client.sendCogdoInterior(Action::BarrelReward);
    }
    if (!assets_.ready())
        return;
    if (!spawned_) {
        Vec3 spawn = assets_.mazeEntrance;
        if (assets_.scene() == CogdoScene::Suite || assets_.scene() == CogdoScene::BattleFloor)
            spawn = {0, -10, 0};
        if (assets_.scene() == CogdoScene::Barrels)
            spawn = {-4, -65, 0};
        if (assets_.scene() == CogdoScene::Crane || assets_.scene() == CogdoScene::Boardroom)
            spawn = {36, -145, 6};
        frame.player.setCollision(&assets_.collision);
        frame.player.setPlatform(nullptr, 0, 0, 0, 0);
        frame.player.setDynamic(nullptr);
        frame.player.reset(spawn.x, spawn.y, spawn.z + .1f, 0);
        frame.client.sendSetParent(net::kParentRender);
        spawned_ = true;
    }
    if (game && game_ != game->doId) {
        game_ = game->doId;
        mazeReady_ = introReady_ = enteredDoor_ = balloon_ = logicFailed_ = false;
        numMemos_ = deaths_ = 0;
        for (auto &memo : memos_)
            memo = Memo{};
        for (auto &cable : cables_)
            cable.reset();
        for (auto &bag : bags_)
            bag = Bag{};
    }
    Input movement = frame.input;
    if (frame.battle.active() || (std::strcmp(state_, "Game") && std::strcmp(state_, "CollectBarrels")))
        movement = {};
    if (assets_.scene() == CogdoScene::Maze && game && game->suitCounts[0])
        maze(frame, *game, now, &movement);
    if (game && game->phase == net::CogdoGamePhase::Intro && !introReady_ &&
        (game->kind != net::CogdoKind::Maze || mazeReady_) && int32_t(now - game->stateMs) >= 24000) {
        introReady_ = frame.client.sendCogdoReady();
        if (introReady_)
            TT_LOG("cogdo: ready game %u", unsigned(game->doId));
    }
    if (assets_.scene() == CogdoScene::Crane && game) {
        movement = {};
        crane(frame, *game, now);
    }
    if (scripted(frame) && assets_.scene() == CogdoScene::Barrels && !std::strcmp(state_, "CollectBarrels")) {
        movement = {};
        if (frame.client.ownStats().hp < frame.client.ownStats().maxHp) {
            for (int slot = 0; slot < net::kMaxCogdoObjects; ++slot) {
                const auto &barrel = frame.client.cogdoSlot(slot);
                if (barrel.active && barrel.kind == net::CogdoKind::Barrel && barrel.index < 10 &&
                    barrel.state[0] == '1' && !barrel.grabbed) {
                    walkToward(frame.player, kBarrels[barrel.index].position, &movement);
                    break;
                }
            }
        }
    }
    const Vec3 before = playerPosition(frame.player);
    frame.player.update(movement, frame.dt);
    if (mazeReady_ && assets_.scene() == CogdoScene::Maze) {
        Vec3 after = playerPosition(frame.player);
        maze_.slide(before, &after);
        if (after.x != frame.player.x() || after.y != frame.player.y())
            frame.player.reset(after.x, after.y, after.z, frame.player.headingDeg());
    }
    if (assets_.scene() == CogdoScene::Barrels && !std::strcmp(state_, "CollectBarrels")) {
        const Vec3 player = playerPosition(frame.player);
        for (int slot = 0; slot < net::kMaxCogdoObjects; ++slot) {
            const auto &barrel = frame.client.cogdoSlot(slot);
            if (!barrel.active || barrel.kind != net::CogdoKind::Barrel || barrel.index >= 10 ||
                barrel.grabbed || barrel.state[0] != '1')
                continue;

            if (now - actionMs_ >= 500 && distanceSquared(player, kBarrels[barrel.index].position) < 12.25f &&
                frame.client.ownStats().hp < frame.client.ownStats().maxHp &&
                frame.client.sendCogdoBarrel(barrel.doId))
                actionMs_ = now;
            if (scripted(frame))
                break;
        }
    }
    // A Cogdo battle is the existing DistributedBattleBldg protocol. The battle
    // controller owns choices, movies, reserves, laff and rewards without shortcuts.
    if ((!std::strcmp(state_, "Battle") || !std::strcmp(state_, "BattleIntro")) && !frame.battle.active()) {
        for (int index = 0; index < net::kMaxBattles; ++index) {
            const auto &battle = frame.client.battleSlot(index);
            if (!battle.active || battle.zoneId != interior.zone || battle.bossId || battle.levelDoId)
                continue;
            frame.battle.requestJoin(frame.client, battle.doId, frame.player.x(), frame.player.y(),
                                     frame.player.z(), frame.player.headingDeg(), now);
            break;
        }
    }
    frame.battle.setAutoPlay(scripted(frame));
    frame.battle.update(frame.client, now, frame.dt, {SuitActors::poseThunk, &frame.suits});
    frame.suits.update(frame.dt, now, &assets_.collision);
    for (int index = 0; index < frame.battle.numSuitPoses(); ++index) {
        const auto &pose = frame.battle.suitPose(index);
        frame.suits.overridePose(pose.doId, pose.x, pose.y, pose.z + pose.hop, pose.h, pose.hidden, pose.flash,
                                 pose.anim, pose.clipArg, pose.clipStartMs);
    }
    frame.battle.fillView(frame.client, now, &battleView_);
    const auto &stats = frame.client.ownStats();
    frame.ui.setBattle(frame.battle.active() ? &battleView_ : nullptr,
                       {stats.inventory, stats.experience, stats.trackAccess});
    if (frame.battle.active()) {
        frame.ui.handleInput(frame.input.down, frame.input.held, frame.touch, frame.uiState);
        BattleAction action;
        while (frame.ui.battle().popAction(action))
            frame.battle.perform(frame.client, action, now);
    }
    BattleActorPose pose;
    if (frame.battle.localPose(&pose)) {
        frame.player.reset(pose.x, pose.y, pose.z + pose.hop, pose.h);
        if (pose.anim >= kBattleAnimClip0 && pose.anim < kBattleAnimClip0 + kNumBattleClips)
            frame.toon.playAction(toonAnimByName(kBattleClipNames[pose.anim - kBattleAnimClip0]),
                                  ClipMode::Once);
        else
            frame.toon.play(static_cast<ToonAnim>(pose.anim), 1);
    } else if (!frame.toon.actionActive()) {
        float rate = 1;
        frame.toon.play(frame.player.animState(&rate), rate);
    }
    frame.effects.update(frame.dt, now, &frame.battle, frame.toon.look());
    frame.toon.update(frame.dt);
    frame.remotes.update(frame.dt);
    // The shop owner is an upstream DNA record, not the SOS reward character.
    if (assets_.scene() == CogdoScene::Suite && interior.npc && ownerNpc_ != interior.npc) {
        ownerNpc_ = interior.npc;
        FILE *file = std::fopen("sdmc:/3ds/toontown3ds/data/cogdo/shop_owners.bin", "rb");
        char magic[4] = {};
        uint16_t version = 0, count = 0;
        bool valid = file && std::fread(magic, 1, 4, file) == 4 && !std::memcmp(magic, "BND1", 4) &&
                     std::fread(&version, 2, 1, file) == 1 && version == 1 &&
                     std::fread(&count, 2, 1, file) == 1 && count <= 4096;
        for (int index = 0; valid && index < count; ++index) {
            uint32_t npc = 0;
            uint8_t dna[15];
            valid = std::fread(&npc, 4, 1, file) == 1 && std::fread(dna, 1, 15, file) == 15;
            if (valid && npc == ownerNpc_) {
                ToonDna parsed;
                if (parseNetString(dna, 15, &parsed))
                    frame.cache.acquire(parsed, ToonLod::Low, &ownerLook_);
                break;
            }
        }
        if (file)
            std::fclose(file);
        ownerToon_.setLook(&ownerLook_, &frame.cache.fallback());
    }
    const float rescueAge = std::max(0.0f, int32_t(now - interior.stateMs) / 1000.0f);
    const bool rescued = !std::strcmp(state_, "Reward");
    ownerToon_.play(rescued && rescueAge >= 1.5f && rescueAge < 3 ? kAnimWalk
                    : rescued                                     ? kAnimWave
                                                                  : kAnimNeutral,
                    1);
    ownerToon_.update(frame.dt);
    if (now - moveMs_ >= 200 && assets_.scene() != CogdoScene::Crane) {
        frame.client.sendPosHpr(frame.player.x(), frame.player.y(), frame.player.z(), frame.player.headingDeg(),
                                0, 0);
        frame.client.sendAnimState(frame.player.moving() ? "run" : "neutral", 1);
        moveMs_ = now;
    }
}
void CogdoRuntime::draw(BossFrame &frame, const net::CogdoState &interior, const net::CogdoState *game,
                        uint32_t now) {
    const bool unsupported = game && game->kind == net::CogdoKind::Flying;
    C3D_FVec eye = frame.player.cameraEye(), target = frame.player.cameraTarget();
    if (assets_.scene() == CogdoScene::Maze) {
        eye = FVec3_New(frame.player.x(), frame.player.y() - 32, 48);
        target = FVec3_New(frame.player.x(), frame.player.y() + 8, 0);
    }
    if (assets_.scene() == CogdoScene::Crane || assets_.scene() == CogdoScene::Boardroom) {
        eye = FVec3_New(36, -190, 65);
        target = FVec3_New(36, -110, 6);
    }
    if (frame.battle.inBattle()) {
        frame.battle.cameraEye(&eye.x, &eye.y, &eye.z);
        frame.battle.cameraTarget(&target.x, &target.y, &target.z);
    }
    if (frame.stereoActive) {
        gfxSet3D(false);
        frame.stereoActive = false;
    }
    frame.renderer.resetStats();
    frame.renderer.beginWorld(frame.renderer.topLeft(), eye, target, 0, 0);
    if (assets_.ready()) {
        frame.renderer.drawModel(assets_.models[0], transform({0, 0, 0}));
        if (assets_.scene() == CogdoScene::Maze && mazeReady_) {
            for (int index = 0; index < 9; ++index) {
                const Vec3 origin{float(index % 3 - 1) * 51, float(index / 3 - 1) * 51, 0};
                frame.renderer.drawModel(assets_.models[1 + maze_.quadrant[index]],
                                         transform(origin, maze_.angle[index] * 90));
            }
            if (!game || !game->doorOpen)
                frame.renderer.drawModel(assets_.models[11], transform(assets_.mazeExit));
            if (balloon_)
                frame.renderer.drawModel(assets_.models[12], transform(balloonPosition_));
            if (game && game->phase == net::CogdoGamePhase::Playing) {
                const float elapsed = int32_t(now - game->startMs) / 1000.0f;
                for (const auto &drop : maze_.drops) {
                    const float age = elapsed - drop.tick / 256.0f;
                    if (age < 0 || age > 4.8f)
                        continue;
                    Vec3 position = drop.position;
                    const float fall = std::clamp(age - 2, 0.0f, 1.0f);
                    position.z = 70 * (1 - fall * fall);
                    frame.renderer.drawModel(assets_.models[14], transform(position));
                }
            }
            int shown = 0;
            for (int index = 0; game && index < numMemos_ && shown < 8; ++index) {
                if (game->pickups[index] ||
                    distanceSquared(playerPosition(frame.player), memos_[index].position) > 900)
                    continue;
                frame.renderer.drawModel(assets_.models[13], transform(memos_[index].position));
                ++shown;
            }
        } else if (assets_.scene() == CogdoScene::Suite) {
            // DistributedCogdoInterior.cageHeights: 11.36 -> .01. Cage geometry
            // retains its original transform; apply only the lowering delta.
            const bool reward = !std::strcmp(interior.state, "Reward");
            const float fraction =
                reward ? std::clamp(int32_t(now - interior.stateMs) / 750.0f, 0.0f, 1.0f) : 0;
            const Vec3 offset{0, 0, -11.35f * fraction};
            frame.renderer.drawModel(assets_.models[1], transform(offset));
            // CageOpenMovie: lower .75 s, pitch the door 90 degrees over .5 s,
            // then let the shop owner walk out over 1.5 s.
            const float age = std::max(0.0f, int32_t(now - interior.stateMs) / 1000.0f);
            const float opening = reward ? std::clamp((age - .75f) / .5f, 0.0f, 1.0f) : 0;
            C3D_Mtx door = transform({assets_.cageDoor.x, assets_.cageDoor.y, assets_.cageDoor.z + offset.z});
            Mtx_RotateX(&door, opening * 90 * kDegrees, true);
            Mtx_Translate(&door, -assets_.cageDoor.x, -assets_.cageDoor.y, -assets_.cageDoor.z, true);
            frame.renderer.drawModel(assets_.models[2], door);
            Vec3 owner{assets_.cage.x, assets_.cage.y - 2, 11.36f - 11.35f * fraction};
            const float walking = reward ? std::clamp((age - 1.5f) / 1.5f, 0.0f, 1.0f) : 0;
            owner.x *= 1 - walking;
            owner.y += (10 - owner.y) * walking;
            owner.z *= 1 - walking;
            ownerToon_.draw(frame.renderer, transform(owner, 180));
        } else if (assets_.scene() == CogdoScene::Barrels) {
            for (int slot = 0; slot < net::kMaxCogdoObjects; ++slot) {
                const auto &barrel = frame.client.cogdoSlot(slot);
                if (!barrel.active || barrel.kind != net::CogdoKind::Barrel || barrel.index >= 10 ||
                    barrel.state[0] == '0' || barrel.grabbed)
                    continue;
                frame.renderer.drawModel(assets_.models[1], transform(kBarrels[barrel.index].position,
                                                                      kBarrels[barrel.index].heading));
            }
        } else if (assets_.scene() == CogdoScene::Crane) {
            for (int index = 0; index < 4; ++index) {
                const auto &root = kCogdoCranes[index];
                const auto &cable = cables_[index];
                frame.renderer.drawModel(assets_.models[1],
                                         transform(root.position, root.heading + cable.heading));
                frame.renderer.drawModel(assets_.models[2], transform(cable.magnet(index), root.heading));
                frame.renderer.drawModel(assets_.models[3], transform(root.position, root.heading));
                frame.renderer.drawModel(assets_.models[4], transform(root.position, root.heading));
            }
            for (const auto &bag : bags_)
                if (bag.id)
                    frame.renderer.drawModel(assets_.models[5], transform(bag.position));
        }
        C3D_Mtx toon;
        frame.player.worldMatrix(&toon);
        frame.toon.draw(frame.renderer, toon);
        frame.remotes.draw(frame.renderer, frame.player.x(), frame.player.y());
        frame.suits.draw(frame.renderer);
        frame.effects.draw(frame.renderer, &frame.battle, frame.toon.look());
    }
    frame.renderer.endWorld();
    prof::lap(prof::kCogdoDraw);
    frame.stats = frame.renderer.stats();
    ui::Context context{};
    context.stats = &frame.client.ownStats();
    const bool failed = assets_.failed() || logicFailed_;
    std::snprintf(title_, sizeof(title_), "%s",
                  unsupported        ? "Flying prototype unsupported"
                  : failed           ? "Cogdo assets missing / malformed"
                  : !assets_.ready() ? "Loading field office"
                                     : interior.state);
    std::snprintf(status_, sizeof(status_), "Floor %d/%d  Laff %d/%d  Memos %u", int(interior.floor) + 1,
                  int(interior.numFloors) + 1, frame.client.ownStats().hp, frame.client.ownStats().maxHp,
                  game ? unsigned(game->memos) : 0);
    const float limit = game && game->kind == net::CogdoKind::Maze        ? 240
                        : game && game->kind == net::CogdoKind::Boardroom ? 60
                                                                          : 180;
    const float remaining = game && game->phase == net::CogdoGamePhase::Playing
                                ? std::max(0.0f, limit - int32_t(now - game->startMs) / 1000.0f)
                                : limit;
    std::snprintf(detail_, sizeof(detail_), "%s  %.0fs  %s",
                  game && game->hasGag ? "Balloon ready" : "Find a water cooler", remaining,
                  game && game->doorOpen ? "EXIT OPEN" : "");
    if (assets_.scene() == CogdoScene::Barrels) {
        int collected = 0;
        for (int slot = 0; slot < net::kMaxCogdoObjects; ++slot) {
            const auto &barrel = frame.client.cogdoSlot(slot);
            if (barrel.active && barrel.kind == net::CogdoKind::Barrel && barrel.grabbed)
                ++collected;
        }
        const float seconds = std::max(0.0f, 30 - int32_t(now - interior.stateMs) / 1000.0f);
        std::snprintf(detail_, sizeof(detail_), "Barrels %d/10  %.0fs  Walk near barrels to heal", collected,
                      seconds);
    } else if (assets_.scene() == CogdoScene::Crane) {
        std::snprintf(detail_, sizeof(detail_), "Forced crane prototype  %.0fs  Magnet %s", remaining,
                      magnet_ ? "ON" : "OFF");
    } else if (assets_.scene() == CogdoScene::Boardroom) {
        std::snprintf(detail_, sizeof(detail_), "Forced boardroom prototype  %.0fs", remaining);
    } else if (assets_.scene() == CogdoScene::Suite || assets_.scene() == CogdoScene::BattleFloor) {
        std::snprintf(detail_, sizeof(detail_), "%s",
                      assets_.scene() == CogdoScene::Suite ? "Executive suite - rescue the shop owner"
                                                           : "Cog battle floor");
    }
    if (!std::strcmp(interior.state, "Reward")) {
        const char *name = "SOS card";
        for (const auto &card : ui::kSosCards)
            if (card.npcId == interior.sos)
                name = card.name;
        std::snprintf(detail_, sizeof(detail_), "Rescued shop owner - %s awarded by AI", name);
    }
    context.infoTitle = title_;
    context.infoLines[0] = status_;
    context.infoLines[1] = detail_;
    context.infoLines[2] = unsupported ? "Missing upstream flying models. B: return"
                           : failed    ? "Copy data/cogdo from bake. B: return"
                           : !std::strcmp(interior.state, "Reward") ? "A: return to the street"
                           : assets_.scene() == CogdoScene::Crane   ? "Pad: crane arm  B: magnet / release"
                                                                    : "Pad: move  A: jump  B: throw balloon";
    context.heapUsedKB = mallinfo().uordblks / 1024;
    context.linearFreeKB = linearSpaceFree() / 1024;
    context.vramFreeKB = vramSpaceFree() / 1024;
    frame.ui.draw(frame.bottom, frame.uiState, context);
}
bool CogdoRuntime::frame(BossFrame &frame) {
    const uint32_t now = net::plat::nowMs();
    if (!active_) {
        board(frame);
        const auto *interior = frame.client.cogdoInterior();
        if (!interior || !frame.client.cogdoParticipant())
            return false;
        active_ = true;
        interior_ = interior->doId;
        exterior_ = interior->exterior;
        building_ = interior->parent;
        state_[0] = 0;
        stateMs_ = 0;
        spawned_ = false;
        mazeReady_ = introReady_ = logicFailed_ = false;
        game_ = 0;
        // Keep the real street anchor for the return, including manual boarding.
        if (const auto *building = frame.client.findCogBuilding(building_)) {
            char name[32];
            std::snprintf(name, sizeof(name), "tb%u", unsigned(building->block));
            if (const auto *anchor = frame.place.point(name)) {
                exitHeading_ = anchor->h;
                exit_ = {anchor->x + 15.463f * std::cos(anchor->h * kDegrees),
                         anchor->y + 15.463f * std::sin(anchor->h * kDegrees), anchor->z};
                exitValid_ = true;
            }
        }
        C3D_FrameSync();
        frame.place.unload();
        frame.battle.reset();
        frame.remotes.clear();
        frame.suits.setZone(frame.client.zoneId());
        frame.ui.book().close();
        TT_LOG("cogdo: entered interior %u exterior %u", unsigned(interior_), unsigned(exterior_));
    }
    frame.client.update(now);
    net::Event event;
    while (frame.client.pollEvent(event)) {
        frame.battle.handleEvent(event, frame.client, now);
        frame.remotes.handleEvent(event, frame.client);
        frame.suits.handleEvent(event, frame.client, now);
    }
    const auto *interior = frame.client.cogdoInterior();
    const auto *game = frame.client.cogdoGame();
    const bool unsupported = game && game->kind == net::CogdoKind::Flying;
    const bool reward = interior && !std::strcmp(interior->state, "Reward");
    if (!interior || interior->doId != interior_ || frame.client.ownStats().hp <= 0 ||
        (interior && !std::strcmp(interior->state, "Failed")) ||
        ((assets_.failed() || logicFailed_ || unsupported) && (frame.input.down & KEY_B)) ||
        (reward && int32_t(now - interior->stateMs) > 8000 &&
         ((frame.input.down & KEY_A) || scripted(frame)))) {
        leave(frame);
        return false;
    }
    const CogdoScene scene = select(*interior, game);
    if (scene != assets_.scene()) {
        C3D_FrameSync();
        frame.player.setCollision(nullptr);
        assets_.select(scene);
        spawned_ = false;
        for (int index = 0; index < kCogdoMazeSuits; ++index)
            frame.suits.removeExternal(kMazeActorBase + index);
    }
    simulate(frame, *interior, game, now);
    prof::lap(prof::kCogdoUpdate);
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    prof::lap(prof::kWait);
    frame.cache.pump();
    assets_.pump();
    if (frame.shot) {
        char path[160];
        std::snprintf(path, sizeof(path), "sdmc:/3ds/toontown3ds/shot_%s_top.bin", frame.shot);
        shot::dumpTarget(frame.renderer.topLeft(), path);
        std::snprintf(path, sizeof(path), "sdmc:/3ds/toontown3ds/shot_%s_bottom.bin", frame.shot);
        shot::dumpTarget(frame.bottom, path);
        frame.shot = nullptr;
    }
    prof::lap(prof::kLoads);
    draw(frame, *interior, game, now);
    frame.commandUsage = C3D_GetCmdBufUsage();
    prof::lap(prof::kBottomUi);
    C3D_FrameEnd(0);
    prof::lap(prof::kFrameEnd);
    audio::update();
    prof::lap(prof::kAudio);
    return true;
}
} // namespace tt::world
