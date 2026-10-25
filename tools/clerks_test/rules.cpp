#include "ui/clerk_rules.h"

#include <cassert>
#include <cstdio>
#include <cstring>
using namespace tt::clerks;
int main() {
    FILE *input = std::fopen("sdmc-out/clerks-test/pets.bin", "rb");
    assert(input);
    uint32_t seed = 0;
    int count = 0;
    while (std::fread(&seed, 4, 1, input) == 1) {
        uint32_t zone = 0, trait = 0, name = 0, cost = 0;
        int8_t dna[9];
        assert(std::fread(&zone, 4, 1, input) == 1 && std::fread(dna, 9, 1, input) == 1);
        assert(std::fread(&trait, 4, 1, input) == 1 && std::fread(&name, 4, 1, input) == 1 &&
               std::fread(&cost, 4, 1, input) == 1);
        PetOffer offer;
        assert(generatePet(seed, zone, &offer));
        if (std::memcmp(dna, offer.dna, 9) || trait != offer.traitSeed || name != offer.nameIndex ||
            int(cost) != offer.cost) {
            std::fprintf(stderr, "pet seed %u zone %u trait %u/%u name %u/%u cost %d/%u\n", seed, zone,
                         offer.traitSeed, trait, offer.nameIndex, name, offer.cost, cost);
            assert(false);
        }
        ++count;
    }
    std::fclose(input);
    std::printf("Pinned PetUtil seeds/DNA/name/traits/cost: %d PASS\n", count);
    input = std::fopen("sdmc-out/clerks-test/karts.bin", "rb");
    assert(input);
    int32_t body, current, accessory;
    uint32_t tickets;
    int8_t owned[16];
    int32_t expectedBody, expectedAccessory;
    count = 0;
    while (std::fread(&body, 4, 1, input) == 1) {
        assert(std::fread(&current, 4, 1, input) == 1);
        assert(std::fread(&accessory, 4, 1, input) == 1);
        assert(std::fread(&tickets, 4, 1, input) == 1);
        assert(std::fread(owned, 16, 1, input) == 1);
        assert(std::fread(&expectedBody, 4, 1, input) == 1);
        assert(std::fread(&expectedAccessory, 4, 1, input) == 1);
        assert(canBuyKart(body, current, tickets) == bool(expectedBody));
        assert(canBuyAccessory(accessory, body, owned, tickets) == bool(expectedAccessory));
        ++count;
    }
    std::fclose(input);
    std::printf("Pinned KartDNA prices and ownership: %d PASS\n", count);
    assert(!canBuyKart(3, 0, 99999));
    assert(!canBuyAccessory(92, 0, owned, 99999));
    input = std::fopen("sdmc-out/clerks-test/fish.bin", "rb");
    assert(input);
    int32_t fishCount, expected;
    count = 0;
    while (std::fread(&fishCount, 4, 1, input) == 1) {
        assert(std::fread(&expected, 4, 1, input) == 1);
        int total = 0;
        for (int i = 0; i < fishCount; ++i) {
            int32_t fish[3];
            assert(std::fread(fish, 12, 1, input) == 1);
            total += fishValue(fish[0], fish[1], fish[2]);
        }
        assert(total == expected);
        ++count;
    }
    std::fclose(input);
    std::printf("Pinned FishTank totals: %d PASS\n", count);
}
