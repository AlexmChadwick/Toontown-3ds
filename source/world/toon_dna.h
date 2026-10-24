// Toon DNA: the 15-byte network string of DistributedToon.setDNAString and
// AstronLoginManager.createAvatar, mapped to baked part files, colors and clothing.
//
// Spec: toontown/toon/ToonDNA.py (makeNetString / isValidNetString / makeFromNetString),
// Toon.py (generateToonClothes, generateToonColor), OTPGlobals.toonBodyScales. Tables are
// generated into toon_dna_tables.h by tools/gen_toon_tables.py. The server ejects a client
// that sends DNA failing isValidNetString, so everything we send goes through it.
//
// Netstring layout (u8 each): 't', head, torso, legs, gender (1 = male, anything else =
// female), topTex, topTexColor, sleeveTex, sleeveTexColor, botTex, botTexColor, armColor,
// gloveColor, legColor, headColor.
//
// Plain C++ (no libctru) so tools/toon_dna_test/ can build it on the host.
#pragma once

#include <cstddef>
#include <cstdint>

namespace tt::world {

constexpr unsigned kDnaNetStringSize = 15;

// toonSpeciesTypes order ("dchmrfpbs").
enum class Species : uint8_t { Dog = 0, Cat, Horse, Mouse, Rabbit, Duck, Monkey, Bear, Pig, Count };
enum class Gender : uint8_t { Female = 0, Male = 1 };

struct ToonDna {
    uint8_t head = 0;  // index into toonHeadTypes ("dls", "dss", ...)
    uint8_t torso = 1; // index into toonTorsoTypes ("ss", "ms", "ls", "sd", "md", "ld", "s", "m", "l")
    uint8_t legs = 1;  // index into toonLegTypes ("s", "m", "l")
    Gender gender = Gender::Male;
    uint8_t topTex = 0, topTexColor = 0;       // Shirts / ClothesColors
    uint8_t sleeveTex = 0, sleeveTexColor = 0; // Sleeves / ClothesColors
    uint8_t botTex = 0, botTexColor = 0;       // BoyShorts or GirlBottoms (by gender) / ClothesColors
    uint8_t armColor = 25, gloveColor = 0, legColor = 25, headColor = 25; // allColorsList
};

// --- netstring ---

// Exactly ToonDNA.isValidNetString (the server's createAvatar check).
bool isValidNetString(const uint8_t* data, size_t size);
// Validates, then decodes. On failure *out is untouched.
bool parseNetString(const uint8_t* data, size_t size, ToonDna* out);
// ToonDNA.makeNetString. Does not validate: call isValidDna before sending.
void makeNetString(const ToonDna& dna, uint8_t out[kDnaNetStringSize]);
bool isValidDna(const ToonDna& dna);

// The upstream default toon, ToonDNA.newToon(('dls', 'ms', 'm', 'm')): default clothes,
// body color 25. This is also the RomFS boot set (romfs/baked/toon).
ToonDna defaultToonDna();

// --- what to draw ---

Species species(const ToonDna& dna);
const char* speciesName(Species s); // "dog", "cat", ...
float bodyScale(const ToonDna& dna); // OTPGlobals.toonBodyScales: scale the whole toon
// legHeight + torsoHeight scaled by body scale, plus headHeight (Toon.resetHeight).
float toonHeight(const ToonDna& dna);

// Codes of the baked files (tools/pipeline/bake_toon.py):
//   <dir>/legs_<legs>_<lod>.ttm, torso_<torso>_<lod>.ttm, head_<head>_<lod>.ttm
//   (+ _<anim>.tta; heads only when headAnimated), lashes_<lashes>.ttm when hasLashes.
struct ToonPartCodes {
    char legs[2];
    char torso[3];  // after the upstream fix-ups below, always one of the six baked torsos
    char head[4];
    char lashes[3]; // "<species><head length>", e.g. "cl"; valid when hasLashes
    bool hasLashes;     // female: ToonHead.setupEyelashes
    bool headAnimated;  // only dog heads have clips upstream; others are static models
};
// Applies Toon.generateToonClothes' torso swap (a girl's torso follows her bottom:
// skirt -> 'd', shorts -> 's') and maps naked torsos ('s', 'm', 'l', not shipped in
// ref/resources) to the shorts torso of the same size.
void partCodes(const ToonDna& dna, ToonPartCodes* out);

// RGBA of allColorsList / ClothesColors entries; out-of-range indices give entry 0, as
// upstream's try/except does.
const float* bodyColor(uint8_t index);
const float* clothesColor(uint8_t index);

// Tint uniforms indexed by render::TintSlot (0 none = white, head, arm, glove, leg, shirt,
// sleeve, bottom), as Toon.generateToonColor / generateToonClothes color the parts.
constexpr unsigned kDnaTintCount = 8;
void tintColors(const ToonDna& dna, float rgba[kDnaTintCount][4]);

// Clothing textures. Tables in clothes.ttx order; a torso batch's texSlot (1 shirt,
// 2 sleeve, 3 bottom) selects which one replaces the batch's own texture.
enum ClothingTable : uint8_t { kClothShirts = 0, kClothSleeves, kClothBoyShorts, kClothGirlBottoms, kClothTableCount };
enum TexSlot : uint8_t { kTexSlotNone = 0, kTexSlotShirt, kTexSlotSleeve, kTexSlotBottom, kTexSlotCount };
struct ClothingTex {
    uint8_t table; // ClothingTable
    uint8_t index; // DNA index into that table (clothes.ttx maps it to a texture)
};
// out[kTexSlotShirt..kTexSlotBottom]; out[0] is unused. Out-of-range indices give 0.
void clothingTextures(const ToonDna& dna, ClothingTex out[kTexSlotCount]);

// --- Make-A-Toon ---

uint8_t headVariantCount(Species s); // heads of a species in toonHeadTypes order (mouse: 2)
const char* headVariantCode(Species s, uint8_t variant);

// Body colors offered by Make-A-Toon (defaultBoyColorList / defaultGirlColorList).
uint8_t matColorCount(Gender g);
uint8_t matColor(Gender g, uint8_t i);

// Clothes offered by Make-A-Toon: every (style, color) of TailorCollections[MAKE_A_TOON].
struct MatShirtChoice { uint8_t topTex, sleeveTex, topTexColor, sleeveTexColor; };
struct MatBottomChoice { uint8_t botTex, botTexColor; bool skirt; };
uint8_t matShirtCount(Gender g);
MatShirtChoice matShirt(Gender g, uint8_t i);
uint8_t matBottomCount(Gender g);
MatBottomChoice matBottom(Gender g, uint8_t i);

struct MakeAToonChoice {
    Species species = Species::Dog;
    uint8_t headVariant = 0; // < headVariantCount(species)
    uint8_t torsoSize = 1;   // 0 short, 1 medium, 2 long
    uint8_t legSize = 1;     // 0 short, 1 medium, 2 long
    Gender gender = Gender::Male;
    uint8_t headColor = 1, armColor = 1, legColor = 1; // allColorsList indices from matColor()
    uint8_t shirt = 0;       // < matShirtCount(gender)
    uint8_t bottom = 0;      // < matBottomCount(gender)
};
// Builds the DNA (torso 's'/'d' from the gender and bottom, glove color 0). Returns false
// when the choice is outside the Make-A-Toon options or the result fails isValidDna.
bool buildMakeAToon(const MakeAToonChoice& choice, ToonDna* out);

} // namespace tt::world
