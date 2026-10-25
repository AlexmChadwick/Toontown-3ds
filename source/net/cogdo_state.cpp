#include "net/cogdo_state.h"
#include <cstring>
namespace tt::net {
namespace {
bool ids(DatagramReader& reader, uint32_t* output, uint8_t* count, unsigned capacity) {
    const uint16_t bytes = reader.u16();
    if (bytes % 4 || bytes > capacity * 4) {
        reader.fail();
        return false;
    }
    *count = bytes / 4;
    for (unsigned index = 0; index < *count; ++index) output[index] = reader.u32();
    return reader.ok();
}
}
CogdoKind cogdoKind(uint16_t classId) {
    if (classId == dc::DistributedCogdoInterior::kClassId) return CogdoKind::Interior;
    if (classId == dc::DistCogdoMazeGame::kClassId) return CogdoKind::Maze;
    if (classId == dc::DistCogdoFlyingGame::kClassId) return CogdoKind::Flying;
    if (classId == dc::DistCogdoCraneGame::kClassId) return CogdoKind::CraneGame;
    if (classId == dc::DistCogdoBoardroomGame::kClassId) return CogdoKind::Boardroom;
    if (classId == dc::DistributedCogdoBarrel::kClassId) return CogdoKind::Barrel;
    if (classId == dc::DistCogdoCrane::kClassId) return CogdoKind::Crane;
    if (classId == dc::DistCogdoCraneMoneyBag::kClassId) return CogdoKind::MoneyBag;
    if (classId == dc::DistCogdoCraneCog::kClassId) return CogdoKind::Cog;
    return CogdoKind::None;
}
bool applyCogdoField(CogdoState& state, uint16_t field, DatagramReader& reader, uint32_t nowMs, uint32_t ownId) {
    const dc::ClassDesc* cls = findClass(state.classId);
    if (!cls || !classHasField(*cls, field)) {
        reader.fail();
        return false;
    }
    CogdoState next = state;
    const char* name = fieldName(field);
    const auto named = [name](const char* expected) { return !std::strcmp(name, expected); };
    if (named("setZoneId")) next.zone = reader.u32();
    else if (named("setExtZoneId") || named("setExteriorZone")) next.exterior = reader.u32();
    else if (named("setDistBldgDoId") || named("setInteriorId") || named("setCraneGameId") || named("setGameId")) next.parent = reader.u32();
    else if (named("setNumFloors")) {
        next.numFloors = reader.i8();
        if (next.numFloors < 0 || next.numFloors > 8) reader.fail();
    } else if (named("setShopOwnerNpcId")) next.npc = reader.u32();
    else if (named("setSOSNpcId")) next.sos = reader.u32();
    else if (named("setFOType")) next.foType = reader.i8();
    else if (named("setDifficultyOverrides")) {
        next.difficulty = reader.i32();
        next.playground = reader.i32();
    } else if (named("setToons")) {
        ids(reader, next.toons, &next.numToons, 4);
        reader.u16();
    } else if (named("setSuits")) {
        ids(reader, next.suits, &next.numSuits, 32);
        ids(reader, next.reserves, &next.numReserves, 32);
        const uint16_t bytes = reader.u16();
        if (bytes != next.numReserves * 2) reader.fail();
        for (unsigned index = 0; index < bytes / 2 && reader.ok(); ++index) reader.u16();
    } else if (named("setState")) {
        next.stateMs = nowMs;
        if (next.kind == CogdoKind::Interior) {
            reader.string(next.state, sizeof(next.state));
            next.stamp = reader.i16();
        } else if (next.kind == CogdoKind::Barrel) {
            const uint32_t value = reader.u32();
            if (value > 3) reader.fail();
            next.state[0] = static_cast<char>('0' + value);
            if (value == 1) next.grabbed = false;
        } else {
            next.state[0] = static_cast<char>(reader.u8());
            next.owner = reader.u32();
        }
    } else if (named("setIndex")) {
        next.index = next.kind == CogdoKind::Barrel ? reader.u32() : reader.u8();
        const unsigned limit = next.kind == CogdoKind::Barrel ? 10 : next.kind == CogdoKind::MoneyBag ? 8 : 4;
        if (next.index >= limit) reader.fail();
    } else if (named("setVisible")) next.phase = CogdoGamePhase::Visible;
    else if (named("setIntroStart")) {
        next.phase = CogdoGamePhase::Intro;
        next.stateMs = nowMs;
    } else if (named("setGameStart")) {
        next.stamp = reader.i16();
        next.startMs = nowMs;
        next.phase = CogdoGamePhase::Playing;
    } else if (named("setGameFinish")) {
        next.stamp = reader.i16();
        next.finishMs = nowMs;
        next.phase = CogdoGamePhase::Finished;
    } else if (named("setNumSuits")) {
        unsigned total = 0;
        for (uint8_t& count : next.suitCounts) {
            count = reader.u8();
            total += count;
        }
        if (total > kMaxCogdoSuits) reader.fail();
        for (unsigned index = 0; index < total && index < kMaxCogdoSuits; ++index) next.suitHp[index] = index < next.suitCounts[0] ? 2 : 1;
    } else if (named("doAction")) {
        next.action = reader.u8();
        next.actionData = reader.u32();
        if (next.kind == CogdoKind::Maze) {
            next.stamp = reader.i16();
            if (next.action == 2) next.doorOpen = true;
            if (next.action == 0 && next.actionData == ownId) next.enteredDoor = true;
        }
        ++next.actionRevision;
    } else if (named("suitHitByGag")) {
        reader.u32();
        const uint8_t type = reader.u8();
        const uint8_t index = reader.u8();
        const unsigned total = next.suitCounts[0] + next.suitCounts[1] + next.suitCounts[2];
        if (type >= 3 || index >= total || index >= kMaxCogdoSuits) reader.fail();
        else if (next.suitHp[index]) --next.suitHp[index];
    } else if (named("pickUp")) {
        reader.u32();
        const unsigned index = next.kind == CogdoKind::Flying ? reader.u16() : reader.u8();
        next.stamp = reader.i16();
        if (index >= 256) reader.fail();
        else if (!next.pickups[index]) {
            next.pickups[index] = true;
            ++next.memos;
        }
    } else if (named("hasGag")) {
        const uint32_t toon = reader.u32();
        next.stamp = reader.i16();
        if (toon == ownId) next.hasGag = true;
    } else if (named("toonUsedGag")) {
        const uint32_t toon = reader.u32();
        reader.i16();
        reader.i16();
        reader.i16();
        next.stamp = reader.i16();
        if (toon == ownId) {
            next.hasGag = false;
            next.gagMs = nowMs;
        }
    } else if (named("setGrab")) {
        next.owner = reader.u32();
        if (next.owner == ownId) next.grabbed = true;
    } else if (named("setObjectState")) {
        next.state[0] = static_cast<char>(reader.u8());
        next.owner = reader.u32();
        next.crane = reader.u32();
    } else if (named("setCablePos")) {
        next.sequence = reader.u8();
        next.armY = reader.i16() / 100.0f;
        next.armH = reader.u16() / 100.0f;
        for (auto& link : next.links) for (float& value : link) value = reader.i16() / 10.0f;
        next.stamp = reader.i16();
    } else if (named("setDNAString")) {
        const Bytes value = reader.blob();
        if (value.len >= sizeof(next.dna)) reader.fail();
        else if (reader.ok()) std::memcpy(next.dna, value.data, value.len);
    } else if (named("setSpawnInfo")) {
        next.index = reader.u8();
        next.stamp = reader.i16();
    } else if (named("setPosHpr") || named("setPos") || named("setHpr")) {
        if (!named("setHpr")) {
            next.x = reader.i16() / 10.0f;
            next.y = reader.i16() / 10.0f;
            next.z = reader.i16() / 10.0f;
        }
        if (!named("setPos")) {
            next.h = reader.i16() / 10.0f;
            next.p = reader.i16() / 10.0f;
            next.r = reader.i16() / 10.0f;
        }
    } else if (named("setBarrelRoomReward")) {
        uint8_t count = 0;
        ids(reader, next.rewards, &count, 4);
        const Bytes values = reader.blob();
        if (values.len != count) reader.fail();
        else if (reader.ok()) std::memcpy(next.rewardValues, values.data, values.len);
    } else if (!skipField(reader, field)) return false;
    if (!reader.ok()) return false;
    ++next.revision;
    state = next;
    return true;
}
} // namespace tt::net
