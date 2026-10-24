#include "net/boss_state.h"

#include <cstring>

namespace tt::net {

CfoKind cfoKind(uint16_t classId) {
    if (classId == dc::DistributedCashbotBossCrane::kClassId) return CfoKind::Crane;
    if (classId == dc::DistributedCashbotBossSafe::kClassId) return CfoKind::Safe;
    if (classId == dc::DistributedCashbotBossGoon::kClassId) return CfoKind::Goon;
    if (classId == dc::DistributedCashbotBossTreasure::kClassId) return CfoKind::Treasure;
    return CfoKind::None;
}

bool applyCfoField(CfoChild& state, uint16_t fieldId, DatagramReader& reader, uint32_t nowMs) {
    const dc::ClassDesc* cls = findClass(state.classId);
    if (!cls || cfoKind(state.classId) == CfoKind::None || !classHasField(*cls, fieldId)) {
        reader.fail();
        return false;
    }
    CfoChild next = state;
    const char* name = fieldName(fieldId);
    const auto named = [name](const char* expected) { return std::strcmp(name, expected) == 0; };
    if (named("setBossCogId")) {
        next.bossId = reader.u32();
    } else if (named("setIndex")) {
        next.index = reader.u8();
        if ((next.kind == CfoKind::Crane && next.index >= 4) ||
            (next.kind == CfoKind::Safe && next.index >= 9)) reader.fail();
    } else if (named("setState") || named("setObjectState")) {
        const char mode = static_cast<char>(reader.u8());
        if (next.state != mode) next.stateMs = nowMs;
        next.state = mode;
        next.owner = reader.u32();
        if (named("setObjectState")) next.crane = reader.u32();
        next.rejected = false;
    } else if (named("setCablePos")) {
        next.sequence = reader.u8();
        next.armY = reader.i16() / 100.0f;
        next.armH = reader.u16() / 100.0f;
        for (auto& link : next.links) {
            for (float& component : link) component = reader.i16() / 100.0f;
        }
        next.cableStamp = reader.i16();
        next.cableMs = nowMs;
    } else if (named("clearSmoothing")) {
        reader.i8();
        next.cableMs = 0;
    } else if (named("rejectGrab")) {
        next.rejected = true;
    } else if (named("destroyGoon")) {
        next.state = 'O';
        next.stateMs = nowMs;
    } else if (named("setVelocity")) {
        next.velocity = reader.u8() / 10.0f;
    } else if (named("setHFov")) {
        next.fov = reader.u8();
    } else if (named("setAttackRadius")) {
        next.radius = reader.u8();
    } else if (named("setStrength")) {
        next.strength = reader.u8();
    } else if (named("setGoonScale")) {
        next.scale = reader.u8() / 50.0f;
    } else if (named("setupGoon")) {
        next.velocity = reader.u8() / 10.0f;
        next.fov = reader.u8();
        next.radius = reader.u8();
        next.strength = reader.u8();
        next.scale = reader.u8() / 50.0f;
    } else if (named("setTarget")) {
        next.targetX = reader.i16() / 10.0f;
        next.targetY = reader.i16() / 10.0f;
        next.targetH = reader.u16() / 100.0f;
        next.arrivalStamp = reader.i16();
        next.targetMs = nowMs;
    } else if (named("setGrab")) {
        next.owner = reader.u32();
        next.grabbed = next.owner != 0;
    } else if (named("setReject")) {
        next.rejected = true;
    } else if (named("setStyle")) {
        next.style = reader.u16();
    } else if (named("setPosition") || named("setFinalPosition")) {
        next.x = reader.i16() / 10.0f;
        next.y = reader.i16() / 10.0f;
        next.z = reader.i16() / 10.0f;
    } else if (named("setX") || named("setComponentX")) {
        next.x = reader.i16() / 10.0f;
    } else if (named("setY") || named("setComponentY")) {
        next.y = reader.i16() / 10.0f;
    } else if (named("setZ") || named("setComponentZ")) {
        next.z = reader.i16() / 10.0f;
    } else if (named("setH") || named("setComponentH")) {
        next.h = reader.i16() / 10.0f;
    } else if (named("setPos") || named("setPosHpr") || named("setXYZH") ||
               named("setSmPos") || named("setSmXYZH") || named("setSmPosHpr") || named("setSmPosHprL") ||
               named("setXY") || named("setXYH") || named("setSmXY") || named("setSmXYH") ||
               named("setXZ") || named("setSmXZ") || named("setHpr") || named("setSmHpr") ||
               named("setSmH") || named("setSmZ")) {
        // Molecular fields are concatenated components, with no array framing.
        if (named("setSmPosHprL")) reader.u64();
        const bool smooth = std::strstr(name, "setSm") == name;
        const char* components = name + (smooth ? 5 : 3);
        if (std::strstr(components, "Pos")) {
            next.x = reader.i16() / 10.0f;
            next.y = reader.i16() / 10.0f;
            next.z = reader.i16() / 10.0f;
        } else {
            if (std::strchr(components, 'X')) next.x = reader.i16() / 10.0f;
            if (std::strchr(components, 'Y')) next.y = reader.i16() / 10.0f;
            if (std::strchr(components, 'Z')) next.z = reader.i16() / 10.0f;
        }
        if (std::strchr(components, 'H')) next.h = reader.i16() / 10.0f;
        if (std::strstr(components, "Hpr")) {
            reader.i16();
            reader.i16();
        }
        if (smooth) reader.i16();
    } else if (!skipField(reader, fieldId)) {
        return false;
    }
    if (!reader.ok()) return false;
    ++next.revision;
    state = next;
    return true;
}

} // namespace tt::net
