#include "ui/book_pages/page_disguise.h"
#include "ui/disguise_tables.h"
#include "ui/suit_heads_atlas.h"
#include <cstdio>
#include <algorithm>

namespace tt::ui {

namespace {

const char* const kDeptNames[4] = { "Bossbot", "Lawbot", "Cashbot", "Sellbot" };

const char* const kMeritNames[4] = {
    "Stock Options", "Jury Notices", "Cogbucks", "Merits"
};

const char* const kSuitNames[4][8] = {
    { "Flunky", "Pencil Pusher", "Yesman", "Micromanager",
      "Downsizer", "Head Hunter", "Corp Raider", "The Big Cheese" },
    { "Bottom Feeder", "Bloodsucker", "Double Talker", "Ambulance Chaser",
      "Back Stabber", "Spin Doctor", "Legal Eagle", "Big Wig" },
    { "Short Change", "Penny Pincher", "Tightwad", "Bean Counter",
      "Number Cruncher", "Money Bags", "Land Shark", "Robber Baron" },
    { "Cold Caller", "Telemarketer", "Name Dropper", "Glad Hander",
      "Mover & Shaker", "Two-Face", "The Mingler", "Mr. Hollywood" },
};

constexpr u32 kDeptColors[4] = {
    C2D_Color32(0x9E, 0x88, 0x88, 0xFF), // Bossbot brown/gray
    C2D_Color32(0x80, 0x90, 0xA8, 0xFF), // Lawbot blue
    C2D_Color32(0x78, 0xA8, 0x90, 0xFF), // Cashbot green
    C2D_Color32(0xB0, 0x88, 0x98, 0xFF), // Sellbot purple
};

constexpr u32 kInk = C2D_Color32(0x20, 0x18, 0x10, 0xFF);
constexpr u32 kCardBg = C2D_Color32(0xFF, 0xFA, 0xEA, 0xFF);
constexpr u32 kCardBorder = C2D_Color32(0xB0, 0xA8, 0x98, 0xFF);

} // namespace

void PageDisguise::init() {
    textBuf_ = C2D_TextBufNew(4096);
    SuitHeadAtlas::instance().init();
    focus_.clear();
    for (int d = 0; d < 4; d++) {
        focus_.addItem(d, 14.0f + d * 70.0f, 20.0f, 66.0f, 18.0f);
    }
    focus_.setFocus(activeDept_);
}

void PageDisguise::shutdown() {
    if (textBuf_) {
        C2D_TextBufDelete(textBuf_);
        textBuf_ = nullptr;
    }
    focus_.clear();
}

u32 PageDisguise::handleInput(u32 keysDown, const touchPosition& touch, BookDrawContext& bdc) {
    (void)bdc;
    if (focus_.count() == 0) init();
    int activated = -1;
    u32 consumed = focus_.handleInput(keysDown, touch, &activated);
    if (activated >= 0 && activated < 4) {
        activeDept_ = activated;
    } else if (focus_.hasFocus() && focus_.focusedId() != activeDept_) {
        activeDept_ = focus_.focusedId();
    }
    return consumed;
}

void PageDisguise::draw(float x, float y, float w, float h, const BookDrawContext& bdc) {
    (void)w; (void)h;
    if (!textBuf_) return;

    C2D_TextBufClear(textBuf_);

    SuitHeadAtlas& atlas = SuitHeadAtlas::instance();
    atlas.init();

    // 4 Department tabs
    const float tabY = y + 2.0f;
    const float tabH = 16.0f;
    const float tabW = 66.0f;

    for (int d = 0; d < 4; d++) {
        const float tx = x + 6.0f + d * 70.0f;
        const bool active = (d == activeDept_);

        C2D_DrawRectSolid(tx, tabY, 0.5f, tabW, tabH, active ? kDeptColors[d] : C2D_Color32(0xD0, 0xC8, 0xC0, 0xFF));
        C2D_DrawRectSolid(tx + 1.0f, tabY + 1.0f, 0.5f, tabW - 2.0f, tabH - 2.0f,
                         active ? C2D_Color32(0xFF, 0xFA, 0xEA, 0xFF) : C2D_Color32(0xE8, 0xE0, 0xD8, 0xFF));

        C2D_Text dTxt;
        C2D_TextParse(&dTxt, textBuf_, kDeptNames[d]);
        C2D_TextOptimize(&dTxt);
        C2D_DrawText(&dTxt, C2D_WithColor, tx + 6.0f, tabY + 2.0f, 0.6f, 0.32f, 0.32f,
                     active ? kDeptColors[d] : C2D_Color32(0x60, 0x58, 0x50, 0xFF));
    }

    const uint8_t type = bdc.book ? (bdc.book->cogTypes[activeDept_] & 7) : 0;
    const uint8_t level = bdc.book ? bdc.book->cogLevels[activeDept_] : 0;
    const uint32_t parts = bdc.book ? bdc.book->cogParts[activeDept_] : 0;
    const uint16_t merits = bdc.book ? bdc.book->cogMerits[activeDept_] : 0;
    const uint8_t neededParts = kPartsPerSuit[activeDept_];
    const int totalParts = __builtin_popcount(parts & ((1u << neededParts) - 1));
    const bool suitComplete = (totalParts >= neededParts);

    const float panelY = y + 24.0f;
    const float panelW = w - 12.0f;
    const float panelH = 150.0f;

    // Outer card
    C2D_DrawRectSolid(x + 6.0f, panelY, 0.5f, panelW, panelH, kDeptColors[activeDept_]);
    C2D_DrawRectSolid(x + 8.0f, panelY + 2.0f, 0.5f, panelW - 4.0f, panelH - 4.0f, kCardBg);

    char buf[128];

    // Left box: Suit head portrait & Identity
    const float leftBoxX = x + 14.0f;
    const float leftBoxY = panelY + 8.0f;

    atlas.drawHead(activeDept_, type, leftBoxX, leftBoxY, 44.0f, 0.6f);

    snprintf(buf, sizeof(buf), "%s", kSuitNames[activeDept_][type]);
    C2D_Text nameTxt;
    C2D_TextParse(&nameTxt, textBuf_, buf);
    C2D_TextOptimize(&nameTxt);
    C2D_DrawText(&nameTxt, C2D_WithColor, leftBoxX + 50.0f, leftBoxY + 4.0f, 0.6f, 0.45f, 0.45f, kInk);

    snprintf(buf, sizeof(buf), "Level %d", int(level) + 1);
    C2D_Text lvlTxt;
    C2D_TextParse(&lvlTxt, textBuf_, buf);
    C2D_TextOptimize(&lvlTxt);
    C2D_DrawText(&lvlTxt, C2D_WithColor, leftBoxX + 50.0f, leftBoxY + 22.0f, 0.6f, 0.38f, 0.38f,
                 C2D_Color32(0x50, 0x48, 0x40, 0xFF));

    // Suit Parts section
    const float partsY = panelY + 58.0f;
    snprintf(buf, sizeof(buf), "Suit Parts:  %d / %d %s", totalParts, neededParts,
             suitComplete ? "(COMPLETE)" : "");
    C2D_Text partsTxt;
    C2D_TextParse(&partsTxt, textBuf_, buf);
    C2D_TextOptimize(&partsTxt);
    C2D_DrawText(&partsTxt, C2D_WithColor, leftBoxX, partsY, 0.6f, 0.36f, 0.36f,
                 suitComplete ? C2D_Color32(0x20, 0x80, 0x20, 0xFF) : kInk);

    // Draw part segment boxes
    const float segW = 12.0f;
    const float segH = 10.0f;
    const float segGap = 3.0f;
    const float segY = partsY + 14.0f;

    for (int p = 0; p < neededParts; p++) {
        const float sx = leftBoxX + p * (segW + segGap);
        const bool hasPart = (parts & (1u << p)) != 0;

        C2D_DrawRectSolid(sx, segY, 0.5f, segW, segH, kCardBorder);
        C2D_DrawRectSolid(sx + 1.0f, segY + 1.0f, 0.5f, segW - 2.0f, segH - 2.0f,
                         hasPart ? kDeptColors[activeDept_] : C2D_Color32(0xE0, 0xD8, 0xD0, 0xFF));
    }

    // Merits / Promotion section
    const float meritsY = segY + segH + 12.0f;
    const uint16_t neededMerits = getNeededMerits(activeDept_, type, level);
    const bool readyForPromotion = suitComplete && (neededMerits > 0 && merits >= neededMerits);

    snprintf(buf, sizeof(buf), "%s Progress:", kMeritNames[activeDept_]);
    C2D_Text mTxt;
    C2D_TextParse(&mTxt, textBuf_, buf);
    C2D_TextOptimize(&mTxt);
    C2D_DrawText(&mTxt, C2D_WithColor, leftBoxX, meritsY, 0.6f, 0.36f, 0.36f, kInk);

    // Merits progress bar
    const float barX = leftBoxX;
    const float barY = meritsY + 14.0f;
    const float barW = panelW - 28.0f;
    const float barH = 14.0f;

    C2D_DrawRectSolid(barX, barY, 0.5f, barW, barH, C2D_Color32(0x60, 0x58, 0x50, 0xFF));
    C2D_DrawRectSolid(barX + 1.0f, barY + 1.0f, 0.5f, barW - 2.0f, barH - 2.0f, C2D_Color32(0xE8, 0xE0, 0xD8, 0xFF));

    float frac = (neededMerits > 0) ? (float(merits) / float(neededMerits)) : 1.0f;
    frac = std::clamp(frac, 0.0f, 1.0f);

    if (frac > 0.0f) {
        C2D_DrawRectSolid(barX + 1.0f, barY + 1.0f, 0.5f, (barW - 2.0f) * frac, barH - 2.0f,
                         readyForPromotion ? C2D_Color32(0x40, 0xB0, 0x40, 0xFF) : kDeptColors[activeDept_]);
    }

    snprintf(buf, sizeof(buf), "%u / %u", unsigned(merits), unsigned(neededMerits));
    C2D_Text numTxt;
    C2D_TextParse(&numTxt, textBuf_, buf);
    C2D_TextOptimize(&numTxt);
    C2D_DrawText(&numTxt, C2D_WithColor, barX + 8.0f, barY + 1.0f, 0.7f, 0.32f, 0.32f,
                 readyForPromotion ? C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF) : kInk);

    if (readyForPromotion) {
        C2D_Text promoTxt;
        C2D_TextParse(&promoTxt, textBuf_, "READY FOR PROMOTION!");
        C2D_TextOptimize(&promoTxt);
        C2D_DrawText(&promoTxt, C2D_WithColor, barX + barW - 130.0f, barY + 1.0f, 0.7f, 0.32f, 0.32f,
                     C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF));
    }
    focus_.drawHighlight();
}

} // namespace tt::ui
