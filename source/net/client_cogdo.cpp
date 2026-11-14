#include "core/log.h"
#include "net/client.h"
#include "net/msg_types.h"
#include <cmath>
#include <cstring>
namespace tt::net {
#define TT_NET_UPDATE(writer, id, cls, field)                                                                  \
    static_assert((dc::cls::kw_##field & dc::kKwClsend) != 0, #cls "." #field " must be clsend");              \
    beginUpdate(writer, id, dc::cls::k_##field)

const CogdoState *Client::cogdoInterior() const {
    for (const CogdoState &object : cogdoObjects_)
        if (object.active && object.kind == CogdoKind::Interior)
            return &object;
    return nullptr;
}
const CogdoState *Client::cogdoGame() const {
    const CogdoState *interior = cogdoInterior();
    for (const CogdoState &object : cogdoObjects_) {
        if (object.active && interior && object.parent == interior->doId &&
            (object.kind == CogdoKind::Maze || object.kind == CogdoKind::Flying ||
             object.kind == CogdoKind::CraneGame || object.kind == CogdoKind::Boardroom))
            return &object;
    }
    return nullptr;
}
const CogdoState *Client::findCogdo(uint32_t id) const {
    for (const CogdoState &object : cogdoObjects_)
        if (object.active && object.doId == id)
            return &object;
    return nullptr;
}
bool Client::cogdoParticipant() const {
    const CogdoState *interior = cogdoInterior();
    if (state_ != State::InZone || !interior)
        return false;
    // The AI sends setToons only after WaitForAllToonsInside (it is not a required field), so
    // until then an interior in our own zone means we rode its elevator in: upstream sends
    // setAvatarJoined from announceGenerate, and DistributedCogdoInteriorAI checks membership.
    if (interior->numToons == 0)
        return interior->zone == cfg_.zoneId;
    for (int index = 0; index < interior->numToons; ++index)
        if (interior->toons[index] == avatarId_)
            return true;
    return false;
}
bool Client::handleCogdoEnter(uint32_t id, uint16_t classId, DatagramReader &reader, const uint16_t *required,
                              uint16_t count, bool other) {
    const CogdoKind kind = cogdoKind(classId);
    if (kind == CogdoKind::None)
        return false;
    CogdoState next;
    next.doId = id;
    next.classId = classId;
    next.kind = kind;
    bool valid = true;
    uint16_t field = 0;
    for (uint16_t index = 0; index < count && valid; ++index) {
        field = required[index];
        valid = applyCogdoField(next, field, reader, now_, avatarId_);
    }
    if (other && valid) {
        const uint16_t fields = reader.u16();
        for (uint16_t index = 0; index < fields && valid && reader.ok(); ++index) {
            field = reader.u16();
            valid = applyCogdoField(next, field, reader, now_, avatarId_);
        }
    }
    if (!valid || !reader.ok()) {
        TT_LOG("cogdo: dropped object %u class %u: field %u (%s) failed to parse", unsigned(id),
               unsigned(classId), unsigned(field), fieldName(field));
        return true;
    }
    CogdoState *destination = nullptr;
    int used = 0;
    for (CogdoState &object : cogdoObjects_) {
        if (object.active && object.doId == id)
            destination = &object;
        if (object.active && object.kind == kind)
            ++used;
    }
    const int limit = kind == CogdoKind::Barrel     ? 10
                      : kind == CogdoKind::Crane    ? 4
                      : kind == CogdoKind::MoneyBag ? 8
                                                    : 1;
    if (!destination && used >= limit) {
        TT_LOG("cogdo: dropped object %u class %u: %d of this kind already present", unsigned(id),
               unsigned(classId), used);
        return true;
    }
    for (CogdoState &object : cogdoObjects_)
        if (!destination && !object.active)
            destination = &object;
    if (!destination) {
        TT_LOG("cogdo: dropped object %u class %u: pool full", unsigned(id), unsigned(classId));
        return true;
    }
    if (timeSynced_ && next.kind == CogdoKind::Interior)
        next.stateMs = networkToLocal(next.stamp, now_);
    next.active = true;
    *destination = next;
    TT_LOG("cogdo: object %u class %u zone %u toons %u state %s", unsigned(id), unsigned(classId),
           unsigned(next.zone), unsigned(next.numToons), next.kind == CogdoKind::Interior ? next.state : "-");
    return true;
}
bool Client::handleCogdoField(DatagramReader &reader, uint32_t id, uint16_t field) {
    for (CogdoState &object : cogdoObjects_) {
        if (!object.active || object.doId != id)
            continue;
        if (applyCogdoField(object, field, reader, now_, avatarId_) && timeSynced_) {
            const char *name = fieldName(field);
            if (!std::strcmp(name, "setState") && object.kind == CogdoKind::Interior)
                object.stateMs = networkToLocal(object.stamp, now_);
            if (!std::strcmp(name, "setGameStart"))
                object.startMs = networkToLocal(object.stamp, now_);
            if (!std::strcmp(name, "setGameFinish"))
                object.finishMs = networkToLocal(object.stamp, now_);
        }
        const char *name = fieldName(field);
        if (!std::strcmp(name, "hasGag") || !std::strcmp(name, "suitHitByGag") ||
            !std::strcmp(name, "doAction") || !std::strcmp(name, "setGameStart") ||
            !std::strcmp(name, "setGameFinish"))
            TT_LOG("cogdo: %s object %u action %u memos %u", name, unsigned(id), unsigned(object.action),
                   unsigned(object.memos));
        return true;
    }
    return false;
}
void Client::cogdoLeaving(uint32_t id) {
    for (CogdoState &object : cogdoObjects_)
        if (object.active && object.doId == id)
            object = CogdoState{};
}
void Client::clearCogdo() {
    for (CogdoState &object : cogdoObjects_)
        object = CogdoState{};
}
bool Client::sendCogdoInterior(CogdoInteriorAction action) {
    const CogdoState *interior = cogdoInterior();
    if (!cogdoParticipant())
        return false;
    const char *expected = action == CogdoInteriorAction::Joined         ? "WaitForAllToonsInside"
                           : action == CogdoInteriorAction::Elevator     ? "Elevator"
                           : action == CogdoInteriorAction::Reserves     ? "ReservesJoining"
                           : action == CogdoInteriorAction::BarrelIntro  ? "BarrelRoomIntro"
                           : action == CogdoInteriorAction::BarrelReward ? "BarrelRoomReward"
                                                                         : "CollectBarrels";
    if (std::strcmp(interior->state, expected))
        return false;
    DatagramWriter writer = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    switch (action) {
    case CogdoInteriorAction::Joined:
        TT_NET_UPDATE(writer, interior->doId, DistributedCogdoInterior, setAvatarJoined);
        break;
    case CogdoInteriorAction::Elevator:
        TT_NET_UPDATE(writer, interior->doId, DistributedCogdoInterior, elevatorDone);
        break;
    case CogdoInteriorAction::Reserves:
        TT_NET_UPDATE(writer, interior->doId, DistributedCogdoInterior, reserveJoinDone);
        break;
    case CogdoInteriorAction::BarrelIntro:
        TT_NET_UPDATE(writer, interior->doId, DistributedCogdoInterior, toonBarrelRoomIntroDone);
        break;
    case CogdoInteriorAction::BarrelReward:
        TT_NET_UPDATE(writer, interior->doId, DistributedCogdoInterior, toonBarrelRoomRewardDone);
        break;
    case CogdoInteriorAction::LeaveBarrels:
        TT_NET_UPDATE(writer, interior->doId, DistributedCogdoInterior, toonLeftBarrelRoom);
        break;
    }
    return finish(writer);
}
bool Client::sendCogdoVictor(uint32_t id) {
    const CogBuilding *building = findCogBuilding(id);
    if (state_ != State::InZone || !building || !building->cogdoVictory)
        return false;
    bool victor = false;
    for (int index = 0; index < building->numVictors; ++index)
        victor |= building->victors[index] == avatarId_;
    if (!victor)
        return false;
    DatagramWriter writer = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(writer, id, DistributedBuilding, setVictorReady);
    return finish(writer);
}
bool Client::sendVictorReady(uint32_t id) {
    const CogBuilding *building = findCogBuilding(id);
    if (state_ != State::InZone || !building || !building->suitVictory)
        return false;
    bool victor = false;
    for (int index = 0; index < building->numVictors; ++index)
        victor |= building->victors[index] == avatarId_;
    if (!victor)
        return false;
    DatagramWriter writer = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(writer, id, DistributedBuilding, setVictorReady);
    return finish(writer);
}
bool Client::sendCogdoReady() {
    const CogdoState *game = cogdoGame();
    if (!cogdoParticipant() || !game || game->phase != CogdoGamePhase::Intro)
        return false;
    DatagramWriter writer = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(writer, game->doId, DistCogdoGame, setAvatarReady);
    return finish(writer);
}
bool Client::sendCogdoAction(uint8_t action, uint32_t data) {
    const CogdoState *game = cogdoGame();
    if (!cogdoParticipant() || !game || game->phase != CogdoGamePhase::Playing)
        return false;
    DatagramWriter writer = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    if (game->kind == CogdoKind::Maze) {
        if ((action != 0 && action != 1) || (action == 0 && (!game->doorOpen || game->enteredDoor)))
            return false;
        TT_NET_UPDATE(writer, game->doId, DistCogdoMazeGame, requestAction);
        writer.u8(action);
        writer.u32(data);
    } else if (game->kind == CogdoKind::Flying) {
        // GotoWinState (2) is an AI broadcast, never a legal client request.
        if (action == 2 || action > 13 || data > 255 || (action == 1 && game->action != 2))
            return false;
        TT_NET_UPDATE(writer, game->doId, DistCogdoFlyingGame, requestAction);
        writer.u8(action);
        writer.u8(static_cast<uint8_t>(data));
    } else
        return false;
    return finish(writer);
}
bool Client::sendCogdoGag(float x, float y, float heading) {
    const CogdoState *game = cogdoGame();
    if (!cogdoParticipant() || !game || game->kind != CogdoKind::Maze ||
        game->phase != CogdoGamePhase::Playing || !game->hasGag)
        return false;
    const float values[] = {x, y, heading};
    for (float value : values)
        if (!std::isfinite(value) || std::fabs(value) > 3276)
            return false;
    DatagramWriter writer = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(writer, game->doId, DistCogdoMazeGame, requestUseGag);
    for (float value : values)
        writer.i16(static_cast<int16_t>(std::round(value * 10)));
    writer.i16(networkTime(now_));
    return finish(writer);
}
bool Client::sendCogdoSuit(uint8_t type, uint8_t serial, bool hitByGag) {
    const CogdoState *game = cogdoGame();
    if (!cogdoParticipant() || !game || game->kind != CogdoKind::Maze || game->phase != CogdoGamePhase::Playing)
        return false;
    const unsigned total = game->suitCounts[0] + game->suitCounts[1] + game->suitCounts[2];
    const unsigned expected = serial < game->suitCounts[0]                         ? 0
                              : serial < game->suitCounts[0] + game->suitCounts[1] ? 1
                                                                                   : 2;
    if (serial >= total || serial >= kMaxCogdoSuits || !game->suitHp[serial] || type != expected)
        return false;
    DatagramWriter writer = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    if (hitByGag) {
        TT_NET_UPDATE(writer, game->doId, DistCogdoMazeGame, requestSuitHitByGag);
    } else {
        TT_NET_UPDATE(writer, game->doId, DistCogdoMazeGame, requestHitBySuit);
    }
    writer.u8(type);
    writer.u8(serial);
    if (!hitByGag)
        writer.i16(networkTime(now_));
    return finish(writer);
}
bool Client::sendCogdoPickup(uint16_t index, uint8_t type) {
    const CogdoState *game = cogdoGame();
    if (!cogdoParticipant() || !game || game->phase != CogdoGamePhase::Playing || index >= 256 ||
        game->pickups[index])
        return false;
    DatagramWriter writer = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    if (game->kind == CogdoKind::Maze) {
        TT_NET_UPDATE(writer, game->doId, DistCogdoMazeGame, requestPickUp);
        writer.u8(static_cast<uint8_t>(index));
    } else if (game->kind == CogdoKind::Flying) {
        TT_NET_UPDATE(writer, game->doId, DistCogdoFlyingGame, requestPickUp);
        writer.u16(index);
        writer.u8(type);
    } else
        return false;
    return finish(writer);
}
bool Client::sendCogdoCooler(uint8_t index) {
    const CogdoState *game = cogdoGame();
    if (!cogdoParticipant() || !game || game->kind != CogdoKind::Maze ||
        game->phase != CogdoGamePhase::Playing || game->hasGag)
        return false;
    DatagramWriter writer = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(writer, game->doId, DistCogdoMazeGame, requestGag);
    writer.u8(index);
    return finish(writer);
}
bool Client::sendCogdoBarrel(uint32_t id) {
    const CogdoState *interior = cogdoInterior();
    const CogdoState *barrel = findCogdo(id);
    if (!cogdoParticipant() || !interior || std::strcmp(interior->state, "CollectBarrels") || !barrel ||
        barrel->kind != CogdoKind::Barrel || barrel->state[0] != '1' || barrel->grabbed)
        return false;
    DatagramWriter writer = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(writer, id, DistributedCogdoBarrel, requestGrab);
    return finish(writer);
}
bool Client::sendCogdoCable(uint32_t id, uint8_t sequence, float armY, float heading, const float links[3][3]) {
    const CogdoState *crane = findCogdo(id);
    const CogdoState *game = cogdoGame();
    if (!cogdoParticipant() || !crane || !game || game->phase != CogdoGamePhase::Playing ||
        crane->parent != game->doId || crane->kind != CogdoKind::Crane || crane->owner != avatarId_ ||
        crane->state[0] != 'C' || !std::isfinite(armY) || armY < 0 || armY > 25 || !std::isfinite(heading) ||
        std::fabs(heading) > 360)
        return false;
    for (int link = 0; link < 3; ++link)
        for (float value : links[link])
            if (!std::isfinite(value) || std::fabs(value) > 3276)
                return false;
    DatagramWriter writer = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(writer, id, DistCogdoCrane, setCablePos);
    writer.u8(sequence);
    writer.i16(static_cast<int16_t>(std::round(armY * 100)));
    writer.u16(static_cast<uint16_t>(std::round(std::fmod(heading + 360, 360) * 100)));
    for (int link = 0; link < 3; ++link)
        for (float value : links[link])
            writer.i16(static_cast<int16_t>(std::round(value * 10)));
    writer.i16(networkTime(now_));
    return finish(writer);
}
bool Client::sendCogdoBag(uint32_t id, bool grab) {
    const CogdoState *game = cogdoGame();
    const CogdoState *bag = findCogdo(id);
    if (!cogdoParticipant() || !game || game->phase != CogdoGamePhase::Playing || !bag ||
        bag->parent != game->doId || bag->kind != CogdoKind::MoneyBag)
        return false;
    bool ownsCrane = false;
    for (const CogdoState &crane : cogdoObjects_)
        ownsCrane |= crane.active && crane.kind == CogdoKind::Crane && crane.parent == game->doId &&
                     crane.owner == avatarId_;
    if (!ownsCrane || (grab ? bag->owner != 0 : bag->owner != avatarId_ || bag->state[0] != 'G'))
        return false;
    DatagramWriter writer = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    if (grab) {
        TT_NET_UPDATE(writer, id, DistCogdoCraneObject, requestGrab);
    } else {
        TT_NET_UPDATE(writer, id, DistCogdoCraneObject, requestDrop);
    }
    return finish(writer);
}
bool Client::sendCogdoBagMotion(uint32_t id, CogdoBagAction action, float x, float y, float z, float h) {
    const CogdoState *bag = findCogdo(id);
    const CogdoState *game = cogdoGame();
    if (!cogdoParticipant() || !game || game->phase != CogdoGamePhase::Playing || !bag ||
        bag->kind != CogdoKind::MoneyBag || bag->parent != game->doId)
        return false;
    const float values[] = {x, y, z, h};
    for (float value : values)
        if (!std::isfinite(value) || std::fabs(value) > 3276)
            return false;
    if (action == CogdoBagAction::Initial ? bag->owner != 0 : bag->owner != avatarId_)
        return false;
    if (action == CogdoBagAction::Floor && bag->state[0] != 'D')
        return false;
    if (action == CogdoBagAction::Free && bag->state[0] != 's')
        return false;
    DatagramWriter writer = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    if (action == CogdoBagAction::Pose) {
        if (bag->state[0] != 'G' && bag->state[0] != 'D' && bag->state[0] != 's')
            return false;
        TT_NET_UPDATE(writer, id, DistCogdoCraneObject, setSmXYZH);
        for (float value : values)
            writer.i16(static_cast<int16_t>(std::round(value * 10)));
        writer.i16(networkTime(now_));
    } else if (action == CogdoBagAction::Floor) {
        TT_NET_UPDATE(writer, id, DistCogdoCraneObject, hitFloor);
    } else if (action == CogdoBagAction::Free) {
        TT_NET_UPDATE(writer, id, DistCogdoCraneObject, requestFree);
        writer.i16(static_cast<int16_t>(std::round(x * 10)));
        writer.i16(static_cast<int16_t>(std::round(y * 10)));
        writer.i16(static_cast<int16_t>(std::round(z * 10)));
        writer.u16(static_cast<uint16_t>(std::round(std::fmod(h + 360, 360) * 100)));
    } else {
        TT_NET_UPDATE(writer, id, DistCogdoCraneMoneyBag, requestInitial);
    }
    return finish(writer);
}
} // namespace tt::net
