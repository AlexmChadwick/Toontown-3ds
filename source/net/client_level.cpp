// Facility level objects (docs/design/levels.md): the levels themselves (DistributedFactory and
// the mint / stage / country club rooms, all DistributedLevels), the floor objects of the
// multi-room facilities (DistributedMint / Stage / CountryClub), their cogs
// (DistributedFactorySuit and subclasses), battle blockers, entities, and the level
// visibility interest.
#include "net/client.h"

#include <cmath>
#include <cstring>

#include "core/log.h"
#include "net/msg_types.h"

namespace tt::net {

#define TT_NET_UPDATE(w, doId, cls, field) \
    static_assert((dc::cls::kw_##field & dc::kKwClsend) != 0, #cls "." #field " is not clsend"); \
    beginUpdate(w, doId, dc::cls::k_##field)

namespace {

constexpr uint16_t kZoneInterest = 3; // client.cpp: the one zone interest handle (re-set, not added)

int16_t packFixed(float v, float scale) {
    float s = std::floor(v * scale + 0.5f);
    if (s > 32767.0f) s = 32767.0f;
    if (s < -32768.0f) s = -32768.0f;
    return int16_t(s);
}

bool isLevelClass(uint16_t classId) {
    return classId == dc::DistributedFactory::kClassId || classId == dc::DistributedMintRoom::kClassId ||
           classId == dc::DistributedStageRoom::kClassId || classId == dc::DistributedCountryClubRoom::kClassId;
}

bool isFacilityClass(uint16_t classId) {
    return classId == dc::DistributedMint::kClassId || classId == dc::DistributedStage::kClassId ||
           classId == dc::DistributedCountryClub::kClassId;
}

bool isLevelSuitClass(uint16_t classId) {
    return classId == dc::DistributedFactorySuit::kClassId || classId == dc::DistributedMintSuit::kClassId ||
           classId == dc::DistributedStageSuit::kClassId;
}

int readIds(DatagramReader& r, uint32_t* out, int cap) {
    const uint16_t bytes = r.u16();
    int stored = 0;
    for (int i = 0; i < bytes / 4 && r.ok(); ++i) {
        const uint32_t v = r.u32();
        if (stored < cap) out[stored++] = v;
    }
    if (bytes % 4) r.skip(bytes % 4);
    return stored;
}

} // namespace

const LevelObj* Client::levelObj(uint32_t doId) const {
    for (const LevelObj& l : levels_)
        if (l.active && l.doId == doId) return &l;
    return nullptr;
}

const LevelSuit* Client::findLevelSuit(uint32_t doId) const {
    for (const LevelSuit& s : levelSuits_)
        if (s.active && s.doId == doId) return &s;
    return nullptr;
}

bool Client::cogIdentity(uint32_t doId, CogIdentity* out) const {
    if (const Suit* s = findSuit(doId)) {
        memcpy(out->name, s->name, sizeof(out->name));
        out->levelDist = s->levelDist, out->hp = s->hp, out->hasDna = s->hasDna;
        return true;
    }
    if (const LevelSuit* s = findLevelSuit(doId)) {
        memcpy(out->name, s->name, sizeof(out->name));
        out->levelDist = s->levelDist, out->hp = s->hp, out->hasDna = s->hasDna;
        return true;
    }
    return false;
}

bool Client::applyLevelObjField(LevelObj& l, DatagramReader& r, uint16_t fieldId) {
    namespace L = dc::DistributedLevel;
    namespace M = dc::DistributedMintRoom;
    namespace S = dc::DistributedStageRoom;
    namespace C = dc::DistributedCountryClubRoom;
    namespace F = dc::DistributedFactory;
    if (fieldId == L::k_setLevelZoneId) l.levelZoneId = r.u32();
    else if (fieldId == L::k_setZoneIds) l.numZones = uint16_t(readIds(r, l.zoneIds, 128));
    else if (fieldId == L::k_setStartTimestamp) {
        // 32-bit ClockDelta centiseconds; the low 16 bits are the 16-bit network time.
        const int32_t ts = r.i32();
        l.startMs = timeSynced_ ? networkToLocal(int16_t(uint32_t(ts) & 0xFFFF), now_) : now_;
    } else if (fieldId == L::k_setEntranceId) l.entranceId = r.u8();
    else if (fieldId == L::k_setPlayerIds) l.numPlayers = uint8_t(readIds(r, l.playerIds, 4));
    else if (fieldId == F::k_setFactoryId || fieldId == M::k_setRoomId || fieldId == S::k_setRoomId ||
             fieldId == C::k_setRoomId)
        l.levelId = r.u16();
    else if (fieldId == M::k_setMintId || fieldId == S::k_setStageId || fieldId == C::k_setCountryClubId)
        l.facilityId = r.u16();
    else if (fieldId == M::k_setRoomNum || fieldId == S::k_setRoomNum || fieldId == C::k_setRoomNum)
        l.roomNum = r.u8();
    else if (fieldId == F::k_setSuits || fieldId == M::k_setSuits || fieldId == S::k_setSuits ||
             fieldId == C::k_setSuits) {
        l.numSuits = uint8_t(readIds(r, l.suits, 32));
        l.numReserveSuits = uint8_t(readIds(r, l.reserveSuits, 32));
    } else if (fieldId == F::k_setForemanConfronted || fieldId == M::k_setBossConfronted ||
               fieldId == S::k_setBossConfronted || fieldId == C::k_setBossConfronted)
        l.confronted = r.u32();
    else if (fieldId == F::k_setDefeated || fieldId == M::k_setDefeated || fieldId == S::k_setDefeated ||
             fieldId == C::k_setDefeated)
        l.defeated = true;
    else return skipField(r, fieldId); // forceOuch, setAttribChange, ...
    return r.ok();
}

bool Client::applyFacilityField(Facility& f, DatagramReader& r, uint16_t fieldId) {
    namespace M = dc::DistributedMint;
    namespace S = dc::DistributedStage;
    namespace C = dc::DistributedCountryClub;
    if (fieldId == M::k_setZoneId || fieldId == S::k_setZoneId || fieldId == C::k_setZoneId) f.zoneId = r.u32();
    else if (fieldId == M::k_setMintId || fieldId == S::k_setStageId || fieldId == C::k_setCountryClubId)
        f.facilityId = r.u16();
    else if (fieldId == S::k_setLayoutIndex || fieldId == C::k_setLayoutIndex) f.layoutIndex = r.u16();
    else if (fieldId == M::k_setFloorNum || fieldId == S::k_setFloorNum || fieldId == C::k_setFloorNum)
        f.floorNum = r.u8();
    else if (fieldId == M::k_setRoomDoIds || fieldId == S::k_setRoomDoIds || fieldId == C::k_setRoomDoIds)
        f.numRooms = uint8_t(readIds(r, f.roomDoIds, kMaxLevels));
    else if (fieldId == S::k_setStageZone || fieldId == C::k_setCountryClubZone) {
        const uint32_t zone = r.u32(); // to the riders of the floor elevator, as the floor closes
        if (r.ok()) pushEventArg(EventType::FacilityNextFloor, f.doId, int32_t(zone));
    }
    else return skipField(r, fieldId); // setBlockedRooms, elevatorAlert, ...
    return r.ok();
}

bool Client::applyLevelElevatorField(LevelElevator& e, DatagramReader& r, uint16_t fieldId) {
    namespace E = dc::DistributedElevatorFSM;
    if (fieldId == E::k_setState) {
        r.string(e.state, sizeof(e.state));
        const int16_t ts = r.i16();
        e.stateMs = timeSynced_ ? networkToLocal(ts, now_) : now_;
    } else if (fieldId >= E::k_fillSlot0 && fieldId <= E::k_fillSlot7) {
        e.slots[fieldId - E::k_fillSlot0] = r.u32();
    } else if (fieldId >= E::k_emptySlot0 && fieldId <= E::k_emptySlot7) {
        e.slots[fieldId - E::k_emptySlot0] = 0;
        r.u32();
        r.u8();
        r.i16();
    } else if (fieldId == E::k_rejectBoard) {
        r.u32();
        TT_LOG("net: floor elevator %u rejected us (reason %u)", unsigned(e.doId), unsigned(r.u8()));
    } else if (fieldId == dc::DistributedElevatorFloor::k_setLocked || fieldId == dc::DistributedClubElevator::k_setLocked) {
        e.locked = r.u16() != 0;
    } else if (fieldId == dc::DistributedElevatorFloor::k_setLatch || fieldId == dc::DistributedClubElevator::k_setLatch) {
        e.latch = r.u32();
    } else return skipField(r, fieldId); // setBldgDoId, setFloor, setEntering, kickToonsOut, ...
    return r.ok();
}

bool Client::applyLevelSuitField(LevelSuit& s, DatagramReader& r, uint16_t fieldId) {
    namespace F = dc::DistributedFactorySuit;
    switch (fieldId) {
    case dc::DistributedSuit::k_setDNAString: { // DistributedSuitBase (same field ids)
        Bytes b = r.blob();
        if (!r.ok()) return false;
        s.hasDna = false;
        if (b.len >= 5 && b.data[0] == 's') {
            int n = 0;
            for (int i = 0; i < 3 && b.data[1 + i]; ++i) s.name[n++] = char(b.data[1 + i]);
            s.name[n] = 0;
            s.dept = char(b.data[4]);
            s.hasDna = n > 0;
        }
        break;
    }
    case dc::DistributedSuit::k_setLevelDist: s.levelDist = r.i16(); break;
    case dc::DistributedSuit::k_setSkelecog: s.skelecog = r.u8(); break;
    case dc::DistributedSuit::k_setHP: s.hp = r.i16(); break;
    case F::k_setLevelDoId: s.levelDoId = r.u32(); break;
    case F::k_setCogId: s.cogId = r.u32(); break;
    case F::k_setReserve: s.reserve = r.u8() != 0; break;
    case F::k_setConfrontToon:
        s.confrontToon = r.u32();
        ++s.chaseSerial;
        break;
    case F::k_setReturn:
        s.confrontToon = 0;
        ++s.returnSerial;
        break;
    default: // setSkeleRevives, setBrushOff, denyBattle, ...
        return skipField(r, fieldId);
    }
    return r.ok();
}

bool Client::applyLevelBlockerField(LevelBlocker& b, DatagramReader& r, uint16_t fieldId) {
    namespace B = dc::BattleBlocker;
    if (fieldId == dc::DistributedEntity::k_setLevelDoId) b.levelDoId = r.u32();
    else if (fieldId == dc::DistributedEntity::k_setEntId) b.entId = r.u32();
    else if (fieldId == B::k_setActive) b.blockerActive = r.u8() != 0;
    else if (fieldId == B::k_setSuits) b.numSuits = uint8_t(readIds(r, b.suits, 8));
    else if (fieldId == B::k_setBattle) b.battleId = r.u32();
    else if (fieldId == B::k_setBattleFinished) b.finished = true;
    else return skipField(r, fieldId);
    return r.ok();
}

bool Client::applyLevelEntityField(LevelEntityObj& e, DatagramReader& r, uint16_t fieldId) {
    namespace D = dc::DistributedDoorEntity;
    if (fieldId == dc::DistributedEntity::k_setLevelDoId) e.levelDoId = r.u32();
    else if (fieldId == dc::DistributedEntity::k_setEntId) e.entId = r.u32();
    else if (fieldId == D::k_setDoorState) {
        e.state = r.u8();
        const int32_t ts = r.i32(); // 32-bit ClockDelta time; its low 16 bits are the 16-bit time
        e.stateMs = timeSynced_ ? networkToLocal(int16_t(uint32_t(ts) & 0xFFFF), now_) : now_;
    } else if (fieldId == D::k_setLocksState) {
        const uint16_t bits = r.u16();
        for (int i = 0; i < 3; ++i)
            if (((bits >> (4 * i)) & 15) != ((e.locks >> (4 * i)) & 15)) e.locksMs[i] = now_;
        e.locks = bits;
    } else if (fieldId == dc::DistributedLift::k_setStateTransition) {
        e.state = r.u8();
        e.fromState = r.u8();
        const uint32_t ts = r.u32(); // 32-bit ClockDelta time
        e.arrivalMs = timeSynced_ ? networkToLocal(int16_t(ts & 0xFFFF), now_) : now_;
    } else if (fieldId == dc::DistributedBarrelBase::k_setGrab) {
        e.avatarId = r.u32();
        e.grabMs = now_;
        if (e.avatarId == avatarId_) e.grabbedByUs = true;
        TT_LOG("net: barrel %u grabbed by %u", unsigned(e.doId), unsigned(e.avatarId));
    } else if (fieldId == dc::DistributedBarrelBase::k_setReject) {
        TT_LOG("net: barrel %u rejected our grab", unsigned(e.doId));
    } else if (fieldId == dc::DistributedGoon::k_setMovie) {
        e.movie = r.u8();
        e.avatarId = r.u32();
        e.pauseTime = r.i32() / 10.0f;
        const int16_t ts = r.i16();
        e.movieMs = timeSynced_ ? networkToLocal(ts, now_) : now_;
    } else if (fieldId == dc::DistributedCrushableEntity::k_setCrushed) {
        r.u32();
        r.u8();
        e.crushed = true;
    } else if (fieldId == dc::DistributedStomper::k_setMovie) {
        e.movie = r.u8();
        const int16_t ts = r.i16();
        e.movieMs = timeSynced_ ? networkToLocal(ts, now_) : now_;
        uint32_t crushed[8];
        readIds(r, crushed, 8);
    } else if (fieldId == dc::DistributedInteractiveEntity::k_setState) {
        r.string(e.stateName, sizeof(e.stateName));
        const int32_t ts = r.i32();
        e.stateMs = timeSynced_ ? networkToLocal(int16_t(uint32_t(ts) & 0xFFFF), now_) : now_;
    } else if (fieldId == dc::DistributedInteractiveEntity::k_setAvatarInteract) {
        e.avatarId = r.u32();
    } else if (fieldId == dc::DistributedInteractiveEntity::k_rejectInteract) {
        TT_LOG("net: switch %u rejected our interact", unsigned(e.doId));
    // ---- level games (world/level_games.*) ----
    } else if (fieldId == dc::DistributedLaserField::k_setGrid) {
        e.gridNumX = r.u8(); e.gridNumY = r.u8();
    } else if (fieldId == dc::DistributedLaserField::k_setField) {
        const uint16_t bytes = r.u16();
        e.fieldDataLen = uint8_t(bytes < sizeof(e.fieldData) ? bytes : sizeof(e.fieldData));
        for (uint16_t i = 0; i < bytes && r.ok(); ++i) {
            uint8_t v = r.u8();
            if (i < sizeof(e.fieldData)) e.fieldData[i] = v;
        }
    } else if (fieldId == dc::DistributedLaserField::k_setSuccess) {
        e.success = r.u8();
    } else if (fieldId == dc::DistributedLaserField::k_setActiveLF) {
        e.activeLF = r.u8() != 0;
    } else if (fieldId == dc::DistributedLaserField::k_setGridGame) {
        r.string(e.gridGame, sizeof(e.gridGame));
    } else if (fieldId == dc::DistributedSecurityCamera::k_setTarget) {
        e.targetHash = r.u8();
    } else if (fieldId == dc::DistributedCrate::k_setAccept) {
        e.pushAccepted = true;
    } else if (fieldId == dc::DistributedCrate::k_setReject) {
        e.pushRejected = true;
    } else if (fieldId == dc::DistributedCrate::k_setMoveTo) {
        e.moveAvId = r.u32();
        e.movePos[0] = float(r.i16()) / 10.0f;
        e.movePos[1] = float(r.i16()) / 10.0f;
        e.movePos[2] = float(r.i16()) / 10.0f;
        e.movePos[3] = float(r.i16()) / 10.0f;
        e.movePos[4] = float(r.i16()) / 10.0f;
        e.movePos[5] = float(r.i16()) / 10.0f;
        e.moving = true;
        e.moveStartMs = now_;
    } else if (fieldId == dc::DistributedGridGoon::k_setPathPts) {
        for (int i = 0; i < 6 && r.ok(); ++i) e.pathPts[i] = float(r.i16()) / 10.0f;
    } else if (fieldId == dc::DistributedMaze::k_setRoomDoId) {
        e.roomDoId = r.u32();
    } else if (fieldId == dc::DistributedMaze::k_setGameStart) {
        const int16_t ts = r.i16();
        e.gameStartMs = timeSynced_ ? networkToLocal(ts, now_) : now_;
    } else if (fieldId == dc::DistributedMaze::k_setGameOver) {
        e.gameOver = true;
    } else if (fieldId == dc::DistributedMaze::k_toonFinished) {
        r.u32(); r.u8(); const uint8_t last = r.u8();
        if (last) e.gameOver = true;
    } else if (fieldId == dc::DistributedMoleField::k_setGameStart) {
        const int16_t ts = r.i16();
        e.gameStartMs = timeSynced_ ? networkToLocal(ts, now_) : now_;
        e.molesWhackedTarget = r.u8();
        e.gameDuration = r.u16();
    } else if (fieldId == dc::DistributedMoleField::k_setScore) {
        e.score = r.i16();
    } else if (fieldId == dc::DistributedMoleField::k_updateMole) {
        r.i8(); r.i8();
    } else if (fieldId == dc::DistributedMoleField::k_setPityWin) {
        e.pityWin = true;
    } else if (fieldId == dc::DistributedGolfGreenGame::k_acceptJoin) {
        e.boardsLeft = r.u16();
        r.i32();
        e.numJoinedToons = uint8_t(readIds(r, e.joinedToons, 4));
    } else if (fieldId == dc::DistributedGolfGreenGame::k_scoreData) {
        e.boardsLeft = r.u8();
        r.u8();
        const uint16_t bytes = r.u16();
        r.skip(bytes);
    } else if (fieldId == dc::DistributedGolfGreenGame::k_signalDone) {
        r.u8();
    } else if (fieldId == dc::BattleBlocker::k_setActive) {
        e.blockerActive = r.u8() != 0;
    } else if (fieldId == dc::BattleBlocker::k_setSuits) {
        e.numSuits = uint8_t(readIds(r, e.suits, 8));
    } else if (fieldId == dc::BattleBlocker::k_setBattle) {
        e.battleId = r.u32();
    } else if (fieldId == dc::BattleBlocker::k_setBattleFinished) {
        e.finished = true;
    // ---- end level games ----
    } else return skipField(r, fieldId);
    return r.ok();
}

bool Client::handleLevelEnter(uint32_t doId, uint16_t classId, DatagramReader& r, const uint16_t* req,
                              uint16_t reqCount, bool other) {
    if (isLevelClass(classId)) {
        LevelObj tmp{};
        tmp.doId = doId;
        tmp.classId = classId;
        bool ok = true;
        for (uint16_t i = 0; i < reqCount && ok; ++i) ok = applyLevelObjField(tmp, r, req[i]);
        if (ok && other) {
            const uint16_t n = r.u16();
            for (uint16_t i = 0; i < n && ok; ++i) ok = applyLevelObjField(tmp, r, r.u16());
        }
        if (!ok || !r.ok()) return true;
        LevelObj* slot = nullptr;
        for (LevelObj& l : levels_)
            if (l.active && l.doId == doId) slot = &l;
        for (LevelObj& l : levels_)
            if (!slot && !l.active) slot = &l;
        if (!slot) {
            TT_LOG("net: level %u dropped (table full)", unsigned(doId));
            return true;
        }
        tmp.active = true;
        *slot = tmp;
        TT_LOG("net: level %u id %u room %u of %u, %u zones", unsigned(doId), unsigned(tmp.levelId),
               unsigned(tmp.roomNum), unsigned(tmp.facilityId), unsigned(tmp.numZones));
        pushEvent(EventType::LevelUp, doId);
        return true;
    }
    if (isFacilityClass(classId)) {
        Facility tmp{};
        tmp.doId = doId;
        tmp.kind = classId == dc::DistributedMint::kClassId    ? FacilityKind::Mint
                   : classId == dc::DistributedStage::kClassId ? FacilityKind::Stage
                                                               : FacilityKind::CountryClub;
        bool ok = true;
        for (uint16_t i = 0; i < reqCount && ok; ++i) ok = applyFacilityField(tmp, r, req[i]);
        if (ok && other) {
            const uint16_t n = r.u16();
            for (uint16_t i = 0; i < n && ok; ++i) ok = applyFacilityField(tmp, r, r.u16());
        }
        if (!ok || !r.ok()) return true;
        tmp.active = true;
        facility_ = tmp;
        TT_LOG("net: facility %u id %u floor %u layout %u, %u rooms", unsigned(doId), unsigned(tmp.facilityId),
               unsigned(tmp.floorNum), unsigned(tmp.layoutIndex), unsigned(tmp.numRooms));
        pushEvent(EventType::FacilityUp, doId);
        return true;
    }
    if (classId == dc::DistributedElevatorFloor::kClassId || classId == dc::DistributedClubElevator::kClassId) {
        LevelElevator tmp{};
        tmp.doId = doId;
        tmp.club = classId == dc::DistributedClubElevator::kClassId;
        bool ok = true;
        for (uint16_t i = 0; i < reqCount && ok; ++i) ok = applyLevelElevatorField(tmp, r, req[i]);
        if (ok && other) {
            const uint16_t n = r.u16();
            for (uint16_t i = 0; i < n && ok; ++i) ok = applyLevelElevatorField(tmp, r, r.u16());
        }
        if (!ok || !r.ok()) return true;
        LevelElevator* slot = nullptr;
        for (LevelElevator& e : levelElevators_)
            if (e.active && e.doId == doId) slot = &e;
        for (LevelElevator& e : levelElevators_)
            if (!slot && !e.active) slot = &e;
        if (!slot) return true;
        tmp.active = true;
        *slot = tmp;
        TT_LOG("net: floor elevator %u on marker %u (%s)", unsigned(doId), unsigned(tmp.latch),
               tmp.locked ? "locked" : "open");
        return true;
    }
    const dc::ClassDesc* cls = findClass(classId);
    const bool suit = isLevelSuitClass(classId);
    const bool blocker = !suit && cls && classHasField(*cls, dc::BattleBlocker::k_setBattle);
    if (classId == dc::DistributedDoorEntity::kClassId || classId == dc::DistributedButton::kClassId ||
        classId == dc::DistributedStomper::kClassId || classId == dc::DistributedLift::kClassId ||
        classId == dc::DistributedGagBarrel::kClassId || classId == dc::DistributedBeanBarrel::kClassId ||
        classId == dc::DistributedHealBarrel::kClassId || classId == dc::DistributedGoon::kClassId ||
        classId == dc::DistributedElevatorMarker::kClassId ||
        // ---- level games (world/level_games.*) ----
        classId == dc::DistributedLaserField::kClassId ||
        classId == dc::DistributedSecurityCamera::kClassId ||
        classId == dc::DistributedCrate::kClassId ||
        classId == dc::DistributedGrid::kClassId ||
        classId == dc::DistributedGridGoon::kClassId ||
        classId == dc::DistributedMaze::kClassId ||
        classId == dc::DistributedMoleField::kClassId ||
        classId == dc::DistributedGolfGreenGame::kClassId
        // ---- end level games ----
        ) {
        LevelEntityObj tmp{};
        tmp.doId = doId;
        tmp.classId = classId;
        bool ok = true;
        for (uint16_t i = 0; i < reqCount && ok; ++i) ok = applyLevelEntityField(tmp, r, req[i]);
        if (ok && other) {
            const uint16_t n = r.u16();
            for (uint16_t i = 0; i < n && ok; ++i) ok = applyLevelEntityField(tmp, r, r.u16());
        }
        if (!ok || !r.ok()) return true;
        for (uint32_t& t : tmp.locksMs) t = 0; // initial lock states are not transitions
        LevelEntityObj* slot = nullptr;
        for (LevelEntityObj& e : levelEntities_)
            if (e.active && e.doId == doId) slot = &e;
        for (LevelEntityObj& e : levelEntities_)
            if (!slot && !e.active) slot = &e;
        if (!slot) return true;
        tmp.active = true;
        *slot = tmp;
        return true;
    }
    if (!suit && !blocker) return false;
    if (suit) {
        LevelSuit tmp{};
        tmp.doId = doId;
        bool ok = true;
        for (uint16_t i = 0; i < reqCount && ok; ++i) ok = applyLevelSuitField(tmp, r, req[i]);
        if (ok && other) {
            const uint16_t n = r.u16();
            for (uint16_t i = 0; i < n && ok; ++i) ok = applyLevelSuitField(tmp, r, r.u16());
        }
        if (!ok || !r.ok()) return true;
        LevelSuit* slot = nullptr;
        for (LevelSuit& s : levelSuits_)
            if (s.active && s.doId == doId) slot = &s;
        for (LevelSuit& s : levelSuits_)
            if (!slot && !s.active) slot = &s;
        if (!slot) {
            TT_LOG("net: level cog %u dropped (table full)", unsigned(doId));
            return true;
        }
        tmp.active = true;
        *slot = tmp;
        TT_LOG("net: level cog %u \"%s\" cog %u level %u%s", unsigned(doId), tmp.name, unsigned(tmp.cogId),
               unsigned(tmp.levelDoId), tmp.reserve ? " (reserve)" : "");
        pushEvent(EventType::LevelSuitUp, doId);
        return true;
    }
    LevelBlocker tmp{};
    tmp.doId = doId;
    bool ok = true;
    for (uint16_t i = 0; i < reqCount && ok; ++i) ok = applyLevelBlockerField(tmp, r, req[i]);
    if (ok && other) {
        const uint16_t n = r.u16();
        for (uint16_t i = 0; i < n && ok; ++i) ok = applyLevelBlockerField(tmp, r, r.u16());
    }
    if (!ok || !r.ok()) return true;
    LevelBlocker* slot = nullptr;
    for (LevelBlocker& b : levelBlockers_)
        if (b.active && b.doId == doId) slot = &b;
    for (LevelBlocker& b : levelBlockers_)
        if (!slot && !b.active) slot = &b;
    if (!slot) return true;
    tmp.active = true;
    *slot = tmp;
    pushEvent(EventType::LevelBlockerState, doId);
    return true;
}

bool Client::handleLevelField(DatagramReader& r, uint32_t doId, uint16_t fieldId) {
    for (LevelObj& l : levels_) {
        if (!l.active || l.doId != doId) continue;
        if (applyLevelObjField(l, r, fieldId)) pushEvent(EventType::LevelState, doId);
        return true;
    }
    if (facility_.active && facility_.doId == doId) {
        if (applyFacilityField(facility_, r, fieldId)) pushEvent(EventType::FacilityUp, doId);
        return true;
    }
    for (LevelElevator& e : levelElevators_) {
        if (!e.active || e.doId != doId) continue;
        applyLevelElevatorField(e, r, fieldId);
        return true;
    }
    for (LevelSuit& s : levelSuits_) {
        if (!s.active || s.doId != doId) continue;
        if (applyLevelSuitField(s, r, fieldId)) pushEvent(EventType::LevelSuitState, doId);
        return true;
    }
    for (LevelBlocker& b : levelBlockers_) {
        if (!b.active || b.doId != doId) continue;
        if (applyLevelBlockerField(b, r, fieldId)) pushEvent(EventType::LevelBlockerState, doId);
        return true;
    }
    for (LevelEntityObj& e : levelEntities_) {
        if (!e.active || e.doId != doId) continue;
        applyLevelEntityField(e, r, fieldId);
        return true;
    }
    return false;
}

bool Client::handleLevelLeaving(uint32_t doId) {
    for (LevelObj& l : levels_) {
        if (!l.active || l.doId != doId) continue;
        l = LevelObj{};
        pushEvent(EventType::LevelGone, doId);
        return true;
    }
    if (facility_.active && facility_.doId == doId) {
        facility_ = Facility{};
        pushEvent(EventType::FacilityGone, doId);
        return true;
    }
    for (LevelElevator& e : levelElevators_) {
        if (!e.active || e.doId != doId) continue;
        e = LevelElevator{};
        return true;
    }
    for (LevelSuit& s : levelSuits_) {
        if (!s.active || s.doId != doId) continue;
        s = LevelSuit{};
        pushEvent(EventType::LevelSuitGone, doId);
        return true;
    }
    for (LevelBlocker& b : levelBlockers_) {
        if (!b.active || b.doId != doId) continue;
        b = LevelBlocker{};
        pushEvent(EventType::LevelBlockerState, doId);
        return true;
    }
    for (LevelEntityObj& e : levelEntities_) {
        if (!e.active || e.doId != doId) continue;
        e = LevelEntityObj{};
        return true;
    }
    return false;
}

void Client::clearLevelObjects() {
    for (LevelObj& l : levels_) {
        if (l.active) pushEvent(EventType::LevelGone, l.doId);
        l = LevelObj{};
    }
    if (facility_.active) pushEvent(EventType::FacilityGone, facility_.doId);
    facility_ = Facility{};
    for (LevelElevator& e : levelElevators_) e = LevelElevator{};
    for (LevelSuit& s : levelSuits_) {
        if (s.active) pushEvent(EventType::LevelSuitGone, s.doId);
        s = LevelSuit{};
    }
    for (LevelBlocker& b : levelBlockers_) b = LevelBlocker{};
    for (LevelEntityObj& e : levelEntities_) e = LevelEntityObj{};
}

bool Client::setLevelVisibility(uint32_t levelZone, const uint32_t* zones, int count) {
    if (state_ != State::InZone) return false;
    uint32_t ids[kMaxVisZones + 1];
    int n = 0;
    ids[n++] = levelZone;
    for (int i = 0; i < count && n <= kMaxVisZones; ++i) {
        bool dup = false;
        for (int j = 0; j < n; ++j) dup = dup || ids[j] == zones[i];
        if (!dup) ids[n++] = zones[i];
    }
    cfg_.zoneId = levelZone;
    sendSetLocation(avatarId_, shardId_, levelZone);
    zoneCtx_ = nextContext_;
    sendAddInterestMultiple(kZoneInterest, shardId_, ids, n);
    TT_LOG("net: level %u visibility: %d zones", unsigned(levelZone), n);
    return true;
}

bool Client::sendLevelSuitBattle(uint32_t doId, float x, float y, float z, float h) {
    if (state_ != State::InZone) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, doId, DistributedFactorySuit, requestBattle);
    w.i16(packFixed(x, 10.0f));
    w.i16(packFixed(y, 10.0f));
    w.i16(packFixed(z, 10.0f));
    w.i16(packFixed(h, 10.0f));
    w.i16(0);
    w.i16(0);
    TT_LOG("net: requestBattle on level cog %u", unsigned(doId));
    return finish(w);
}

bool Client::sendLevelDoorOpen(uint32_t doId) {
    if (state_ != State::InZone) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, doId, DistributedDoorEntity, requestOpen);
    return finish(w);
}

bool Client::sendLevelInteract(uint32_t doId, bool enter) {
    if (state_ != State::InZone) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    if (enter) {
        TT_NET_UPDATE(w, doId, DistributedInteractiveEntity, requestInteract);
    } else {
        TT_NET_UPDATE(w, doId, DistributedInteractiveEntity, requestExit);
    }
    return finish(w);
}

bool Client::sendLevelOuch(uint32_t levelDoId, uint8_t penalty) {
    if (state_ != State::InZone) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, levelDoId, DistributedLevel, setOuch);
    w.u8(penalty);
    return finish(w);
}

bool Client::sendLevelLiftRider(uint32_t doId, bool enter) {
    if (state_ != State::InZone) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    if (enter) {
        TT_NET_UPDATE(w, doId, DistributedLift, setAvatarEnter);
    } else {
        TT_NET_UPDATE(w, doId, DistributedLift, setAvatarLeave);
    }
    return finish(w);
}

bool Client::sendLevelElevatorBoard(uint32_t doId) {
    if (state_ != State::InZone) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, doId, DistributedElevatorFSM, requestBoard);
    TT_LOG("net: requestBoard floor elevator %u", unsigned(doId));
    return finish(w);
}

bool Client::sendLevelBarrelGrab(uint32_t doId) {
    if (state_ != State::InZone) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, doId, DistributedBarrelBase, requestGrab);
    return finish(w);
}

bool Client::sendLevelGoonBattle(uint32_t doId, float pauseTime) {
    if (state_ != State::InZone) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, doId, DistributedGoon, requestBattle);
    w.i16(packFixed(pauseTime, 10.0f));
    return finish(w);
}

bool Client::sendLevelGoonStunned(uint32_t doId, float pauseTime) {
    if (state_ != State::InZone) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, doId, DistributedGoon, requestStunned);
    w.i16(packFixed(pauseTime, 10.0f));
    return finish(w);
}

bool Client::sendLevelSuitAlert(uint32_t doId) {
    if (state_ != State::InZone) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, doId, DistributedFactorySuit, setAlert);
    w.u32(avatarId_);
    return finish(w);
}

bool Client::sendLevelSuitStrayed(uint32_t doId) {
    if (state_ != State::InZone) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, doId, DistributedFactorySuit, setStrayed);
    return finish(w);
}

// ---- level games (world/level_games.*) ----
bool Client::sendLaserHit(uint32_t doId, int8_t newX, int8_t newY, int8_t oldX, int8_t oldY) {
    if (state_ != State::InZone) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, doId, DistributedLaserField, hit);
    w.i8(newX);
    w.i8(newY);
    w.i8(oldX);
    w.i8(oldY);
    return finish(w);
}

bool Client::sendLaserTrapFire(uint32_t doId) {
    if (state_ != State::InZone) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, doId, DistributedLaserField, trapFire);
    return finish(w);
}

bool Client::sendCameraTrapFire(uint32_t doId) {
    if (state_ != State::InZone) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, doId, DistributedSecurityCamera, trapFire);
    return finish(w);
}

bool Client::sendCratePush(uint32_t doId, uint8_t side) {
    if (state_ != State::InZone) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, doId, DistributedCrate, requestPush);
    w.u8(side);
    return finish(w);
}

bool Client::sendCrateDone(uint32_t doId) {
    if (state_ != State::InZone) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, doId, DistributedCrate, setDone);
    return finish(w);
}

bool Client::sendMazeTriggered(uint32_t doId) {
    if (state_ != State::InZone) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, doId, DistributedMaze, setClientTriggered);
    return finish(w);
}

bool Client::sendMazeFinished(uint32_t doId) {
    if (state_ != State::InZone) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, doId, DistributedMaze, setFinishedMaze);
    return finish(w);
}

bool Client::sendMazeDamage(uint32_t doId) {
    if (state_ != State::InZone) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, doId, DistributedMaze, damageMe);
    return finish(w);
}

bool Client::sendMoleTriggered(uint32_t doId) {
    if (state_ != State::InZone) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, doId, DistributedMoleField, setClientTriggered);
    return finish(w);
}

bool Client::sendMoleWhacked(uint32_t doId, int8_t moleIndex, int16_t popupNum) {
    if (state_ != State::InZone) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, doId, DistributedMoleField, whackedMole);
    w.i8(moleIndex);
    w.i16(popupNum);
    return finish(w);
}

bool Client::sendMoleBomb(uint32_t doId, int8_t moleIndex, int16_t popupNum, int32_t ts) {
    if (state_ != State::InZone) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, doId, DistributedMoleField, whackedBomb);
    w.i8(moleIndex);
    w.i16(popupNum);
    w.i32(ts);
    return finish(w);
}

bool Client::sendMoleDamage(uint32_t doId) {
    if (state_ != State::InZone) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, doId, DistributedMoleField, damageMe);
    return finish(w);
}

bool Client::sendGolfJoin(uint32_t doId) {
    if (state_ != State::InZone) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, doId, DistributedGolfGreenGame, requestJoin);
    return finish(w);
}

bool Client::sendGolfLeave(uint32_t doId) {
    if (state_ != State::InZone) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, doId, DistributedGolfGreenGame, leaveGame);
    return finish(w);
}

bool Client::sendGolfBoard(uint32_t doId, uint8_t index) {
    if (state_ != State::InZone) return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, doId, DistributedGolfGreenGame, requestBoard);
    w.u8(index);
    return finish(w);
}
// ---- end level games ----

} // namespace tt::net
