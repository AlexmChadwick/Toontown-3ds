#include "net/ceo_state.h"
#include <cstring>
#include <initializer_list>
namespace tt::net {
CeoKind ceoKind(uint16_t classId) {
    if (classId == dc::DistributedBanquetTable::kClassId) return CeoKind::Table;
    if (classId == dc::DistributedGolfSpot::kClassId) return CeoKind::Golf;
    if (classId == dc::DistributedFoodBelt::kClassId) return CeoKind::Belt;
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
    const auto named = [name](const char* expected) { return !std::strcmp(name, expected); };
    if (named("setBossCogId")) next.bossId = reader.u32();
    else if (named("setIndex")) {
        next.index = reader.u8();
        const int limit = next.kind == CeoKind::Table ? 13 : next.kind == CeoKind::Golf ? 4 : 2;
        if (next.index >= limit) reader.fail();
    } else if (named("setNumDiners")) {
        next.numDiners = reader.u8();
        if (next.numDiners > 8) reader.fail();
    } else if (named("setDinerInfo")) {
        for (uint8_t* array : {next.hungry, next.eating, next.levels}) {
            const Bytes data = reader.blob();
            if (data.len > 8 || data.len != next.numDiners) reader.fail();
            if (reader.ok()) std::memcpy(array, data.data, data.len);
        }
    } else if (named("setDinerStatus")) {
        const uint8_t seat = reader.u8();
        const uint8_t status = reader.u8();
        if (seat >= next.numDiners || status > 5) reader.fail();
        else next.diners[seat] = status;
    } else if (named("setState")) {
        next.state = static_cast<char>(reader.u8());
        next.stateMs = nowMs;
        if (next.kind != CeoKind::Belt) {
            next.owner = reader.u32();
            next.extra = reader.i8();
            if (next.state != 'C' && next.state != 'L') next.owner = 0;
        }
        if (next.kind == CeoKind::Table && next.state == 'N') {
            for (int seat = 0; seat < next.numDiners; ++seat) next.diners[seat] = 1;
        }
    } else if (named("setPitcherPos") || named("setGolfSpotPos")) {
        next.sequence = reader.u8();
        next.heading = reader.u16() / 100.0f;
        next.stamp = reader.i16();
    } else if (named("firingWater")) {
        for (float& value : next.water) value = reader.i32() / 100.0f;
        next.waterMs = nowMs;
        ++next.waterRevision;
    } else if (named("setSwingInfo")) {
        next.golfPower = reader.u8();
        next.golfAngle = reader.i16() / 10.0f;
        next.golfSequence = reader.u8();
        if (next.golfPower > 100) reader.fail();
        next.golfMs = nowMs;
        ++next.golfRevision;
    } else if (named("setGoingToReward")) {
        next.owner = 0;
        next.state = 'O';
    } else if (!skipField(reader, fieldId)) return false;
    if (!reader.ok()) return false;
    ++next.revision;
    state = next;
    return true;
}
} // namespace tt::net
