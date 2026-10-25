#pragma once
#include <cstdint>
namespace tt::clerks {
// NPCToons.py SELL_MOVIE_*; party modes have a separate numbering scheme.
constexpr uint8_t kSellNoFish = 3;
constexpr uint8_t kSellTrophy = 4;
constexpr int kPetNames = 2713; // TTLocalizer.PetNameIndexMAX
constexpr int kPetOffers = 6;   // Three sorted seeds, duplicated for PetGenders.
constexpr int kKartFields = 9;  // KartDNA
constexpr int kKartTrunk = 16;  // KartShopGlobals.MAX_KART_ACC
struct PetOffer {
    int8_t dna[9] = {};
    uint32_t traitSeed = 0;
    uint32_t nameIndex = 0;
    int cost = 0;
};
bool generatePet(uint32_t seed, uint32_t safezone, PetOffer *output);
const char *petName(uint32_t index);
const float *petColor(int body, int color);
int fishValue(int genus, int species, int weight);
int kartCost(int body);
int accessoryCost(int accessory);
bool canBuyKart(int body, int currentBody, uint32_t tickets);
bool canBuyAccessory(int accessory, int body, const int8_t owned[16], uint32_t tickets);
} // namespace tt::clerks
