#include "ui/book_pages/page_inventory.h"
#include "ui/draw_shapes.h"
#include "world/battle_fx.h"
#include <cstdio>
#include <algorithm>

namespace tt::ui {

namespace {

constexpr uint16_t kLevels[7][7] = {
    {0, 20, 200, 800, 2000, 6000, 10000}, // Heal
    {0, 20, 100, 800, 2000, 6000, 10000}, // Trap
    {0, 20, 100, 800, 2000, 6000, 10000}, // Lure
    {0, 40, 200, 1000, 2500, 7500, 10000},// Sound
    {0, 10, 50, 400, 2000, 6000, 10000},  // Throw
    {0, 10, 50, 400, 2000, 6000, 10000},  // Squirt
    {0, 20, 100, 500, 2000, 6000, 10000}, // Drop
};

constexpr uint32_t kTrackColors[7] = {
    C2D_Color32(0xD3, 0x94, 0xFF, 0xFF), // Toon-Up
    C2D_Color32(0xE0, 0xE0, 0x20, 0xFF), // Trap
    C2D_Color32(0x20, 0xC0, 0x20, 0xFF), // Lure
    C2D_Color32(0x40, 0x80, 0xFF, 0xFF), // Sound
    C2D_Color32(0xFF, 0xA0, 0x20, 0xFF), // Throw
    C2D_Color32(0xFF, 0x40, 0xC0, 0xFF), // Squirt
    C2D_Color32(0x20, 0xD0, 0xD0, 0xFF), // Drop
};

const char* const kTrackNames[7] = {
    "Toon-Up", "Trap", "Lure", "Sound", "Throw", "Squirt", "Drop"
};

constexpr u32 kInk = C2D_Color32(0x20, 0x18, 0x10, 0xFF);
constexpr u32 kBarBg = C2D_Color32(0xDD, 0xD8, 0xD0, 0xFF);
constexpr u32 kBarBorder = C2D_Color32(0x88, 0x80, 0x78, 0xFF);
constexpr u32 kSlotPaper = C2D_Color32(0xFA, 0xF6, 0xEE, 0xFF);
constexpr u32 kSlotEdge = C2D_Color32(0xA0, 0x91, 0x7D, 0xFF);
constexpr u32 kLockedPaper = C2D_Color32(0xD8, 0xD1, 0xC6, 0xFF);
constexpr u32 kLockedInk = C2D_Color32(0x76, 0x6E, 0x64, 0xFF);

void nextExpThreshold(int track, uint16_t exp, uint16_t& curThresh, uint16_t& nextThresh) {
    if (track < 0 || track >= 7) { curThresh = 0; nextThresh = 10000; return; }
    if (exp >= 10500) { curThresh = 10500; nextThresh = 10500; return; }
    if (exp >= 10000) { curThresh = 10000; nextThresh = 10500; return; } // Uber
    for (int l = 6; l >= 0; l--) {
        if (exp >= kLevels[track][l]) {
            curThresh = kLevels[track][l];
            nextThresh = (l < 6) ? kLevels[track][l + 1] : 10000;
            return;
        }
    }
    curThresh = 0;
    nextThresh = kLevels[track][1];
}

} // namespace

void PageInventory::init() {
    textBuf_ = C2D_TextBufNew(2048);
}

void PageInventory::shutdown() {
    if (textBuf_) {
        C2D_TextBufDelete(textBuf_);
        textBuf_ = nullptr;
    }
}

void PageInventory::draw(float x, float y, float w, float h, const BookDrawContext& bdc) {
    (void)w; (void)h;
    if (!textBuf_) return;

    // Draw Gag carry totals header
    int totalGags = 0;
    const int maxCarry = bdc.stats ? bdc.stats->maxCarry : 20;
    if (bdc.stats) {
        for (int t = 0; t < 7; t++) {
            for (int l = 0; l < 7; l++) {
                totalGags += bdc.stats->inventory[t][l];
            }
        }
    }

    C2D_TextBufClear(textBuf_);
    C2D_Text titleText;
    char buf[128];
    snprintf(buf, sizeof(buf), "Gags  %d / %d", totalGags, maxCarry);
    C2D_TextParse(&titleText, textBuf_, buf);
    C2D_TextOptimize(&titleText);
    C2D_DrawText(&titleText, C2D_WithColor, x + 6.0f, y + 2.0f, 0.5f, 0.45f, 0.45f, kInk);
    // InventoryPage.moneyDisplay: the jellybean jar's count, on the same page.
    if (bdc.stats) {
        snprintf(buf, sizeof(buf), "Jellybeans  %d / %d", bdc.stats->money, bdc.stats->maxMoney);
        C2D_Text moneyText;
        C2D_TextParse(&moneyText, textBuf_, buf);
        C2D_TextOptimize(&moneyText);
        C2D_DrawText(&moneyText, C2D_WithColor, x + 150.0f, y + 2.0f, 0.5f, 0.45f, 0.45f,
                     C2D_Color32(0x80, 0x50, 0x00, 0xFF));
    }

    const int8_t trainingTrack = bdc.book ? bdc.book->trackProgressId : -1;
    const uint32_t trainingBits = bdc.book ? bdc.book->trackProgress : 0;

    // Table of 7 tracks
    // Seven 21 px rows fill the page while leaving room for track training.  The
    // previous 15 px loose icons were hard to identify at native 3DS size.
    const float rowH = 21.0f;
    const float startY = y + 18.0f;

    tt::world::GagAtlas& atlas = tt::world::GagAtlas::instance();
    atlas.init();

    for (int t = 0; t < 7; t++) {
        const float ry = startY + t * rowH;
        const bool unlocked = bdc.stats ? (bdc.stats->trackAccess[t] != 0) : false;
        const uint16_t exp = bdc.stats ? bdc.stats->experience[t] : 0;

        // Track color indicator tab
        C2D_DrawRectSolid(x + 4.0f, ry + 1.0f, 0.5f, 4.0f, rowH - 2.0f, kTrackColors[t]);

        // Track name
        C2D_Text nameTxt;
        C2D_TextParse(&nameTxt, textBuf_, kTrackNames[t]);
        C2D_TextOptimize(&nameTxt);
        C2D_DrawText(&nameTxt, C2D_WithColor, x + 12.0f, ry + 3.0f, 0.5f, 0.36f, 0.36f,
                     unlocked ? kInk : C2D_Color32(0x88, 0x88, 0x88, 0xFF));

        if (!unlocked) {
            if (trainingTrack == t) {
                // Training indicator
                C2D_Text trText;
                C2D_TextParse(&trText, textBuf_, "TRAINING");
                C2D_TextOptimize(&trText);
                C2D_DrawText(&trText, C2D_WithColor, x + 60.0f, ry + 3.0f, 0.5f, 0.34f, 0.34f, kTrackColors[t]);
            } else {
                C2D_Text lkText;
                C2D_TextParse(&lkText, textBuf_, "Locked");
                C2D_TextOptimize(&lkText);
                C2D_DrawText(&lkText, C2D_WithColor, x + 60.0f, ry + 3.0f, 0.5f, 0.34f, 0.34f,
                             C2D_Color32(0x99, 0x90, 0x88, 0xFF));
            }
        }

        // Seven stable paper slots: learned gags keep their original art and
        // unavailable levels retain a clear silhouette instead of leaving gaps.
        const float gagStartX = x + 60.0f;
        const float gagSize = 18.0f;
        const float gagSpacing = 19.0f;

        for (int l = 0; l < 7; l++) {
            const float gx = gagStartX + l * gagSpacing;
            if (unlocked) {
                const uint8_t count = bdc.stats ? bdc.stats->inventory[t][l] : 0;
                const bool hasExp = exp >= kLevels[t][l];

                if (hasExp) {
                    shape::roundFrame(gx, ry + 1.0f, 0.49f, gagSize, gagSize, 3.0f, 1.0f,
                                      kSlotEdge, kSlotPaper);
                    atlas.drawIcon(t, l, gx + 1.0f, ry + 2.0f, gagSize - 2.0f, 0.52f,
                                   count > 0 ? 1.0f : 0.35f);
                    snprintf(buf, sizeof(buf), "%d", count);
                    C2D_Text cntTxt;
                    C2D_TextParse(&cntTxt, textBuf_, buf);
                    C2D_TextOptimize(&cntTxt);
                    // Opaque badge: the number never disappears into detailed gag art.
                    shape::roundRect(gx + 9.0f, ry + 11.0f, 0.58f, 9.0f, 8.0f, 3.0f,
                                     count ? kTrackColors[t] : kBarBorder);
                    C2D_DrawText(&cntTxt, C2D_WithColor, gx + (count >= 10 ? 9.5f : 11.5f), ry + 10.5f,
                                 0.60f, 0.27f, 0.27f, kInk);
                } else {
                    // Preserve the seven-card ladder even before this level is
                    // earned.  A full dimmed card is much easier to scan than the
                    // old six-pixel dot and mirrors the live battle picker.
                    shape::roundFrame(gx, ry + 1.0f, 0.49f, gagSize, gagSize, 3.0f, 1.0f,
                                      kSlotEdge, kLockedPaper);
                    C2D_DrawRectSolid(gx + 2.0f, ry + 3.0f, 0.54f, 2.0f, gagSize - 4.0f,
                                      kTrackColors[t]);
                    snprintf(buf, sizeof(buf), "%d", l + 1);
                    C2D_Text levelTxt;
                    C2D_TextParse(&levelTxt, textBuf_, buf);
                    C2D_TextOptimize(&levelTxt);
                    C2D_DrawText(&levelTxt, C2D_WithColor, gx + 8.0f, ry + 3.0f, 0.56f,
                                 0.28f, 0.28f, kLockedInk);
                }
            }
        }

        // Experience bar on the right side: x + 192 .. x + 288 (width 96)
        const float barX = x + 195.0f;
        const float barY = ry + 4.0f;
        const float barW = 87.0f;
        const float barH = 10.0f;

        // InventoryNew.showTrack: a DirectWaitBar with range nextExp and value curExp (so the
        // fill is curExp / nextExp), its frame the track colour x 0.6.
        const u32 col = kTrackColors[t];
        const u32 dim = C2D_Color32(uint8_t((col & 0xFF) * 6 / 10), uint8_t(((col >> 8) & 0xFF) * 6 / 10),
                                    uint8_t(((col >> 16) & 0xFF) * 6 / 10), 0xFF);
        C2D_DrawRectSolid(barX, barY, 0.5f, barW, barH, kBarBorder);
        C2D_DrawRectSolid(barX + 1.0f, barY + 1.0f, 0.5f, barW - 2.0f, barH - 2.0f, unlocked ? dim : kBarBg);

        if (unlocked) {
            uint16_t curThresh = 0, nextThresh = 10000;
            nextExpThreshold(t, exp, curThresh, nextThresh);
            const bool uber = exp >= 10000; // regMaxSkill: range UberSkill, "N to Go!"
            const float range = uber ? 500.0f : float(nextThresh);
            const float value = uber ? float(exp - 10000) : float(exp);
            const float frac = std::clamp(range > 0 ? value / range : 1.0f, 0.0f, 1.0f);
            if (frac > 0.0f) {
                C2D_DrawRectSolid(barX + 1.0f, barY + 1.0f, 0.5f, (barW - 2.0f) * frac, barH - 2.0f, col);
            }

            if (exp >= 10500) {
                snprintf(buf, sizeof(buf), "MAXED");
            } else if (uber) {
                snprintf(buf, sizeof(buf), "%d to Go!", 10500 - exp); // TTLocalizer.InventoryUberTrackExp
            } else {
                snprintf(buf, sizeof(buf), "%d / %d", exp, nextThresh); // TTLocalizer.InventoryTrackExp
            }
            C2D_Text expTxt;
            C2D_TextParse(&expTxt, textBuf_, buf);
            C2D_TextOptimize(&expTxt);
            float tw = 0.0f;
            C2D_TextGetDimensions(&expTxt, 0.32f, 0.32f, &tw, nullptr);
            C2D_DrawText(&expTxt, C2D_WithColor, barX + (barW - tw) * 0.5f, barY - 1.5f, 0.6f, 0.32f, 0.32f,
                         C2D_Color32(0x10, 0x10, 0x10, 0xFF));
        }
    }

    // Bottom section: Track training grid (TrackPage filmstrip, 16 frames)
    const float filmY = startY + 7 * rowH + 4.0f;
    if (trainingTrack >= 0 && trainingTrack < 7) {
        snprintf(buf, sizeof(buf), "Training %s track:", kTrackNames[trainingTrack]);
        C2D_Text trLabel;
        C2D_TextParse(&trLabel, textBuf_, buf);
        C2D_TextOptimize(&trLabel);
        C2D_DrawText(&trLabel, C2D_WithColor, x + 6.0f, filmY, 0.5f, 0.35f, 0.35f, kTrackColors[trainingTrack]);

        // 16 frames in a row across w
        const float frameW = 14.0f;
        const float frameH = 16.0f;
        const float frameGap = 3.0f;
        const float filmStartX = x + 12.0f;
        const float frameY = filmY + 12.0f;

        for (int i = 0; i < 16; i++) {
            const float fx = filmStartX + i * (frameW + frameGap);
            const bool trained = (trainingBits & (1u << i)) != 0;

            C2D_DrawRectSolid(fx, frameY, 0.5f, frameW, frameH, C2D_Color32(0x33, 0x33, 0x33, 0xFF));
            C2D_DrawRectSolid(fx + 1.0f, frameY + 1.0f, 0.5f, frameW - 2.0f, frameH - 2.0f,
                             trained ? kTrackColors[trainingTrack] : C2D_Color32(0xEE, 0xEE, 0xEE, 0xFF));

            C2D_Text fTxt;
            if (trained) {
                snprintf(buf, sizeof(buf), "%d", i + 1);
            } else {
                snprintf(buf, sizeof(buf), "?");
            }
            C2D_TextParse(&fTxt, textBuf_, buf);
            C2D_TextOptimize(&fTxt);
            C2D_DrawText(&fTxt, C2D_WithColor, fx + (trained ? 3.0f : 4.0f), frameY + 2.0f, 0.6f, 0.30f, 0.30f,
                         trained ? C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF) : C2D_Color32(0x66, 0x66, 0x66, 0xFF));
        }
    }
}

} // namespace tt::ui
