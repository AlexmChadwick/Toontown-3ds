#include "net/cj_state.h"
#include <cstring>

namespace tt::net {
CjKind cjKind(uint16_t classId) {
    if (classId == dc::DistributedLawbotCannon::kClassId) return CjKind::Cannon;
    if (classId == dc::DistributedLawbotChair::kClassId) return CjKind::Chair;
    if (classId == dc::DistributedLawbotBossGavel::kClassId) return CjKind::Gavel;
    if (classId == dc::DistributedLawbotBossSuit::kClassId) return CjKind::Lawyer;
    return CjKind::None;
}

bool applyCjField(CjChild& state, uint16_t fieldId, DatagramReader& reader, uint32_t nowMs) {
    const dc::ClassDesc* cls = findClass(state.classId);
    if (!cls || cjKind(state.classId) == CjKind::None || !classHasField(*cls, fieldId)) {
        reader.fail();
        return false;
    }
    CjChild next = state;
    const char* name = fieldName(fieldId);
    const auto named = [name](const char* expected) { return std::strcmp(name, expected) == 0; };
    if (named("setBossCogId")) {
        next.bossId = reader.u32();
    } else if (named("setIndex")) {
        next.index = reader.u8();
        const int limit = next.kind == CjKind::Chair ? 12 : 8;
        if (next.index >= limit) reader.fail();
    } else if (named("setState")) {
        next.state = static_cast<char>(reader.u8());
        next.stateMs = nowMs;
    } else if (named("setPosHpr")) {
        next.x = reader.i16() / 10.0f;
        next.y = reader.i16() / 10.0f;
        next.z = reader.i16() / 10.0f;
        next.h = reader.i16() / 10.0f;
        next.p = reader.i16() / 10.0f;
        next.r = reader.i16() / 10.0f;
    } else if (named("setDNAString")) {
        const Bytes dna = reader.blob();
        if (dna.len >= sizeof(next.dna)) reader.fail();
        else if (reader.ok()) {
            std::memcpy(next.dna, dna.data, dna.len);
            next.dna[dna.len] = 0;
        }
    } else if (named("setMovie")) {
        next.movie = reader.i8();
        next.owner = reader.u32();
        next.balls = reader.u8();
        next.pendingFire = false;
        next.inFlight = false;
        next.chairReported = false;
        if (next.movie != 1) next.owner = 0;
    } else if (named("updateCannonPosition")) {
        const uint32_t owner = reader.u32();
        const float rotation = reader.i16() / 10.0f;
        const float elevation = reader.i16() / 10.0f;
        if (owner == next.owner) {
            next.rotation = rotation;
            next.elevation = elevation;
        }
    } else if (named("setCannonWillFire")) {
        next.owner = reader.u32();
        next.fireDelay = reader.i16() / 10.0f;
        next.rotation = reader.i16() / 10.0f;
        next.elevation = reader.i16() / 10.0f;
        next.fireStamp = reader.i16();
        next.fireMs = nowMs;
        ++next.fireRevision;
        next.pendingFire = false;
        next.inFlight = true;
        next.chairReported = false;
    } else if (named("setToonJurorIndex")) {
        next.juror = reader.i8();
        if (next.juror < -1 || next.juror >= 8) reader.fail();
    } else if (named("showCogJurorFlying")) {
        next.flyingMs = nowMs;
        ++next.flyingRevision;
    } else if (named("doAttack")) {
        for (float& component : next.attack) component = reader.i16() / 10.0f;
        next.attackMs = nowMs;
        ++next.attackRevision;
    } else if (named("doProsecute")) {
        next.state = 'P';
        next.stateMs = nowMs;
    } else if (named("doStun")) {
        next.state = 'S';
        next.stunMs = nowMs;
        ++next.stunRevision;
    } else if (!skipField(reader, fieldId)) {
        return false;
    }
    if (!reader.ok()) return false;
    ++next.revision;
    state = next;
    return true;
}
} // namespace tt::net
