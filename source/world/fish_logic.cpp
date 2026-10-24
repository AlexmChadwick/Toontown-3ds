#include "world/fish_logic.h"

#include <cmath>
#include <cstdio>

namespace tt::world::fish {

const Species* findSpecies(int genus, int species) {
    for (int i = 0; i < kNumSpecies; i++) {
        if (kSpecies[i].genus == genus && kSpecies[i].species == species) return &kSpecies[i];
    }
    return nullptr;
}

const char* genusName(int genus) {
    return genus >= 0 && genus <= kMaxGenus && kGenusNames[genus] ? kGenusNames[genus] : "Fish";
}

void weightText(int weightOz, char* buf, int cap) {
    const int lb = weightOz / 16, oz = weightOz % 16;
    snprintf(buf, static_cast<size_t>(cap), "%d %s %d oz.", lb, lb == 1 ? "lb." : "lbs.", oz);
}

int fishValue(int genus, int species, int weightOz) {
    const Species* s = findSpecies(genus, species);
    if (!s) return 1;
    const float rarityValue = powf(kRarityValueScale * s->rarity, 1.5f);
    const float weightValue = powf(kWeightValueScale * weightOz, 1.1f);
    return static_cast<int>(ceilf(kOverallValueScale * (rarityValue + weightValue)));
}

namespace {

const PondRow* findRows(int area, int rod, int rarity, int* count) {
    // Rows are sorted by (area, rod, rarity); the table is small enough for a linear scan.
    for (int i = 0; i < kNumPondRows; i++) {
        const PondRow& r = kPondRows[i];
        if (r.area == area && r.rod == rod && r.rarity == rarity) {
            *count = r.count;
            return &r;
        }
    }
    *count = 0;
    return nullptr;
}

bool knownArea(int area) {
    for (int i = 0; i < kNumPondRows; i++) {
        if (kPondRows[i].area == area) return true;
    }
    return false;
}

// FishGlobals.__rollRarityDice
int rollRarity(int rod, Rng& rng) {
    const float roll = rng.unit();
    const int rarity = static_cast<int>(ceilf(10.0f * (1.0f - powf(roll, kRods[rod].rarityExp))));
    return rarity <= 0 ? 1 : rarity;
}

} // namespace

bool randomFishVitals(int area, int rod, Rng& rng, int* genus, int* species, int* weightOz) {
    if (rod < 0 || rod >= kNumRods) rod = 0;
    if (!knownArea(area)) area = 0; // no pond table: only the "anywhere" species
    const int rarity = rollRarity(rod, rng);
    int count = 0;
    const PondRow* row = findRows(area, rod, rarity, &count);
    if (!row || !count) return false;
    const uint8_t* pick = kPool[row->first + rng.below(static_cast<uint32_t>(count))];
    *genus = pick[0];
    *species = pick[1];
    // getRandomWeight: the mean of two rolls inside the fish range clipped to the rod's range.
    const Species* s = findSpecies(pick[0], pick[1]);
    float lo = s->minLb, hi = s->maxLb;
    if (kRods[rod].minLb > lo) lo = kRods[rod].minLb;
    if (kRods[rod].maxLb < hi) hi = kRods[rod].maxLb;
    const float mean = (rng.unit() + rng.unit()) * 0.5f;
    *weightOz = static_cast<int>(lroundf((lo + (hi - lo) * mean) * 16.0f));
    return true;
}

Catch rollCatch(int area, int rod, Rng& rng) {
    Catch c;
    const int roll = 1 + static_cast<int>(rng.below(100));
    if (roll <= kFishCutoff) {
        int g, s, w;
        if (randomFishVitals(area, rod, rng, &g, &s, &w)) {
            c.kind = CatchKind::Fish;
            c.genus = static_cast<uint8_t>(g);
            c.species = static_cast<uint8_t>(s);
            c.weight = static_cast<uint16_t>(w);
            c.value = static_cast<uint16_t>(fishValue(g, s, w));
        }
    } else if (roll <= kBeanCutoff) {
        c.kind = CatchKind::Jellybeans;
        c.beans = kRods[rod >= 0 && rod < kNumRods ? rod : 0].beans;
    } else {
        c.kind = CatchKind::Boot;
    }
    return c;
}

float waterLevel(int area) {
    for (int i = 0; i < kNumWater; i++) {
        if (kWater[i].area == area) return kWater[i].level;
    }
    return 0.0f; // FishingTargetGlobals.getWaterLevel default
}

bool pondRegion(int area, float* cx, float* cy, float* radius) {
    for (int i = 0; i < kNumPondAreas; i++) {
        if (kPondAreas[i].area == area) {
            *cx = kPondAreas[i].cx;
            *cy = kPondAreas[i].cy;
            *radius = kPondAreas[i].radius;
            return true;
        }
    }
    return false;
}

Landing castLanding(float power, float water) {
    const float vZero = power * kVZeroMax;
    const float angle = power * kAngleMaxDeg * 3.14159265f / 180.0f;
    const float vy = vZero * cosf(angle), vz = vZero * sinf(angle);
    // z(t) = z0 + vz t - g t^2 / 2 = water  ->  positive root.
    const float c = kBobStartZ - water;
    const float disc = vz * vz + 2.0f * kGravity * c;
    const float t = disc > 0.0f ? (vz + sqrtf(disc)) / kGravity : 0.0f;
    return Landing{0.0f, kBobStartY + vy * t, t};
}

} // namespace tt::world::fish
