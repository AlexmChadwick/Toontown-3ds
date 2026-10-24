// Host driver for tools/toon_dna_test/run_test.py: reads commands on stdin, prints what
// source/world/toon_dna.cpp makes of them, one line per command.
//
//   parse <hex>   -> "invalid" | "valid <14 fields> <legs> <torso> <head> <lashes|-> <anim 0|1>
//                    <bodyScale> <height> <8 tints x rgba> <3 clothing table:index> <hex roundtrip>"
//   build <species> <variant> <torsoSize> <legSize> <gender> <headColor> <armColor> <legColor>
//         <shirt> <bottom>    -> "ok <hex>" | "reject"
//   default       -> "<hex>"
//   mat           -> counts and entries of the Make-A-Toon tables
#include <cstdio>
#include <cstring>
#include <string>

#include "world/toon_dna.h"

using namespace tt::world;

namespace {

std::string hex(const uint8_t* d, size_t n) {
    static const char* k = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; i++) {
        s += k[d[i] >> 4];
        s += k[d[i] & 15];
    }
    return s;
}

size_t unhex(const char* s, uint8_t* out, size_t cap) {
    size_t n = 0;
    while (s[0] && s[1] && n < cap) {
        unsigned v = 0;
        if (sscanf(s, "%2x", &v) != 1) break;
        out[n++] = static_cast<uint8_t>(v);
        s += 2;
    }
    return n;
}

void parse(const char* arg) {
    uint8_t buf[64];
    const size_t n = unhex(arg, buf, sizeof(buf));
    const bool valid = isValidNetString(buf, n);
    ToonDna dna;
    const bool parsed = parseNetString(buf, n, &dna);
    if (valid != parsed) {
        printf("mismatch\n");
        return;
    }
    if (!valid) {
        printf("invalid\n");
        return;
    }
    ToonPartCodes codes;
    partCodes(dna, &codes);
    printf("valid %u %u %u %u %u %u %u %u %u %u %u %u %u %u", dna.head, dna.torso, dna.legs,
           static_cast<unsigned>(dna.gender), dna.topTex, dna.topTexColor, dna.sleeveTex, dna.sleeveTexColor,
           dna.botTex, dna.botTexColor, dna.armColor, dna.gloveColor, dna.legColor, dna.headColor);
    printf(" %s %s %s %s %d %.6f %.6f", codes.legs, codes.torso, codes.head, codes.hasLashes ? codes.lashes : "-",
           codes.headAnimated ? 1 : 0, static_cast<double>(bodyScale(dna)), static_cast<double>(toonHeight(dna)));
    float tints[kDnaTintCount][4];
    tintColors(dna, tints);
    for (auto& c : tints)
        for (float v : c) printf(" %.6f", static_cast<double>(v));
    ClothingTex tex[kTexSlotCount];
    clothingTextures(dna, tex);
    for (int s = kTexSlotShirt; s < kTexSlotCount; s++) printf(" %u:%u", tex[s].table, tex[s].index);
    uint8_t out[kDnaNetStringSize];
    makeNetString(dna, out);
    printf(" %s %s\n", hex(out, sizeof(out)).c_str(), speciesName(species(dna)));
}

void build(const char* line) {
    unsigned v[10];
    if (sscanf(line, "%u %u %u %u %u %u %u %u %u %u", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7],
               &v[8], &v[9]) != 10) {
        printf("badcmd\n");
        return;
    }
    MakeAToonChoice c;
    c.species = static_cast<Species>(v[0]);
    c.headVariant = static_cast<uint8_t>(v[1]);
    c.torsoSize = static_cast<uint8_t>(v[2]);
    c.legSize = static_cast<uint8_t>(v[3]);
    c.gender = static_cast<Gender>(v[4]);
    c.headColor = static_cast<uint8_t>(v[5]);
    c.armColor = static_cast<uint8_t>(v[6]);
    c.legColor = static_cast<uint8_t>(v[7]);
    c.shirt = static_cast<uint8_t>(v[8]);
    c.bottom = static_cast<uint8_t>(v[9]);
    ToonDna dna;
    if (!buildMakeAToon(c, &dna)) {
        printf("reject\n");
        return;
    }
    uint8_t out[kDnaNetStringSize];
    makeNetString(dna, out);
    printf("ok %s\n", hex(out, sizeof(out)).c_str());
}

void mat() {
    for (int g = 0; g < 2; g++) {
        const Gender gender = static_cast<Gender>(g);
        printf("colors %d", g);
        for (int i = 0; i < matColorCount(gender); i++) printf(" %u", matColor(gender, static_cast<uint8_t>(i)));
        printf("\nshirts %d", g);
        for (int i = 0; i < matShirtCount(gender); i++) {
            const MatShirtChoice s = matShirt(gender, static_cast<uint8_t>(i));
            printf(" %u,%u,%u,%u", s.topTex, s.sleeveTex, s.topTexColor, s.sleeveTexColor);
        }
        printf("\nbottoms %d", g);
        for (int i = 0; i < matBottomCount(gender); i++) {
            const MatBottomChoice b = matBottom(gender, static_cast<uint8_t>(i));
            printf(" %u,%u,%d", b.botTex, b.botTexColor, b.skirt ? 1 : 0);
        }
        printf("\n");
    }
    printf("heads");
    for (int s = 0; s < static_cast<int>(Species::Count); s++) {
        for (int i = 0; i < headVariantCount(static_cast<Species>(s)); i++)
            printf(" %s", headVariantCode(static_cast<Species>(s), static_cast<uint8_t>(i)));
    }
    printf("\nend\n");
}

} // namespace

int main() {
    char line[512];
    while (fgets(line, sizeof(line), stdin)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (!strncmp(line, "parse ", 6)) {
            parse(line + 6);
        } else if (!strcmp(line, "parse")) {
            parse("");
        } else if (!strncmp(line, "build ", 6)) {
            build(line + 6);
        } else if (!strcmp(line, "default")) {
            uint8_t out[kDnaNetStringSize];
            makeNetString(defaultToonDna(), out);
            printf("%s\n", hex(out, sizeof(out)).c_str());
        } else if (!strcmp(line, "mat")) {
            mat();
        } else {
            printf("badcmd\n");
        }
        fflush(stdout);
    }
    return 0;
}
