#include <cstdio>

#include "world/toon_dna.h"

namespace {

void printShirts(tt::world::Gender gender) {
    const unsigned n = tt::world::matShirtCount(gender);
    std::printf("shirts %u\n", n);
    for (unsigned i = 0; i < n; i++) {
        const auto s = tt::world::matShirt(gender, static_cast<uint8_t>(i));
        std::printf("%u %u %u %u\n", s.topTex, s.topTexColor, s.sleeveTex, s.sleeveTexColor);
    }
}

void printBottoms(tt::world::Gender gender) {
    const unsigned n = tt::world::matBottomCount(gender);
    std::printf("bottoms %u\n", n);
    for (unsigned i = 0; i < n; i++) {
        const auto b = tt::world::matBottom(gender, static_cast<uint8_t>(i));
        std::printf("%u %u %u\n", b.botTex, b.botTexColor, b.skirt ? 1 : 0);
    }
}

void printNetString(const tt::world::ToonDna& dna) {
    uint8_t blob[tt::world::kDnaNetStringSize];
    tt::world::makeNetString(dna, blob);
    std::printf("dna");
    for (uint8_t b : blob) std::printf(" %u", b);
    std::printf("\n");
}

} // namespace

int main() {
    printShirts(tt::world::Gender::Male);
    printShirts(tt::world::Gender::Female);
    printBottoms(tt::world::Gender::Male);
    printBottoms(tt::world::Gender::Female);

    tt::world::ToonDna dna = tt::world::defaultToonDna();
    const auto shirt = tt::world::matShirt(tt::world::Gender::Female, 19);
    const auto bottom = tt::world::matBottom(tt::world::Gender::Female, 25);
    dna.gender = tt::world::Gender::Female;
    dna.topTex = shirt.topTex;
    dna.topTexColor = shirt.topTexColor;
    dna.sleeveTex = shirt.sleeveTex;
    dna.sleeveTexColor = shirt.sleeveTexColor;
    dna.botTex = bottom.botTex;
    dna.botTexColor = bottom.botTexColor;
    dna.torso = static_cast<uint8_t>((dna.torso % 3) + (bottom.skirt ? 3 : 0));
    printNetString(dna);
    return 0;
}
