#pragma once
#include "net/datagram.h"
namespace tt::net {
// Stock CogdoGameConsts.MaxPlayers=4; bounded pools include 10 barrels, 4 cranes,
// 8 money bags and the interior/game/Cog. No per-frame network allocation.
constexpr int kMaxCogdoObjects = 32;
constexpr int kMaxCogdoSuits = 64;
enum class CogdoKind : uint8_t { None, Interior, Maze, Flying, CraneGame, Boardroom, Barrel, Crane, MoneyBag, Cog };
enum class CogdoGamePhase : uint8_t { Hidden, Visible, Intro, Playing, Finished };
struct CogdoState {
    uint32_t doId = 0;
    uint16_t classId = 0;
    CogdoKind kind = CogdoKind::None;
    bool active = false;
    uint32_t parent = 0;
    uint32_t zone = 0;
    uint32_t exterior = 0;
    uint32_t npc = 0;
    uint32_t sos = 0;
    int8_t foType = 0;
    int8_t numFloors = 0;
    int32_t difficulty = -1;
    int32_t playground = -1;
    uint32_t index = 0;
    char state[32] = {};
    int16_t stamp = 0;
    uint32_t stateMs = 0;
    uint32_t startMs = 0;
    uint32_t finishMs = 0;
    uint32_t revision = 0;
    CogdoGamePhase phase = CogdoGamePhase::Hidden;
    uint32_t toons[4] = {};
    uint8_t numToons = 0;
    uint32_t suits[32] = {};
    uint8_t numSuits = 0;
    uint32_t reserves[32] = {};
    uint8_t numReserves = 0;
    uint8_t suitCounts[3] = {};
    uint8_t suitHp[kMaxCogdoSuits] = {};
    bool pickups[256] = {};
    uint16_t memos = 0;
    bool grabbed = false;
    bool hasGag = false;
    uint32_t gagMs = 0;
    uint32_t hitMs = 0;
    bool doorOpen = false;
    bool enteredDoor = false;
    uint32_t owner = 0;
    uint32_t crane = 0;
    uint32_t action = 0;
    uint32_t actionData = 0;
    uint32_t actionRevision = 0;
    float x = 0, y = 0, z = 0, h = 0, p = 0, r = 0;
    float armY = 20;
    float armH = 0;
    float links[3][3] = {};
    uint8_t sequence = 0;
    char dna[8] = {};
    uint32_t rewards[4] = {};
    uint8_t rewardValues[4] = {};
};
CogdoKind cogdoKind(uint16_t classId);
bool applyCogdoField(CogdoState& state, uint16_t field, DatagramReader& reader, uint32_t nowMs, uint32_t ownId);
} // namespace tt::net
