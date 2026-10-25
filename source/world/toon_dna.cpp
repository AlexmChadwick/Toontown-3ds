#include "world/toon_dna.h"

#include "world/toon_dna_tables.h"

#ifdef __3DS__
#include "render/model.h"
#endif

namespace tt::world {

namespace t = dna_tables;

namespace {

static_assert(static_cast<unsigned>(Species::Count) == t::kNumSpecies, "species table");
static_assert(t::kNumHeadTypes <= 255 && t::kNumShirts <= 256 && t::kNumSleeves <= 256 &&
                  t::kNumBoyShorts <= 256 && t::kNumGirlBottoms <= 256,
              "DNA fields are u8");
static_assert(t::kSpeciesLetters[0] == 'd', "Species::Dog is toonSpeciesTypes[0]");
// buildMakeAToon / ToonDna defaults rely on this toonTorsoTypes / toonLegTypes order.
static_assert(t::kTorsoTypes[0][0] == 's' && t::kTorsoTypes[1][0] == 'm' && t::kTorsoTypes[2][0] == 'l' &&
                  t::kTorsoTypes[0][1] == 's' && t::kTorsoTypes[1][1] == 's' && t::kTorsoTypes[2][1] == 's' &&
                  t::kTorsoTypes[3][0] == 's' && t::kTorsoTypes[4][0] == 'm' && t::kTorsoTypes[5][0] == 'l' &&
                  t::kTorsoTypes[3][1] == 'd' && t::kTorsoTypes[4][1] == 'd' && t::kTorsoTypes[5][1] == 'd',
              "toonTorsoTypes order");
static_assert(t::kLegTypes[1][0] == 'm' && t::kHeadTypes[0][0] == 'd' && t::kHeadTypes[0][1] == 'l' &&
                  t::kHeadTypes[0][2] == 's',
              "ToonDna defaults are dls/ms/m");
#ifdef __3DS__
static_assert(render::kTintCount == kDnaTintCount && render::kTintHead == 1 && render::kTintArm == 2 &&
                  render::kTintGlove == 3 && render::kTintLeg == 4 && render::kTintShirt == 5 &&
                  render::kTintSleeve == 6 && render::kTintBottom == 7,
              "tintColors() order must match render::TintSlot");
#endif

constexpr uint8_t kTypeToon = 't';

unsigned bottomCount(Gender g) { return g == Gender::Male ? t::kNumBoyShorts : t::kNumGirlBottoms; }

Gender genderFromByte(uint8_t b) { return b == 1 ? Gender::Male : Gender::Female; }

void copyCode(char* dst, unsigned cap, const char* src) {
    unsigned i = 0;
    for (; i + 1 < cap && src[i]; i++) dst[i] = src[i];
    dst[i] = '\0';
}

void copyRgba(float dst[4], const float* src) {
    for (int i = 0; i < 4; i++) dst[i] = src[i];
}

bool inList(uint8_t v, const uint8_t* list, unsigned n) {
    for (unsigned i = 0; i < n; i++)
        if (list[i] == v) return true;
    return false;
}

} // namespace

bool isValidNetString(const uint8_t* d, size_t size) {
    if (!d || size != kDnaNetStringSize) return false;
    if (d[0] != kTypeToon) return false;
    if (d[1] >= t::kNumHeadTypes || d[2] >= t::kNumTorsoTypes || d[3] >= t::kNumLegTypes) return false;
    const Gender gender = genderFromByte(d[4]);
    if (d[5] >= t::kNumShirts || d[6] >= t::kNumClothesColors) return false;
    if (d[7] >= t::kNumSleeves || d[8] >= t::kNumClothesColors) return false;
    if (d[9] >= bottomCount(gender) || d[10] >= t::kNumClothesColors) return false;
    if (d[11] >= t::kNumAllColors) return false; // armColor
    if (d[12] != 0) return false;                // gloveColor
    if (d[13] >= t::kNumAllColors) return false; // legColor
    if (d[14] >= t::kNumAllColors) return false; // headColor
    return true;
}

bool parseNetString(const uint8_t* d, size_t size, ToonDna* out) {
    if (!out || !isValidNetString(d, size)) return false;
    ToonDna dna;
    dna.head = d[1];
    dna.torso = d[2];
    dna.legs = d[3];
    dna.gender = genderFromByte(d[4]);
    dna.topTex = d[5];
    dna.topTexColor = d[6];
    dna.sleeveTex = d[7];
    dna.sleeveTexColor = d[8];
    dna.botTex = d[9];
    dna.botTexColor = d[10];
    dna.armColor = d[11];
    dna.gloveColor = d[12];
    dna.legColor = d[13];
    dna.headColor = d[14];
    *out = dna;
    return true;
}

void makeNetString(const ToonDna& dna, uint8_t out[kDnaNetStringSize]) {
    out[0] = kTypeToon;
    out[1] = dna.head;
    out[2] = dna.torso;
    out[3] = dna.legs;
    out[4] = dna.gender == Gender::Male ? 1 : 0;
    out[5] = dna.topTex;
    out[6] = dna.topTexColor;
    out[7] = dna.sleeveTex;
    out[8] = dna.sleeveTexColor;
    out[9] = dna.botTex;
    out[10] = dna.botTexColor;
    out[11] = dna.armColor;
    out[12] = dna.gloveColor;
    out[13] = dna.legColor;
    out[14] = dna.headColor;
}

bool isValidDna(const ToonDna& dna) {
    uint8_t buf[kDnaNetStringSize];
    makeNetString(dna, buf);
    return isValidNetString(buf, sizeof(buf));
}

ToonDna defaultToonDna() { return ToonDna(); }

Species species(const ToonDna& dna) {
    const char letter = t::kHeadTypes[dna.head < t::kNumHeadTypes ? dna.head : 0][0];
    for (unsigned i = 0; i < t::kNumSpecies; i++)
        if (t::kSpeciesLetters[i] == letter) return static_cast<Species>(i);
    return Species::Dog;
}

const char* speciesName(Species s) {
    const unsigned i = static_cast<unsigned>(s);
    return i < t::kNumSpecies ? t::kSpeciesNames[i] : "?";
}

float bodyScale(const ToonDna& dna) { return t::kBodyScale[static_cast<unsigned>(species(dna))]; }

float toonHeight(const ToonDna& dna) {
    const float scale = bodyScale(dna);
    const unsigned legs = dna.legs < t::kNumLegTypes ? dna.legs : 0;
    const unsigned torso = dna.torso < t::kNumTorsoTypes ? dna.torso : 0;
    const unsigned head = dna.head < t::kNumHeadTypes ? dna.head : 0;
    return (t::kLegHeight[legs] + t::kTorsoHeight[torso]) * scale + t::kHeadHeight[head]; // head scale is 1
}

void partCodes(const ToonDna& dna, ToonPartCodes* out) {
    const char* head = t::kHeadTypes[dna.head < t::kNumHeadTypes ? dna.head : 0];
    const char* torsoType = t::kTorsoTypes[dna.torso < t::kNumTorsoTypes ? dna.torso : 0];
    copyCode(out->legs, sizeof(out->legs), t::kLegTypes[dna.legs < t::kNumLegTypes ? dna.legs : 0]);
    copyCode(out->head, sizeof(out->head), head);
    char clothes = torsoType[1] ? torsoType[1] : 's'; // naked -> shorts
    // Toon.generateToonClothes: a girl's torso model follows her bottom (swapToonTorso to
    // torso[0] + 'd' for a skirt, + 's' for shorts), whatever the DNA's torso letter says.
    if (dna.gender == Gender::Female) {
        const unsigned bot = dna.botTex < t::kNumGirlBottoms ? dna.botTex : 0;
        clothes = t::kGirlBottomIsSkirt[bot] ? 'd' : 's';
    }
    out->torso[0] = torsoType[0];
    out->torso[1] = clothes;
    out->torso[2] = '\0';
    out->lashes[0] = head[0];
    out->lashes[1] = head[1];
    out->lashes[2] = '\0';
    out->hasLashes = dna.gender == Gender::Female;
    out->headAnimated = head[0] == 'd';
}

const float* bodyColor(uint8_t index) { return t::kAllColors[index < t::kNumAllColors ? index : 0]; }

const float* clothesColor(uint8_t index) { return t::kClothesColors[index < t::kNumClothesColors ? index : 0]; }

void tintColors(const ToonDna& dna, float rgba[kDnaTintCount][4]) {
    static const float kWhite[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    copyRgba(rgba[0], kWhite);
    copyRgba(rgba[1], bodyColor(dna.headColor));
    copyRgba(rgba[2], bodyColor(dna.armColor));
    copyRgba(rgba[3], bodyColor(dna.gloveColor));
    copyRgba(rgba[4], bodyColor(dna.legColor));
    copyRgba(rgba[5], clothesColor(dna.topTexColor));
    copyRgba(rgba[6], clothesColor(dna.sleeveTexColor));
    copyRgba(rgba[7], clothesColor(dna.botTexColor));
}

void clothingTextures(const ToonDna& dna, ClothingTex out[kTexSlotCount]) {
    out[kTexSlotNone] = {kClothShirts, 0};
    out[kTexSlotShirt] = {kClothShirts, static_cast<uint8_t>(dna.topTex < t::kNumShirts ? dna.topTex : 0)};
    out[kTexSlotSleeve] = {kClothSleeves, static_cast<uint8_t>(dna.sleeveTex < t::kNumSleeves ? dna.sleeveTex : 0)};
    const bool male = dna.gender == Gender::Male;
    out[kTexSlotBottom] = {male ? kClothBoyShorts : kClothGirlBottoms,
                           static_cast<uint8_t>(dna.botTex < bottomCount(dna.gender) ? dna.botTex : 0)};
}

uint8_t headVariantCount(Species s) {
    const unsigned i = static_cast<unsigned>(s);
    return i < t::kNumSpecies ? t::kSpeciesHeadCount[i] : 0;
}

const char* headVariantCode(Species s, uint8_t variant) {
    if (variant >= headVariantCount(s)) return nullptr;
    return t::kHeadTypes[t::kSpeciesHeadStart[static_cast<unsigned>(s)] + variant];
}

uint8_t matColorCount(Gender g) {
    return static_cast<uint8_t>(g == Gender::Male ? t::kNumDefaultBoyColors : t::kNumDefaultGirlColors);
}

uint8_t matColor(Gender g, uint8_t i) {
    if (i >= matColorCount(g)) return 0;
    return g == Gender::Male ? t::kDefaultBoyColors[i] : t::kDefaultGirlColors[i];
}

uint8_t matShirtCount(Gender g) {
    return static_cast<uint8_t>(g == Gender::Male ? t::kNumMatBoyShirts : t::kNumMatGirlShirts);
}

MatShirtChoice matShirt(Gender g, uint8_t i) {
    if (i >= matShirtCount(g)) i = 0;
    const t::MatShirt& s = g == Gender::Male ? t::kMatBoyShirts[i] : t::kMatGirlShirts[i];
    return {s.topTex, s.sleeveTex, s.topTexColor, s.sleeveTexColor};
}

uint8_t matBottomCount(Gender g) {
    return static_cast<uint8_t>(g == Gender::Male ? t::kNumMatBoyBottoms : t::kNumMatGirlBottoms);
}

MatBottomChoice matBottom(Gender g, uint8_t i) {
    if (i >= matBottomCount(g)) i = 0;
    const t::MatBottom& b = g == Gender::Male ? t::kMatBoyBottoms[i] : t::kMatGirlBottoms[i];
    const bool skirt = g == Gender::Female && t::kGirlBottomIsSkirt[b.botTex] != 0;
    return {b.botTex, b.botTexColor, skirt};
}

bool buildMakeAToon(const MakeAToonChoice& c, ToonDna* out) {
    if (!out) return false;
    if (static_cast<unsigned>(c.species) >= t::kNumSpecies) return false;
    if (c.gender != Gender::Male && c.gender != Gender::Female) return false;
    if (c.headVariant >= headVariantCount(c.species)) return false;
    if (c.torsoSize > 2 || c.legSize > 2) return false;
    if (c.shirt >= matShirtCount(c.gender) || c.bottom >= matBottomCount(c.gender)) return false;
    const uint8_t* colors = c.gender == Gender::Male ? t::kDefaultBoyColors : t::kDefaultGirlColors;
    const unsigned numColors = matColorCount(c.gender);
    if (!inList(c.headColor, colors, numColors) || !inList(c.armColor, colors, numColors) ||
        !inList(c.legColor, colors, numColors))
        return false;

    const MatShirtChoice shirt = matShirt(c.gender, c.shirt);
    const MatBottomChoice bottom = matBottom(c.gender, c.bottom);
    ToonDna dna;
    dna.head = static_cast<uint8_t>(t::kSpeciesHeadStart[static_cast<unsigned>(c.species)] + c.headVariant);
    // toonTorsoTypes: 0..2 = s/m/l shorts, 3..5 = s/m/l skirt ('d').
    dna.torso = static_cast<uint8_t>(c.torsoSize + (bottom.skirt ? 3 : 0));
    dna.legs = c.legSize;
    dna.gender = c.gender;
    dna.topTex = shirt.topTex;
    dna.topTexColor = shirt.topTexColor;
    dna.sleeveTex = shirt.sleeveTex;
    dna.sleeveTexColor = shirt.sleeveTexColor;
    dna.botTex = bottom.botTex;
    dna.botTexColor = bottom.botTexColor;
    dna.armColor = c.armColor;
    dna.gloveColor = 0;
    dna.legColor = c.legColor;
    dna.headColor = c.headColor;
    if (!isValidDna(dna)) return false;
    *out = dna;
    return true;
}

} // namespace tt::world
