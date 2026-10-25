#include "ui/clerk_rules.h"
#include "ui/clerk_tables.h"
#include "world/py_random.h"
#include <algorithm>
#include <cmath>
namespace tt::clerks {
namespace {
// CPython Random.random and gauss (Box-Muller with a cached second variate).
class PetRandom {
public:
    explicit PetRandom(uint64_t seed) : random_(seed) {}
    uint32_t below(uint32_t count) {
        return random_.randbelow(count);
    }
    double unit() {
        const uint32_t high = random_.getrandbits(27);
        const uint32_t low = random_.getrandbits(26);
        return (high * 67108864.0 + low) / 9007199254740992.0;
    }
    double normal(double low, double high) {
        for (;;) {
            double z = nextNormal_;
            if (cached_)
                cached_ = false;
            else {
                const double angle = unit() * 6.283185307179586;
                const double radius = std::sqrt(-2 * std::log(1 - unit()));
                z = std::cos(angle) * radius;
                nextNormal_ = std::sin(angle) * radius;
                cached_ = true;
            }
            const double sigma = (high - low) / 6;
            const double value = (low + high) * .5 + z * sigma;
            if (value >= low && value <= high)
                return value;
        }
    }

private:
    world::PyRandom random_;
    bool cached_ = false;
    double nextNormal_ = 0;
};
} // namespace
bool generatePet(uint32_t seed, uint32_t zone, PetOffer *output) {
    int hood = -1;
    for (int index = 0; index < 6; ++index)
        if (kHoodPets[index].zone == zone)
            hood = index;
    if (hood < 0 || !output)
        return false;
    const auto &data = kHoodPets[hood];
    PetOffer pet;
    PetRandom random(seed);
    const int sizes[] = {2, 6, 5, 5};
    for (int index = 0; index < 4; ++index)
        pet.dna[index] = int(random.below(sizes[index])) - 1;
    int total = 0;
    for (int index = 0; index < data.count; ++index)
        total += data.weights[index];
    const double roll = random.unit() * total;
    for (int index = 0; index < data.count; ++index) {
        total -= data.weights[index];
        if (total <= roll) {
            pet.dna[4] = data.bodies[index];
            break;
        }
    }
    pet.dna[5] = random.below(pet.dna[4] < 4 ? 23 : 17);
    pet.dna[6] = random.below(9);
    pet.dna[7] = random.below(6);
    pet.dna[8] = random.below(2);
    pet.traitSeed = static_cast<uint32_t>(random.unit() * 2147483647.0);
    PetRandom names(uint64_t(seed) + zone);
    if (pet.dna[8] == 0)
        pet.nameIndex = kBoyNames[names.below(sizeof(kBoyNames) / sizeof(kBoyNames[0]))];
    else
        pet.nameIndex = kGirlNames[names.below(sizeof(kGirlNames) / sizeof(kGirlNames[0]))];
    PetRandom traits(pet.traitSeed);
    const double forgetMax[] = {1, .9, .8, .7, .6, .5};
    const double incMin[] = {.2, .3, .4, .5, .6, .7}, incMax[] = {.65, .7, .75, .8, .85, .9};
    const double decMin[] = {.35, .3, .25, .2, .15, .1}, decMax[] = {.8, .7, .6, .5, .4, .3};
    double value = 0;
    for (int index = 0; index < 13; ++index) {
        const bool decreasing = index == 0 || index == 3 || index == 9 || index == 12;
        const double low = index == 0 ? 0 : decreasing ? decMin[hood] : incMin[hood];
        const double high = index == 0 ? forgetMax[hood] : decreasing ? decMax[hood] : incMax[hood];
        const double trait = int(traits.normal(low, high) * 10000) / 10000.0;
        // PetTraits skips surprise's worth, but divides by all 13 traits.
        if (index != 11)
            value += decreasing ? 1 - trait : trait;
    }
    value = std::max(0.0, value / 13 - .3);
    const double rarity = kBodyRarity[pet.dna[4]] * (1 - value);
    pet.cost = int((std::pow(.001, rarity) - .001) * (data.high - data.low) + data.low);
    *output = pet;
    return true;
}
int fishValue(int genus, int species, int weight) {
    for (const auto &entry : kFishRarities) {
        if (entry.genus == genus && entry.species == species)
            return int(std::ceil(15 * (std::pow(.2 * entry.rarity, 1.5) + std::pow(.003125 * weight, 1.1))));
    }
    return 0;
}
const char *petName(uint32_t index) {
    return index < kPetNames ? kPetNameRows[index].name : "Unknown";
}
const float *petColor(int body, int color) {
    const int generic[] = {0, 1, 3, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 21, 22, 23, 24, 25};
    const int specific[] = {0, 1, 3, 5, 6, 7, 8, 9, 10, 11, 12, 16, 17, 18, 23, 24, 25};
    const int count = body < 4 ? 23 : 17;
    if (color < 0 || color >= count)
        return kPetColors[0];
    return kPetColors[body < 4 ? generic[color] : specific[color]];
}
int kartCost(int body) {
    return body >= 0 && body < 3 ? kKartBodies[body].cost : -1;
}
int accessoryCost(int accessory) {
    return accessory >= 0 && accessory < 92 ? kKartAccessories[accessory].cost : -1;
}
bool canBuyKart(int body, int currentBody, uint32_t tickets) {
    const int cost = kartCost(body);
    return cost >= 0 && body != currentBody && uint32_t(cost) <= tickets;
}
bool canBuyAccessory(int accessory, int body, const int8_t owned[16], uint32_t tickets) {
    const int cost = accessoryCost(accessory);
    if (body < 0 || body > 2 || accessory == 40 || cost < 0 || uint32_t(cost) > tickets)
        return false;
    int count = 0;
    for (int index = 0; index < 16; ++index) {
        if (owned[index] == accessory)
            return false;
        if (owned[index] >= 0)
            ++count;
    }
    return count < 16;
}
} // namespace tt::clerks
