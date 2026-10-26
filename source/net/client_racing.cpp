#include "core/log.h"
#include "net/client.h"
#include "net/msg_types.h"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace tt::net {
#define TT_NET_UPDATE(w, id, cls, field)                                                           \
    static_assert((dc::cls::kw_##field & dc::kKwClsend) != 0, #cls "." #field " must be clsend");  \
    beginUpdate(w, id, dc::cls::k_##field)
namespace {
constexpr bool equal(const char *a, const char *b) {
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}
#define TT_SIG(cls, field, sig)                                                                    \
    static_assert(equal(dc::cls::sig_##field, sig), #cls "." #field " layout changed")
TT_SIG(DistributedStartingBlock, requestEnter, "u8");
TT_SIG(DistributedStartingBlock, requestExit, "");
TT_SIG(DistributedStartingBlock, movieFinished, "");
TT_SIG(DistributedVehicle, requestControl, "");
TT_SIG(DistributedVehicle, requestParked, "");
TT_SIG(DistributedRace, requestKart, "");
TT_SIG(DistributedRace, racerLeft, "u32");
TT_SIG(DistributedRace, heresMyT, "u32,i8,u16/65535,i16");
TT_SIG(DistributedRace, requestThrow, "i32/1000,i32/1000,i32/1000");
TT_SIG(DistributedRace, hasGag, "u8,u8,u8");
TT_SIG(DistributedObject, setBarrierReady, "u16");
TT_SIG(DistributedSmoothNode, setSmPosHpr,
       "i16/10,i16/10,i16/10,i16%360/10,i16%360/10,i16%360/10,i16");
TT_SIG(DistributedGag, hitSomebody, "u32,i16");
TT_SIG(DistributedProjectile, hitSomebody, "u32,i16");
#undef TT_SIG
bool ids(DatagramReader &r, uint32_t *output, int capacity, uint8_t *count) {
    const Bytes bytes = r.blob();
    if (bytes.len % 4 || bytes.len / 4 > unsigned(capacity)) {
        r.fail();
        return false;
    }
    DatagramReader list(bytes.data, bytes.len);
    *count = uint8_t(bytes.len / 4);
    for (int i = 0; i < *count; ++i)
        output[i] = list.u32();
    return r.ok();
}
RacingResult *result(RacingRace &race, uint32_t avatar) {
    for (int i = 0; i < race.numRacers; ++i)
        if (race.racers[i] == avatar)
            return &race.results[i];
    return nullptr;
}
} // namespace
void Client::clearRacing() {
    for (auto &p : racingPads_)
        p = RacingPad{};
    for (auto &b : racingBlocks_)
        b = RacingBlock{};
    for (auto &v : racingVehicles_)
        v = RacingVehicle{};
    for (auto &h : racingHazards_)
        h = RacingHazard{};
    if (racingRace_.active)
        pushEvent(EventType::RacingGone, racingRace_.doId);
    racingRace_ = RacingRace{};
}
bool Client::raceParticipant() const {
    if (state_ != State::InZone || !avatarId_ || !racingRace_.active)
        return false;
    for (int i = 0; i < racingRace_.numRacers; ++i)
        if (racingRace_.racers[i] == avatarId_)
            return true;
    return false;
}
bool Client::handleRacingEnter(uint32_t id, uint16_t cls, DatagramReader &r,
                               const uint16_t *required, uint16_t count, bool other) {
    enum Kind { Pad, View, Block, Vehicle, Race, Gag, Projectile, Unknown } kind = Unknown;
    if (cls == dc::DistributedRacePad::kClassId)
        kind = Pad;
    else if (cls == dc::DistributedViewPad::kClassId)
        kind = View;
    else if (cls == dc::DistributedStartingBlock::kClassId ||
             cls == dc::DistributedViewingBlock::kClassId)
        kind = Block;
    else if (cls == dc::DistributedVehicle::kClassId)
        kind = Vehicle;
    else if (cls == dc::DistributedRace::kClassId)
        kind = Race;
    else if (cls == dc::DistributedGag::kClassId)
        kind = Gag;
    else if (cls == dc::DistributedProjectile::kClassId)
        kind = Projectile;
    if (kind == Unknown)
        return false;
    // The decoder is also used for OTHER fields; publish only a complete object.
    int slot = -1;
    if (kind == Pad || kind == View) {
        for (int i = 0; i < kMaxRacingPads; ++i)
            if (racingPads_[i].doId == id || (!racingPads_[i].active && slot < 0))
                slot = i;
        if (slot >= 0) {
            racingPads_[slot] = RacingPad{};
            racingPads_[slot].doId = id;
            racingPads_[slot].active = true;
            racingPads_[slot].viewing = kind == View;
        }
    } else if (kind == Block) {
        for (int i = 0; i < kMaxRacingBlocks; ++i)
            if (racingBlocks_[i].doId == id || (!racingBlocks_[i].active && slot < 0))
                slot = i;
        if (slot >= 0) {
            racingBlocks_[slot] = RacingBlock{};
            racingBlocks_[slot].doId = id;
            racingBlocks_[slot].active = true;
            racingBlocks_[slot].viewing = cls == dc::DistributedViewingBlock::kClassId;
        }
    } else if (kind == Vehicle) {
        for (int i = 0; i < kMaxRacingVehicles; ++i)
            if (racingVehicles_[i].doId == id || (!racingVehicles_[i].active && slot < 0))
                slot = i;
        if (slot >= 0) {
            racingVehicles_[slot] = RacingVehicle{};
            racingVehicles_[slot].doId = id;
            racingVehicles_[slot].active = true;
        }
    } else if (kind == Race) {
        racingRace_ = RacingRace{};
        racingRace_.doId = id;
        racingRace_.active = true;
        slot = 0;
    } else {
        for (int i = 0; i < 32; ++i)
            if (!racingHazards_[i].active && slot < 0)
                slot = i;
        if (slot >= 0) {
            racingHazards_[slot] = RacingHazard{};
            racingHazards_[slot].active = true;
            racingHazards_[slot].doId = id;
            racingHazards_[slot].projectile = kind == Projectile;
        }
    }
    racingGenerating_ = true;
    auto apply = [&](uint16_t field) {
        if (slot < 0)
            skipField(r, field);
        else
            handleRacingField(r, id, field);
    };
    for (int i = 0; i < count && r.ok(); ++i)
        apply(required[i]);
    if (other && r.ok()) {
        const unsigned n = r.u16();
        for (unsigned i = 0; i < n && r.ok(); ++i)
            apply(r.u16());
    }
    racingGenerating_ = false;
    if (!r.ok()) {
        if (kind == Pad || kind == View) {
            if (slot >= 0)
                racingPads_[slot].active = false;
        } else if (kind == Block) {
            if (slot >= 0)
                racingBlocks_[slot].active = false;
        } else if (kind == Vehicle) {
            if (slot >= 0)
                racingVehicles_[slot].active = false;
        } else if (kind == Race)
            racingRace_.active = false;
        else if (slot >= 0)
            racingHazards_[slot].active = false;
        return true;
    }
    if (kind == Race)
        pushEvent(EventType::RacingRaceUp, id);
    else if (kind == Pad || kind == View)
        pushEvent(EventType::RacingPadUp, id);
    else if (kind == Block)
        pushEvent(EventType::RacingBlockUp, id);
    else if (kind == Vehicle)
        pushEvent(EventType::RacingVehicleUp, id);
    return true;
}
bool Client::handleRacingField(DatagramReader &r, uint32_t id, uint16_t field) {
    // Atomic field publication: malformed updates leave the prior object intact.
    if (racingRace_.active && racingRace_.doId == id) {
        const auto saved = racingRace_;
        const bool handled = decodeRacingField(r, id, field);
        if (!r.ok())
            racingRace_ = saved;
        return handled;
    }
    for (auto &object : racingPads_)
        if (object.active && object.doId == id) {
            const auto saved = object;
            const bool handled = decodeRacingField(r, id, field);
            if (!r.ok())
                object = saved;
            return handled;
        }
    for (auto &object : racingBlocks_)
        if (object.active && object.doId == id) {
            const auto saved = object;
            const bool handled = decodeRacingField(r, id, field);
            if (!r.ok())
                object = saved;
            return handled;
        }
    for (auto &object : racingVehicles_)
        if (object.active && object.doId == id) {
            const auto saved = object;
            const bool handled = decodeRacingField(r, id, field);
            if (!r.ok())
                object = saved;
            return handled;
        }
    for (auto &object : racingHazards_)
        if (object.active && object.doId == id) {
            const auto saved = object;
            const bool handled = decodeRacingField(r, id, field);
            if (!r.ok())
                object = saved;
            return handled;
        }
    return false;
}
bool Client::decodeRacingField(DatagramReader &r, uint32_t id, uint16_t field) {
    for (auto &p : racingPads_)
        if (p.active && p.doId == id) {
            if (field == dc::DistributedRacePad::k_setState) {
                r.string(p.state, sizeof(p.state));
                p.stateMs = networkToLocal(r.i16(), now_);
            } else if (field == dc::DistributedKartPad::k_setArea)
                p.area = r.u32();
            else if (field == dc::DistributedRacePad::k_setRaceZone)
                p.raceZone = r.u32();
            else if (field == dc::DistributedRacePad::k_setTrackInfo) {
                const Bytes list = r.blob();
                if (list.len % 2 || list.len > 16)
                    r.fail();
                p.trackCount = uint8_t(std::min<size_t>(8, list.len / 2));
                for (int i = 0; i < p.trackCount; ++i)
                    p.trackIds[i] = uint16_t(list.data[i * 2] | list.data[i * 2 + 1] << 8);
            } else if (field == dc::DistributedViewPad::k_setLastEntered)
                p.stateMs = networkToLocal(r.i16(), now_);
            else
                skipField(r, field);
            if (r.ok() && !racingGenerating_)
                pushEvent(EventType::RacingPadState, id);
            return true;
        }
    for (auto &b : racingBlocks_)
        if (b.active && b.doId == id) {
            if (field == dc::DistributedStartingBlock::k_setOccupied) {
                b.occupant = r.u32();
                b.rejection = 0;
            } else if (field == dc::DistributedStartingBlock::k_setMovie) {
                b.movie = r.u8();
                ++b.revision;
            } else if (field == dc::DistributedStartingBlock::k_rejectEnter) {
                b.rejection = r.u8();
                ++b.revision;
            } else if (field == dc::DistributedStartingBlock::k_setPadDoId)
                b.padDoId = r.u32();
            else if (field == dc::DistributedStartingBlock::k_setPadLocationId)
                b.location = r.u8();
            else if (field == dc::DistributedStartingBlock::k_setPosHpr) {
                b.x = r.i16() / 10.0f;
                b.y = r.i16() / 10.0f;
                b.z = r.i16() / 10.0f;
                b.h = r.i16() / 10.0f;
                b.p = r.i16() / 10.0f;
                b.r = r.i16() / 10.0f;
            } else
                skipField(r, field);
            if (r.ok() && !racingGenerating_)
                pushEvent(EventType::RacingBlockState, id);
            return true;
        }
    for (auto &v : racingVehicles_)
        if (v.active && v.doId == id) {
            if (field == dc::DistributedVehicle::k_setOwner)
                v.owner = r.u32();
            else if (field == dc::DistributedVehicle::k_setState) {
                v.state = char(r.u8());
                v.driver = r.u32();
            } else if (field == dc::DistributedVehicle::k_setBodyType)
                v.body = r.i8();
            else if (field == dc::DistributedVehicle::k_setBodyColor)
                v.bodyColor = r.i8();
            else if (field == dc::DistributedVehicle::k_setAccessoryColor)
                v.accessoryColor = r.i8();
            else if (field == dc::DistributedVehicle::k_setEngineBlockType)
                v.engine = r.i8();
            else if (field == dc::DistributedVehicle::k_setSpoilerType)
                v.spoiler = r.i8();
            else if (field == dc::DistributedVehicle::k_setFrontWheelWellType)
                v.frontWheels = r.i8();
            else if (field == dc::DistributedVehicle::k_setBackWheelWellType)
                v.backWheels = r.i8();
            else if (field == dc::DistributedVehicle::k_setRimType)
                v.rims = r.i8();
            else if (field == dc::DistributedVehicle::k_setDecalType)
                v.decal = r.i8();
            else if (field == dc::DistributedVehicle::k_setInput)
                v.input = r.i8();
            else if (field >= dc::DistributedSmoothNode::k_setSmStop &&
                     field <= dc::DistributedSmoothNode::k_setSmPosHprL) {
                const bool full = field == dc::DistributedSmoothNode::k_setSmPosHpr ||
                                  field == dc::DistributedSmoothNode::k_setSmPosHprL;
                if (field == dc::DistributedSmoothNode::k_setSmPosHprL)
                    r.u64();
                if (full || field == dc::DistributedSmoothNode::k_setSmXYH ||
                    field == dc::DistributedSmoothNode::k_setSmXYZH ||
                    field == dc::DistributedSmoothNode::k_setSmXY ||
                    field == dc::DistributedSmoothNode::k_setSmXZ ||
                    field == dc::DistributedSmoothNode::k_setSmPos)
                    v.x = r.i16() / 10.0f;
                if (full || field == dc::DistributedSmoothNode::k_setSmXYH ||
                    field == dc::DistributedSmoothNode::k_setSmXYZH ||
                    field == dc::DistributedSmoothNode::k_setSmXY ||
                    field == dc::DistributedSmoothNode::k_setSmPos)
                    v.y = r.i16() / 10.0f;
                if (full || field == dc::DistributedSmoothNode::k_setSmXYZH ||
                    field == dc::DistributedSmoothNode::k_setSmXZ ||
                    field == dc::DistributedSmoothNode::k_setSmPos ||
                    field == dc::DistributedSmoothNode::k_setSmZ)
                    v.z = r.i16() / 10.0f;
                if (full || field == dc::DistributedSmoothNode::k_setSmXYH ||
                    field == dc::DistributedSmoothNode::k_setSmXYZH ||
                    field == dc::DistributedSmoothNode::k_setSmHpr ||
                    field == dc::DistributedSmoothNode::k_setSmH)
                    v.h = r.i16() / 10.0f;
                if (full || field == dc::DistributedSmoothNode::k_setSmHpr) {
                    r.i16();
                    r.i16();
                }
                v.poseMs = networkToLocal(r.i16(), now_);
            } else
                skipField(r, field);
            if (r.ok() && !racingGenerating_)
                pushEvent(EventType::RacingVehicleState, id);
            return true;
        }
    for (auto &h : racingHazards_)
        if (h.active && h.doId == id) {
            if (field == dc::DistributedGag::k_setRace ||
                field == dc::DistributedProjectile::k_setRace)
                h.race = r.u32();
            else if (field == dc::DistributedGag::k_setOwnerId ||
                     field == dc::DistributedProjectile::k_setOwnerId)
                h.owner = r.u32();
            else if (field == dc::DistributedGag::k_setInitTime ||
                     field == dc::DistributedProjectile::k_setInitTime)
                h.initMs = networkToLocal(r.i16(), now_);
            else if (field == dc::DistributedGag::k_setPos ||
                     field == dc::DistributedProjectile::k_setPos) {
                h.x = r.i32() / 1000.0f;
                h.y = r.i32() / 1000.0f;
                h.z = r.i32() / 1000.0f;
            } else if (field == dc::DistributedGag::k_setType ||
                       field == dc::DistributedProjectile::k_setType)
                h.type = r.u8();
            else if (field == dc::DistributedGag::k_setActivateTime)
                h.activate = r.u16();
            else
                skipField(r, field);
            return true;
        }
    if (!racingRace_.active || racingRace_.doId != id)
        return false;
    RacingRace &race = racingRace_;
    if (field == dc::DistributedRace::k_setZoneId)
        race.zone = r.u32();
    else if (field == dc::DistributedRace::k_setTrackId)
        race.track = r.u16();
    else if (field == dc::DistributedRace::k_setRaceType)
        race.raceType = r.u16();
    else if (field == dc::DistributedRace::k_setLapCount)
        race.laps = r.u8();
    else if (field == dc::DistributedRace::k_setCircuitLoop) {
        const Bytes tracks = r.blob();
        if (tracks.len % 2 || tracks.len > 8)
            r.fail();
        else
            race.circuitCount = uint8_t(tracks.len / 2);
    } else if (field == dc::DistributedRace::k_setAvatars) {
        ids(r, race.racers, 4, &race.numRacers);
        for (int i = 0; i < race.numRacers; ++i)
            race.results[i].avatar = race.racers[i];
    } else if (field == dc::DistributedRace::k_setStartingPlaces) {
        const Bytes list = r.blob();
        if (list.len > 4)
            r.fail();
        for (unsigned i = 0; i < list.len && i < 4; ++i)
            race.results[i].startingPlace = list.data[i];
    } else if (field == dc::DistributedRace::k_setEnteredRacers) {
        const Bytes list = r.blob();
        if (list.len % 8 || list.len > 32)
            r.fail();
        DatagramReader entries(list.data, list.len);
        while (entries.remaining() && entries.ok()) {
            const uint32_t avatar = entries.u32(), kart = entries.u32();
            if (auto *player = result(race, avatar))
                player->kart = kart;
        }
        race.phase = 1;
    } else if (field == dc::DistributedRace::k_waitingForJoin)
        race.phase = 0;
    else if (field == dc::DistributedRace::k_prepForRace)
        race.phase = 2;
    else if (field == dc::DistributedRace::k_startTutorial)
        race.phase = 3;
    else if (field == dc::DistributedRace::k_startRace) {
        race.startTs = r.i16();
        race.startMs = networkToLocal(race.startTs, now_);
        race.started = true;
        race.phase = 4;
    } else if (field == dc::DistributedRace::k_heresMyT) {
        const uint32_t avatar = r.u32();
        const int8_t laps = r.i8();
        const float fraction = r.u16() / 65535.0f;
        r.i16();
        if (auto *player = result(race, avatar)) {
            player->laps = laps;
            player->fraction = fraction;
        }
    } else if (field == dc::DistributedRace::k_setPlace) {
        const uint32_t avatar = r.u32();
        RacingResult value;
        value.avatar = avatar;
        value.time = r.u32() / 1000.0f;
        value.place = r.u8();
        value.entryFee = r.u32();
        value.qualify = r.u8();
        value.winnings = r.u32();
        value.bonus = r.u32();
        r.blob();
        r.blob();
        r.u32();
        value.finished = true;
        if (r.ok())
            if (auto *player = result(race, avatar)) {
                value.kart = player->kart;
                *player = value;
            }
        if (avatar == avatarId_ && r.ok()) {
            race.finished = true;
            race.localPlace = value.place;
            race.phase = 5;
        }
        TT_LOG("racing: result avatar %lu place %u time %.3f fee %lu winnings %lu bonus %lu",
               (unsigned long)avatar, value.place, double(value.time),
               (unsigned long)value.entryFee, (unsigned long)value.winnings,
               (unsigned long)value.bonus);
    } else if (field == dc::DistributedRace::k_goToSpeedway) {
        uint32_t avatars[4];
        uint8_t count = 0;
        ids(r, avatars, 4, &count);
        race.returnReason = r.u8();
        for (int i = 0; i < count; ++i)
            if (avatars[i] == avatarId_)
                race.returning = true;
    } else if (field == dc::DistributedRace::k_setRaceZone) {
        race.nextZone = r.u32();
        race.track = uint16_t(r.u32());
    } else if (field == dc::DistributedRace::k_setCircuitPlace) {
        const uint32_t avatar = r.u32();
        const uint8_t place = r.u8();
        const uint32_t entryFee = r.u32(), winnings = r.u32(), bonus = r.u32();
        r.blob();
        if (r.ok())
            if (auto *player = result(race, avatar)) {
                player->place = place;
                player->entryFee = entryFee;
                player->winnings = winnings;
                player->bonus = bonus;
            }
    } else if (field == dc::DistributedRace::k_endCircuitRace)
        race.finished = true;
    else if (field == dc::DistributedRace::k_genGag) {
        const unsigned slot = r.u8();
        const uint16_t number = r.u16();
        const uint8_t index = r.u8();
        if (slot < 32 && index < 6)
            race.gags[slot] = RacingGagSlot{number, index, index != 0};
    } else if (field == dc::DistributedRace::k_hasGag) {
        const unsigned slot = r.u8();
        r.u8();
        r.u8();
        if (slot < 32)
            race.gags[slot].active = false;
    } else if (field == dc::DistributedRace::k_dropAnvilOn) {
        r.u32();
        race.hitAvatar = r.u32();
        r.i16();
        race.hitType = 3;
        ++race.hitRevision;
    } else if (field == dc::DistributedRace::k_shootPiejectile) {
        RacingShot shot;
        shot.source = r.u32();
        shot.target = r.u32();
        shot.type = r.u8();
        if (r.ok()) {
            shot.serial = ++race.shotSerial;
            race.shots[(shot.serial - 1) % 16] = shot;
        }
    } else if (field == dc::DistributedRace::k_racerDisconnected ||
               field == dc::DistributedRace::k_racerLeft) {
        if (auto *player = result(race, r.u32()))
            player->disconnected = true;
    } else if (field == dc::DistributedObject::k_setBarrierData) {
        const Bytes list = r.blob();
        DatagramReader data(list.data, list.len);
        int count = 0;
        while (data.remaining() && data.ok() && count < 8) {
            RacingBarrier entry;
            entry.context = data.u16();
            data.string(entry.name, sizeof(entry.name));
            uint32_t avatars[4];
            uint8_t n = 0;
            ids(data, avatars, 4, &n);
            for (int i = 0; i < n; ++i)
                entry.waiting |= avatars[i] == avatarId_;
            race.barriers[count++] = entry;
        }
        if (!data.ok() || data.remaining())
            r.fail();
        if (r.ok())
            race.numBarriers = count;
    } else
        skipField(r, field);
    if (r.ok() && !racingGenerating_) {
        ++race.revision;
        pushEvent(EventType::RacingRaceState, id);
    }
    return true;
}
bool Client::sendRacingEnter(uint32_t id, bool paid) {
    if (state_ != State::InZone)
        return false;
    bool found = false;
    for (const auto &b : racingBlocks_)
        found |= b.active && b.doId == id && !b.occupant;
    if (!found)
        return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, id, DistributedStartingBlock, requestEnter);
    w.u8(paid ? 1 : 0);
    return finish(w);
}
bool Client::sendRacingExit(uint32_t id) {
    bool found = false;
    for (const auto &b : racingBlocks_)
        found |= b.active && b.doId == id && b.occupant == avatarId_;
    if (state_ != State::InZone || !found || !avatarId_)
        return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, id, DistributedStartingBlock, requestExit);
    return finish(w);
}
bool Client::sendRacingMovieDone(uint32_t id) {
    bool found = false;
    for (const auto &b : racingBlocks_)
        found |= b.active && b.doId == id && b.occupant == avatarId_ && b.movie;
    if (state_ != State::InZone || !found || !avatarId_)
        return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, id, DistributedStartingBlock, movieFinished);
    return finish(w);
}
bool Client::sendRacingControl(uint32_t id) {
    bool owner = false;
    for (const auto &v : racingVehicles_)
        owner |= v.active && v.doId == id && v.owner == avatarId_;
    if (!raceParticipant() || !owner)
        return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, id, DistributedVehicle, requestControl);
    return finish(w);
}
bool Client::sendRacingPark(uint32_t id) {
    bool owner = false;
    for (const auto &v : racingVehicles_)
        owner |= v.active && v.doId == id && v.owner == avatarId_ && v.driver == avatarId_;
    if (!raceParticipant() || !owner)
        return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, id, DistributedVehicle, requestParked);
    return finish(w);
}
bool Client::sendRacingInput(uint32_t, int8_t) {
    return false;
} // clrecv-only, never legal outbound
bool Client::sendRacingThrow(uint32_t race, float x, float y, float z) {
    if (!raceParticipant() || race != racingRace_.doId || racingRace_.finished ||
        !std::isfinite(x + y + z) || std::fabs(x) > 3276.7f || std::fabs(y) > 3276.7f ||
        std::fabs(z) > 3276.7f)
        return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, race, DistributedRace, requestThrow);
    w.i32(int32_t(x * 1000));
    w.i32(int32_t(y * 1000));
    w.i32(int32_t(z * 1000));
    return finish(w);
}
bool Client::sendRacingGag(uint32_t race, uint8_t slot, uint8_t type, uint8_t index) {
    if (!raceParticipant() || race != racingRace_.doId || slot >= 32 || type < 1 || type > 4 ||
        index > 5 || racingRace_.finished)
        return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, race, DistributedRace, hasGag);
    w.u8(slot);
    w.u8(type);
    w.u8(index);
    return finish(w);
}
bool Client::sendRaceKart() {
    if (!raceParticipant())
        return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, racingRace_.doId, DistributedRace, requestKart);
    return finish(w);
}
bool Client::sendRaceBarrier(uint16_t context) {
    if (!raceParticipant())
        return false;
    RacingBarrier *entry = nullptr;
    for (auto &b : racingRace_.barriers)
        if (b.waiting && b.context == context)
            entry = &b;
    if (!entry)
        return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, racingRace_.doId, DistributedObject, setBarrierReady);
    w.u16(context);
    if (!finish(w))
        return false;
    entry->waiting = false;
    TT_LOG("racing: barrier %s context %u acknowledged", entry->name, context);
    return true;
}
bool Client::sendRaceProgress(int8_t laps, float fraction, uint32_t nowMs) {
    if (!raceParticipant() || !racingRace_.started || fraction < 0 || fraction > 1 ||
        !std::isfinite(fraction))
        return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, racingRace_.doId, DistributedRace, heresMyT);
    w.u32(avatarId_);
    w.i8(laps);
    w.u16(uint16_t(std::lround(fraction * 65535)));
    w.i16(networkTime(nowMs));
    return finish(w);
}
bool Client::sendRaceLeave() {
    if (!raceParticipant())
        return false;
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    TT_NET_UPDATE(w, racingRace_.doId, DistributedRace, racerLeft);
    w.u32(avatarId_);
    return finish(w);
}
bool Client::sendRaceVehiclePose(uint32_t id, float x, float y, float z, float h, float p,
                                 float roll, uint32_t nowMs) {
    bool controlled = false;
    for (const auto &v : racingVehicles_)
        controlled |= v.active && v.doId == id && v.owner == avatarId_ && v.driver == avatarId_ &&
                      v.state == 'C';
    if (!raceParticipant() || !controlled || !std::isfinite(x + y + z + h + p + roll) ||
        std::fabs(x) > 3276.7f || std::fabs(y) > 3276.7f || std::fabs(z) > 3276.7f)
        return false;
    static_assert((dc::DistributedSmoothNode::kw_setSmPosHpr & (dc::kKwClsend | dc::kKwOwnsend)) !=
                      0,
                  "pose is not authorized");
    DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
    // The AI grants this field explicitly on the Controlled vehicle, not the avatar.
    beginUpdate(w, id, dc::DistributedSmoothNode::k_setSmPosHpr);
    w.i16(int16_t(std::lround(x * 10)));
    w.i16(int16_t(std::lround(y * 10)));
    w.i16(int16_t(std::lround(z * 10)));
    for (float angle : {h, p, roll}) {
        angle = std::fmod(angle, 360.0f);
        if (angle < 0)
            angle += 360;
        w.i16(int16_t(std::lround(angle * 10)));
    }
    w.i16(networkTime(nowMs));
    return finish(w);
}
bool Client::sendRaceHazardHit(uint32_t id, uint32_t nowMs) {
    if (!raceParticipant() || racingRace_.finished)
        return false;
    for (const auto &hazard : racingHazards_)
        if (hazard.active && hazard.doId == id && hazard.race == racingRace_.doId &&
            hazard.owner != avatarId_) {
            DatagramWriter w = beginMessage(msg::CLIENT_OBJECT_SET_FIELD);
            if (hazard.projectile) {
                TT_NET_UPDATE(w, id, DistributedProjectile, hitSomebody);
            } else {
                TT_NET_UPDATE(w, id, DistributedGag, hitSomebody);
            }
            w.u32(avatarId_);
            w.i16(networkTime(nowMs));
            return finish(w);
        }
    return false;
}
#undef TT_NET_UPDATE
} // namespace tt::net
