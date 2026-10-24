// Lawbot child snapshots; no platform dependencies or per-frame allocation.
#pragma once
#include "net/datagram.h"

namespace tt::net {
// Globals and AI bounds: 8 cannons, 12 chairs, 8 gavels, 10 lawyers.
constexpr int kMaxCjChildren = 38;
enum class CjKind : uint8_t { None, Cannon, Chair, Gavel, Lawyer };
struct CjChild {
    uint32_t doId = 0;
    uint32_t bossId = 0;
    uint16_t classId = 0;
    CjKind kind = CjKind::None;
    bool active = false;
    uint8_t index = 0;
    char state = 'F';
    int8_t juror = -1;
    uint32_t stateMs = 0;
    uint32_t revision = 0;
    float x = 0;
    float y = 0;
    float z = 0;
    float h = 0;
    float p = 0;
    float r = 0;
    char dna[8] = {};
    int8_t movie = 2; // CannonGlobals.CANNON_MOVIE_CLEAR.
    uint32_t owner = 0;
    uint8_t balls = 0;
    float rotation = 0;
    float elevation = 10;
    float fireDelay = 0;
    int16_t fireStamp = 0;
    uint32_t fireMs = 0;
    uint32_t fireRevision = 0;
    bool pendingFire = false;
    bool inFlight = false;
    bool chairReported = false;
    float attack[6] = {};
    uint32_t attackMs = 0;
    uint32_t attackRevision = 0;
    uint32_t stunMs = 0;
    uint32_t stunRevision = 0;
    uint32_t flyingMs = 0;
    uint32_t flyingRevision = 0;
};
CjKind cjKind(uint16_t classId);
bool applyCjField(CjChild& state, uint16_t fieldId, DatagramReader& reader, uint32_t nowMs);
} // namespace tt::net
