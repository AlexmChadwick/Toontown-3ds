#include "net/ceo_state.h"

#include <cstring>
#include <initializer_list>

namespace tt::net {

CeoKind ceoKind(uint16_t classId) {
    if (classId == dc::DistributedBanquetTable::kClassId) {
        return CeoKind::Table;
    }
    if (classId == dc::DistributedGolfSpot::kClassId) {
        return CeoKind::Golf;
    }
    if (classId == dc::DistributedFoodBelt::kClassId) {
        return CeoKind::Belt;
    }
    return CeoKind::None;
}

bool applyCeoField(CeoChild& state, uint16_t fieldId, DatagramReader& reader, uint32_t nowMs) {
    const dc::ClassDesc* cls = findClass(state.classId);
    if (!cls || ceoKind(state.classId) == CeoKind::None || !classHasField(*cls, fieldId)) {
        reader.fail();
        return false;
    }

    CeoChild next = state;
    const char* name = fieldName(fieldId);
    const auto named = [name](const char* expected) {
        return !std::strcmp(name, expected);
    };

    if (named("setBossCogId")) {
        next.bossId = reader.u32();
    } else if (named("setIndex")) {
        next.index = reader.u8();
        int limit = kCeoMaxTables;
        if (next.kind == CeoKind::Golf) {
            limit = kCeoMaxGolfSpots;
        } else if (next.kind == CeoKind::Belt) {
            limit = kCeoMaxFoodBelts;
        }
        if (next.index >= limit) {
            reader.fail();
        }
    } else if (named("setNumDiners")) {
        next.numDiners = reader.u8();
        if (next.numDiners > kCeoMaxDinersPerTable) {
            reader.fail();
        }
    } else if (named("setDinerInfo")) {
        // Upstream DistributedBanquetTableAI sends three blobs: hungry, eating, levels.
        uint32_t count = next.numDiners;
        for (uint8_t* array : {next.hungry, next.eating, next.levels}) {
            const Bytes data = reader.blob();
            if (data.len > kCeoMaxDinersPerTable || (count && data.len != count)) {
                reader.fail();
            }
            if (!count) {
                count = data.len;
            }
            if (reader.ok()) {
                std::memcpy(array, data.data, data.len);
            }
        }
        if (!next.numDiners) {
            next.numDiners = static_cast<uint8_t>(count);
        }
    } else if (named("setDinerStatus")) {
        const uint8_t seat = reader.u8();
        const uint8_t status = reader.u8();
        if (seat >= kCeoMaxDinersPerTable || (next.numDiners && seat >= next.numDiners) ||
            status > kDinerStatusHidden) {
            reader.fail();
        } else {
            next.diners[seat] = status;
        }
    } else if (named("setState")) {
        next.state = static_cast<char>(reader.u8());
        next.stateMs = nowMs;
        if (next.kind != CeoKind::Belt) {
            next.owner = reader.u32();
            next.extra = reader.i8();
            // Upstream DistributedBanquetTableAI: Controlled ('C') and Flat ('L') keep the owner.
            if (next.state != kTableStateControlled && next.state != kTableStateFlat) {
                next.owner = 0;
            }
        }
        // When table turns on ('N'), diners begin hungry (kDinerStatusHungry).
        if (next.kind == CeoKind::Table && next.state == kTableStateOn) {
            for (int seat = 0; seat < next.numDiners; ++seat) {
                next.diners[seat] = kDinerStatusHungry;
            }
        }
    } else if (named("setPitcherPos") || named("setGolfSpotPos")) {
        next.sequence = reader.u8();
        next.heading = reader.u16() / 100.0f;
        next.stamp = reader.i16();
    } else if (named("firingWater")) {
        for (float& value : next.water) {
            value = reader.i32() / 100.0f;
        }
        next.waterMs = nowMs;
        ++next.waterRevision;
    } else if (named("setSwingInfo")) {
        next.golfPower = reader.u8();
        next.golfAngle = reader.i16() / 10.0f;
        next.golfSequence = reader.u8();
        if (next.golfPower > 100) {
            reader.fail();
        }
        next.golfMs = nowMs;
        ++next.golfRevision;
    } else if (named("setGoingToReward")) {
        next.owner = 0;
        next.state = kGolfStateOff;
    } else if (!skipField(reader, fieldId)) {
        return false;
    }

    if (!reader.ok()) {
        return false;
    }

    ++next.revision;
    state = next;
    return true;
}

} // namespace tt::net
