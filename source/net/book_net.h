// Shtiker Book network data structures and parsers (docs/design/shtiker_book.md).
#pragma once

#include <cstdint>
#include <cstring>
#include "net/datagram.h"

namespace tt::net {

constexpr int kNumCogs = 32;
constexpr int kNumDepts = 4;
constexpr int kNumGagTracks = 7;
constexpr int kMaxFishCollection = 70;
constexpr int kMaxSosCards = 16;
constexpr int kKartingTrophies = 33;
constexpr int kKartingHistory = 16;
constexpr int kGolfHistory = 18;
constexpr int kGolfTrophies = 30;
constexpr int kGolfCups = 3;

// Cog Status enum (CogPageGlobals.py)
enum CogStatus : uint32_t {
    kCogUnseen = 1,
    kCogBattled = 2,
    kCogDefeated = 3,
    kCogComplete1 = 4,
    kCogComplete2 = 5,
};

struct FishCollectionEntry {
    uint8_t genus = 0;
    uint8_t species = 0;
    uint16_t maxWeight = 0; // ounces
};

struct SosEntry {
    uint16_t npcId = 0;
    uint8_t count = 0;
};

struct OwnBookStats {
    // Gags & Track training progress
    int8_t trackProgressId = -1; // -1 none, 0..6 track
    uint32_t trackProgress = 0;  // bitmask (15 frames)

    // Cog Gallery
    uint32_t cogStatus[kNumCogs] = {};
    uint32_t cogCount[kNumCogs] = {};
    uint8_t cogRadar[kNumDepts] = {};
    uint8_t buildingRadar[kNumDepts] = {};

    // Disguise
    uint32_t cogParts[kNumDepts] = {};   // suit parts bitmask
    uint16_t cogMerits[kNumDepts] = {};  // current merits
    uint8_t cogTypes[kNumDepts] = {};    // cog type 0..7
    uint8_t cogLevels[kNumDepts] = {};   // cog level

    // Fish Album
    FishCollectionEntry fishCollection[kMaxFishCollection] = {};
    uint8_t numFishCollected = 0;
    uint8_t fishingTrophies[10] = {};
    uint8_t numFishingTrophies = 0;

    // Trophies - Karting
    uint8_t kartingTrophies[kKartingTrophies] = {};
    uint32_t tickets = 0;
    uint8_t kartingHistory[kKartingHistory] = {};

    // Trophies - Golf
    uint16_t golfHistory[kGolfHistory] = {};
    uint8_t golfHoleBest[18] = {};
    uint8_t golfCourseBest[3] = {};

    // SOS Cards
    SosEntry sosCards[kMaxSosCards] = {};
    uint8_t numSosCards = 0;

    // Version counter for UI redraw caching
    uint32_t version = 1;

    void initDefaults();
};

bool parseOwnBookField(DatagramReader& r, uint16_t fieldId, OwnBookStats& out);

// Helpers
int calcGolfTrophies(const uint16_t history[kGolfHistory], bool trophiesOut[kGolfTrophies]);
int calcGolfCups(const uint16_t history[kGolfHistory], bool cupsOut[kGolfCups]);

} // namespace tt::net
