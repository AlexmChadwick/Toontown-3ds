#include "ui/book_pages/page_fish.h"
#include "world/fish_tables.h"
#include <cstdio>
#include <vector>

namespace tt::ui {

namespace {

constexpr u32 kInk = C2D_Color32(0x20, 0x18, 0x10, 0xFF);
constexpr u32 kCardBg = C2D_Color32(0xFF, 0xFA, 0xEA, 0xFF);
constexpr u32 kCardBorder = C2D_Color32(0xB0, 0xA8, 0x98, 0xFF);
constexpr u32 kCaughtBg = C2D_Color32(0xE8, 0xF8, 0xE8, 0xFF);
constexpr u32 kCaughtBorder = C2D_Color32(0x30, 0x90, 0x30, 0xFF);
constexpr u32 kCaughtInk = C2D_Color32(0x10, 0x60, 0x10, 0xFF);

// Find record weight caught for (genus, species)
uint16_t findRecordWeight(const net::OwnBookStats* book, uint8_t g, uint8_t sp) {
    if (!book) return 0;
    for (int i = 0; i < book->numFishCollected; i++) {
        if (book->fishCollection[i].genus == g && book->fishCollection[i].species == sp) {
            return book->fishCollection[i].maxWeight;
        }
    }
    return 0;
}

// Total caught count
int totalCaughtCount(const net::OwnBookStats* book) {
    if (!book) return 0;
    return book->numFishCollected;
}

// 14 distinct genera indices in kSpecies
constexpr uint8_t kDistinctGenera[14] = {
    0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26
};

} // namespace

void PageFish::init() {
    textBuf_ = C2D_TextBufNew(4096);
}

void PageFish::shutdown() {
    if (textBuf_) {
        C2D_TextBufDelete(textBuf_);
        textBuf_ = nullptr;
    }
}

u32 PageFish::handleInput(u32 keysDown, const touchPosition& touch, BookDrawContext& bdc) {
    (void)bdc;
    if (keysDown & KEY_TOUCH) {
        // Prev genus button: x = 16..46, y = 24..42
        if (touch.px >= 16 && touch.px <= 46 && touch.py >= 24 && touch.py <= 42) {
            if (activeGenusIdx_ > 0) activeGenusIdx_--;
            else activeGenusIdx_ = 13;
            return KEY_TOUCH;
        }
        // Next genus button: x = 274..304, y = 24..42
        if (touch.px >= 274 && touch.px <= 304 && touch.py >= 24 && touch.py <= 42) {
            if (activeGenusIdx_ < 13) activeGenusIdx_++;
            else activeGenusIdx_ = 0;
            return KEY_TOUCH;
        }
    }
    if (keysDown & KEY_DLEFT) {
        if (activeGenusIdx_ > 0) activeGenusIdx_--;
        else activeGenusIdx_ = 13;
        return KEY_DLEFT;
    }
    if (keysDown & KEY_DRIGHT) {
        if (activeGenusIdx_ < 13) activeGenusIdx_++;
        else activeGenusIdx_ = 0;
        return KEY_DRIGHT;
    }
    return 0;
}

void PageFish::draw(float x, float y, float w, float h, const BookDrawContext& bdc) {
    (void)w; (void)h;
    if (!textBuf_) return;

    C2D_TextBufClear(textBuf_);

    char buf[128];
    const int caughtTotal = totalCaughtCount(bdc.book);
    const int trophiesWon = caughtTotal / 10; // 1 trophy per 10 species up to 7

    // Header: Total Species and Trophies
    snprintf(buf, sizeof(buf), "Fish Album:  %d / 70 Species   Trophies: %d / 7", caughtTotal, trophiesWon);
    C2D_Text hdrTxt;
    C2D_TextParse(&hdrTxt, textBuf_, buf);
    C2D_TextOptimize(&hdrTxt);
    C2D_DrawText(&hdrTxt, C2D_WithColor, x + 6.0f, y + 2.0f, 0.5f, 0.40f, 0.40f, kInk);

    // Genus navigation bar: < [Genus Name (caught/total)] >
    const uint8_t targetGenus = kDistinctGenera[activeGenusIdx_];
    const char* genusName = (targetGenus <= world::fish::kMaxGenus && world::fish::kGenusNames[targetGenus])
                                ? world::fish::kGenusNames[targetGenus]
                                : "Fish";

    // Count species in this genus
    int genusSpeciesCount = 0;
    int genusCaughtCount = 0;
    for (int i = 0; i < world::fish::kNumSpecies; i++) {
        if (world::fish::kSpecies[i].genus == targetGenus) {
            genusSpeciesCount++;
            if (findRecordWeight(bdc.book, targetGenus, world::fish::kSpecies[i].species) > 0) {
                genusCaughtCount++;
            }
        }
    }

    const float navY = y + 18.0f;
    const float navW = w - 12.0f;

    // Genus bar background
    C2D_DrawRectSolid(x + 6.0f, navY, 0.5f, navW, 20.0f, C2D_Color32(0xD0, 0xC4, 0xB0, 0xFF));
    C2D_DrawRectSolid(x + 7.0f, navY + 1.0f, 0.5f, navW - 2.0f, 18.0f, C2D_Color32(0xF5, 0xEE, 0xDC, 0xFF));

    // Left arrow
    C2D_DrawRectSolid(x + 10.0f, navY + 2.0f, 0.5f, 16.0f, 14.0f, C2D_Color32(0xC0, 0xB0, 0x98, 0xFF));
    C2D_Text leftArrow;
    C2D_TextParse(&leftArrow, textBuf_, "<");
    C2D_TextOptimize(&leftArrow);
    C2D_DrawText(&leftArrow, C2D_WithColor, x + 15.0f, navY + 2.0f, 0.6f, 0.35f, 0.35f, kInk);

    // Right arrow
    C2D_DrawRectSolid(x + navW - 22.0f, navY + 2.0f, 0.5f, 16.0f, 14.0f, C2D_Color32(0xC0, 0xB0, 0x98, 0xFF));
    C2D_Text rightArrow;
    C2D_TextParse(&rightArrow, textBuf_, ">");
    C2D_TextOptimize(&rightArrow);
    C2D_DrawText(&rightArrow, C2D_WithColor, x + navW - 17.0f, navY + 2.0f, 0.6f, 0.35f, 0.35f, kInk);

    // Genus label
    snprintf(buf, sizeof(buf), "%s  (%d / %d caught)", genusName, genusCaughtCount, genusSpeciesCount);
    C2D_Text gTxt;
    C2D_TextParse(&gTxt, textBuf_, buf);
    C2D_TextOptimize(&gTxt);
    C2D_DrawText(&gTxt, C2D_WithColor, x + 34.0f, navY + 3.0f, 0.6f, 0.36f, 0.36f, kInk);

    // List species in this genus
    const float listY = navY + 24.0f;
    const float itemH = 24.0f;

    int slot = 0;
    for (int i = 0; i < world::fish::kNumSpecies; i++) {
        const auto& spec = world::fish::kSpecies[i];
        if (spec.genus != targetGenus) continue;

        const float iy = listY + slot * (itemH + 3.0f);
        const uint16_t recordWeight = findRecordWeight(bdc.book, spec.genus, spec.species);
        const bool caught = (recordWeight > 0);

        C2D_DrawRectSolid(x + 6.0f, iy, 0.5f, navW, itemH, caught ? kCaughtBorder : kCardBorder);
        C2D_DrawRectSolid(x + 7.0f, iy + 1.0f, 0.5f, navW - 2.0f, itemH - 2.0f, caught ? kCaughtBg : kCardBg);

        // Name
        C2D_Text nameTxt;
        C2D_TextParse(&nameTxt, textBuf_, spec.name);
        C2D_TextOptimize(&nameTxt);
        C2D_DrawText(&nameTxt, C2D_WithColor, x + 12.0f, iy + 4.0f, 0.6f, 0.34f, 0.34f, caught ? kCaughtInk : kInk);

        // Weight range & rarity
        snprintf(buf, sizeof(buf), "Rarity: %d   Range: %d-%d lb", spec.rarity, spec.minLb, spec.maxLb);
        C2D_Text rarTxt;
        C2D_TextParse(&rarTxt, textBuf_, buf);
        C2D_TextOptimize(&rarTxt);
        C2D_DrawText(&rarTxt, C2D_WithColor, x + 120.0f, iy + 4.0f, 0.6f, 0.30f, 0.30f, C2D_Color32(0x60, 0x58, 0x50, 0xFF));

        // Catch record
        if (caught) {
            const int lb = recordWeight / 16;
            const int oz = recordWeight % 16;
            snprintf(buf, sizeof(buf), "Record: %d lb %d oz", lb, oz);
            C2D_Text recTxt;
            C2D_TextParse(&recTxt, textBuf_, buf);
            C2D_TextOptimize(&recTxt);
            C2D_DrawText(&recTxt, C2D_WithColor, x + navW - 86.0f, iy + 4.0f, 0.6f, 0.30f, 0.30f, kCaughtInk);
        } else {
            C2D_Text recTxt;
            C2D_TextParse(&recTxt, textBuf_, "Not caught");
            C2D_TextOptimize(&recTxt);
            C2D_DrawText(&recTxt, C2D_WithColor, x + navW - 65.0f, iy + 4.0f, 0.6f, 0.30f, 0.30f, C2D_Color32(0x99, 0x90, 0x88, 0xFF));
        }

        slot++;
    }

    // Bottom: 7 Fishing Trophy Cups
    const float trophyBarY = y + 160.0f;
    C2D_Text tHdr;
    C2D_TextParse(&tHdr, textBuf_, "Trophies (1 every 10 species):");
    C2D_TextOptimize(&tHdr);
    C2D_DrawText(&tHdr, C2D_WithColor, x + 8.0f, trophyBarY, 0.5f, 0.32f, 0.32f, kInk);

    const float tStartX = x + 12.0f;
    const float tSize = 14.0f;
    const float tGap = 24.0f;

    for (int t = 0; t < 7; t++) {
        const float tx = tStartX + t * (tSize + tGap);
        const float ty = trophyBarY + 12.0f;
        const bool earned = (caughtTotal >= (t + 1) * 10);

        C2D_DrawRectSolid(tx, ty, 0.5f, tSize, tSize, C2D_Color32(0x60, 0x50, 0x40, 0xFF));
        C2D_DrawRectSolid(tx + 1.0f, ty + 1.0f, 0.5f, tSize - 2.0f, tSize - 2.0f,
                         earned ? C2D_Color32(0xF0, 0xC0, 0x20, 0xFF) : C2D_Color32(0xD0, 0xC8, 0xC0, 0xFF));

        snprintf(buf, sizeof(buf), "%d", (t + 1) * 10);
        C2D_Text cntTxt;
        C2D_TextParse(&cntTxt, textBuf_, buf);
        C2D_TextOptimize(&cntTxt);
        C2D_DrawText(&cntTxt, C2D_WithColor, tx + 1.0f, ty + 2.0f, 0.6f, 0.28f, 0.28f,
                     earned ? C2D_Color32(0x50, 0x30, 0x00, 0xFF) : C2D_Color32(0x80, 0x78, 0x70, 0xFF));
    }
}

} // namespace tt::ui
