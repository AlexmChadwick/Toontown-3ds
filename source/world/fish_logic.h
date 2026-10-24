// Pure fishing rules ported from toontown/fishing/FishGlobals.py: catch rolls, weights, values,
// and the cast projectile of DistributedFishingSpot.moveBobTask. No 3DS headers, so
// tools/fishing_test builds it on the host. Data tables: fish_tables.* (tools/gen_fish_tables.py).
#pragma once

#include <cstdint>

#include "world/fish_tables.h"

namespace tt::world::fish {

// xorshift32; seed with anything non-zero.
struct Rng {
    uint32_t state = 0x9E3779B9u;
    void seed(uint32_t s) { state = s ? s : 0x9E3779B9u; }
    uint32_t next() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
    float unit() { return (next() >> 8) * (1.0f / 16777216.0f); } // [0, 1)
    uint32_t below(uint32_t n) { return n ? next() % n : 0; }
};

enum class CatchKind : uint8_t { Nothing, Fish, Jellybeans, Boot };
struct Catch {
    CatchKind kind = CatchKind::Nothing;
    uint8_t genus = 0, species = 0;
    uint16_t weight = 0; // ounces (FishGlobals weights are pounds * 16)
    uint16_t value = 0;  // fish: jellybeans when sold
    uint16_t beans = 0;  // jellybean catch
};

const Species* findSpecies(int genus, int species);
const char* genusName(int genus);
// "N lb. M oz." text as FishPanel shows it (TTLocalizer.FishPageWeight*): fills buf.
void weightText(int weightOz, char* buf, int cap);

// FishGlobals.getValue (no holiday multiplier).
int fishValue(int genus, int species, int weightOz);

// FishGlobals.getRandomFishVitals(zoneId, rodId): false when the rarity roll hits an empty list.
bool randomFishVitals(int area, int rod, Rng& rng, int* genus, int* species, int* weightOz);

// The AI's pull-in roll: ProbabilityDict (1..100: fish <= 93, jellybeans == 94, else boot).
Catch rollCatch(int area, int rod, Rng& rng);

// FishingTargetGlobals: water level relative to the spot, and the fish region of a pond area.
float waterLevel(int area);
bool pondRegion(int area, float* cx, float* cy, float* radius);

// DistributedFishingSpot.moveBobTask. power in [0, 1]. Returns the landing point in the
// spot's aim frame (x right, y forward) and the flight time; the bob starts at (0, 3, 8.5).
struct Landing {
    float x, y, t;
};
Landing castLanding(float power, float waterLevel);
constexpr float kBobStartY = 3.0f, kBobStartZ = 8.5f;
constexpr float kVZeroMax = 25.0f, kAngleMaxDeg = 30.0f, kGravity = 32.2f;

} // namespace tt::world::fish
