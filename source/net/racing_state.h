#pragma once
#include <cstdint>
namespace tt::net {
constexpr int kMaxRacingPads = 24;
constexpr int kMaxRacingBlocks = 96;
constexpr int kMaxRacingVehicles = 16;
constexpr int kRacePlayers = 4; // RaceGlobals.MaxRacers
struct RacingPad {
    uint32_t doId = 0, area = 0, raceZone = 0, stateMs = 0;
    char state[20] = {};
    uint16_t trackIds[8] = {};
    uint8_t trackCount = 0;
    bool active = false, viewing = false;
};
struct RacingBlock {
    uint32_t doId = 0, padDoId = 0, occupant = 0, revision = 0;
    float x = 0, y = 0, z = 0, h = 0, p = 0, r = 0;
    uint8_t location = 0, movie = 0, rejection = 0;
    bool active = false, viewing = false;
};
struct RacingVehicle {
    uint32_t doId = 0, owner = 0, driver = 0, poseMs = 0;
    char state = 'P';
    int8_t body = -1, bodyColor = -1, accessoryColor = -1;
    int8_t engine = -1, spoiler = -1, frontWheels = -1, backWheels = -1, rims = -1, decal = -1;
    int8_t input = 0;
    float x = 0, y = 0, z = 0, h = 0;
    bool active = false;
};
struct RacingResult {
    uint32_t avatar = 0, kart = 0, entryFee = 0, winnings = 0, bonus = 0;
    float fraction = 0, time = 0;
    int8_t laps = 0;
    uint8_t place = 0, startingPlace = 0;
    bool finished = false, qualify = false, disconnected = false;
};
struct RacingBarrier {
    uint16_t context = 0;
    char name[32] = {};
    bool waiting = false;
};
struct RacingGagSlot {
    uint16_t generation = 0;
    uint8_t index = 0;
    bool active = false;
};
struct RacingShot {
    uint32_t serial = 0, source = 0, target = 0;
    uint8_t type = 0;
};
struct RacingRace {
    uint32_t doId = 0, zone = 0, nextZone = 0, revision = 0;
    uint16_t track = 0, raceType = 0, laps = 3;
    uint32_t racers[4] = {};
    RacingResult results[4];
    RacingBarrier barriers[8];
    RacingGagSlot gags[32];
    RacingShot shots[16];
    uint32_t shotSerial = 0;
    uint8_t circuitCount = 0;
    uint8_t numRacers = 0, localPlace = 0, localLap = 0, phase = 0, returnReason = 0;
    int numBarriers = 0;
    int16_t startTs = 0;
    uint32_t startMs = 0, hitAvatar = 0, hitRevision = 0;
    uint8_t hitType = 0;
    bool started = false, finished = false, returning = false, active = false;
};
struct RacingHazard {
    uint32_t doId = 0, race = 0, owner = 0, initMs = 0;
    float x = 0, y = 0, z = 0;
    uint8_t type = 0;
    uint16_t activate = 0;
    bool active = false, projectile = false;
};
} // namespace tt::net
