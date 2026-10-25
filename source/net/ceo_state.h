#pragma once
#include "net/datagram.h"
namespace tt::net {
// BossbotBossDifficultySettings: at most 13 tables × 8 seats, 4 spots, 2 belts.
constexpr int kMaxCeoChildren = 19;
enum class CeoKind : uint8_t { None, Table, Golf, Belt };
struct CeoChild {
    uint32_t doId = 0;
    uint32_t bossId = 0;
    uint16_t classId = 0;
    CeoKind kind = CeoKind::None;
    bool active = false;
    bool impactReported = false;
    uint8_t index = 0;
    uint8_t numDiners = 0;
    uint8_t hungry[8] = {};
    uint8_t eating[8] = {};
    uint8_t levels[8] = {};
    uint8_t diners[8] = {};
    char state = 'F';
    uint32_t owner = 0;
    int8_t extra = 0;
    uint32_t stateMs = 0;
    uint32_t revision = 0;
    uint8_t sequence = 0;
    float heading = 0;
    int16_t stamp = 0;
    float water[6] = {};
    uint32_t waterMs = 0;
    uint32_t waterRevision = 0;
    uint8_t golfPower = 0;
    float golfAngle = 0;
    uint8_t golfSequence = 0;
    uint32_t golfMs = 0;
    uint32_t golfRevision = 0;
};
CeoKind ceoKind(uint16_t classId);
bool applyCeoField(CeoChild& state, uint16_t fieldId, DatagramReader& reader, uint32_t nowMs);
} // namespace tt::net
