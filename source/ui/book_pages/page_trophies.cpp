#include "ui/book_pages/page_trophies.h"
#include <cstdio>

namespace tt::ui {

namespace {

constexpr u32 kInk = C2D_Color32(0x20, 0x18, 0x10, 0xFF);
constexpr u32 kSubInk = C2D_Color32(0x50, 0x48, 0x40, 0xFF);
constexpr u32 kColBorder = C2D_Color32(0xB0, 0xA4, 0x90, 0xFF);
constexpr u32 kColBg = C2D_Color32(0xFF, 0xFA, 0xEC, 0xFF);
constexpr u32 kGoldTrophy = C2D_Color32(0xF5, 0xC0, 0x20, 0xFF);
constexpr u32 kSilverCup = C2D_Color32(0xD0, 0xD8, 0xE0, 0xFF);
constexpr u32 kEmptyBadge = C2D_Color32(0xD8, 0xD0, 0xC8, 0xFF);

} // namespace

void PageTrophies::init() {
    textBuf_ = C2D_TextBufNew(4096);
}

void PageTrophies::shutdown() {
    if (textBuf_) {
        C2D_TextBufDelete(textBuf_);
        textBuf_ = nullptr;
    }
}

void PageTrophies::draw(float x, float y, float w, float h, const BookDrawContext& bdc) {
    (void)h;
    if (!textBuf_) return;

    C2D_TextBufClear(textBuf_);

    char buf[128];

    // Count kart trophies
    int kartWon = 0;
    if (bdc.book) {
        for (int i = 0; i < net::kKartingTrophies; i++) {
            if (bdc.book->kartingTrophies[i] != 0) kartWon++;
        }
    }
    const uint32_t tickets = bdc.book ? bdc.book->tickets : 0;

    // Calculate golf trophies & cups
    bool golfTrophies[net::kGolfTrophies] = {};
    bool golfCups[net::kGolfCups] = {};
    int golfWon = 0;
    int cupsWon = 0;
    if (bdc.book) {
        golfWon = net::calcGolfTrophies(bdc.book->golfHistory, golfTrophies);
        cupsWon = net::calcGolfCups(bdc.book->golfHistory, golfCups);
    }

    // Two Columns: Left = Go-Karting (width 140), Right = Mini-Golf (width 140)
    const float colW = (w - 18.0f) * 0.5f;
    const float colH = 175.0f;
    const float leftColX = x + 6.0f;
    const float rightColX = leftColX + colW + 6.0f;
    const float topY = y + 4.0f;

    // ================= Left Column: Go-Karting =================
    C2D_DrawRectSolid(leftColX, topY, 0.5f, colW, colH, kColBorder);
    C2D_DrawRectSolid(leftColX + 1.0f, topY + 1.0f, 0.5f, colW - 2.0f, colH - 2.0f, kColBg);

    // Header
    C2D_Text kHdr;
    C2D_TextParse(&kHdr, textBuf_, "Go-Karting");
    C2D_TextOptimize(&kHdr);
    C2D_DrawText(&kHdr, C2D_WithColor, leftColX + 8.0f, topY + 4.0f, 0.6f, 0.40f, 0.40f, C2D_Color32(0xC0, 0x30, 0x20, 0xFF));

    snprintf(buf, sizeof(buf), "Trophies:  %d / 33", kartWon);
    C2D_Text ktTxt;
    C2D_TextParse(&ktTxt, textBuf_, buf);
    C2D_TextOptimize(&ktTxt);
    C2D_DrawText(&ktTxt, C2D_WithColor, leftColX + 8.0f, topY + 20.0f, 0.6f, 0.32f, 0.32f, kInk);

    snprintf(buf, sizeof(buf), "Tickets:  %u", unsigned(tickets));
    C2D_Text tkTxt;
    C2D_TextParse(&tkTxt, textBuf_, buf);
    C2D_TextOptimize(&tkTxt);
    C2D_DrawText(&tkTxt, C2D_WithColor, leftColX + 8.0f, topY + 32.0f, 0.6f, 0.32f, 0.32f, kInk);

    // 33 Trophy Badges grid (11 cols x 3 rows)
    const float bStartX = leftColX + 6.0f;
    const float bStartY = topY + 46.0f;
    const float bSize = 8.5f;
    const float bGap = 3.0f;

    for (int i = 0; i < net::kKartingTrophies; i++) {
        const int col = i % 11;
        const int row = i / 11;
        const float bx = bStartX + col * (bSize + bGap);
        const float by = bStartY + row * (bSize + bGap);
        const bool got = bdc.book ? (bdc.book->kartingTrophies[i] != 0) : false;

        C2D_DrawRectSolid(bx, by, 0.5f, bSize, bSize, got ? C2D_Color32(0x80, 0x60, 0x10, 0xFF) : C2D_Color32(0x80, 0x78, 0x70, 0xFF));
        C2D_DrawRectSolid(bx + 0.5f, by + 0.5f, 0.5f, bSize - 1.0f, bSize - 1.0f, got ? kGoldTrophy : kEmptyBadge);
    }

    // Karting history stats
    const float kStatY = bStartY + 3 * (bSize + bGap) + 8.0f;
    const uint8_t* kh = bdc.book ? bdc.book->kartingHistory : nullptr;

    snprintf(buf, sizeof(buf), "Races Won:  %u", kh ? kh[0] : 0);
    C2D_Text rwTxt;
    C2D_TextParse(&rwTxt, textBuf_, buf);
    C2D_TextOptimize(&rwTxt);
    C2D_DrawText(&rwTxt, C2D_WithColor, leftColX + 8.0f, kStatY, 0.6f, 0.30f, 0.30f, kSubInk);

    snprintf(buf, sizeof(buf), "Qualifying Wins:  %u", kh ? kh[1] : 0);
    C2D_Text qwTxt;
    C2D_TextParse(&qwTxt, textBuf_, buf);
    C2D_TextOptimize(&qwTxt);
    C2D_DrawText(&qwTxt, C2D_WithColor, leftColX + 8.0f, kStatY + 12.0f, 0.6f, 0.30f, 0.30f, kSubInk);

    snprintf(buf, sizeof(buf), "Total Laps:  %u", kh ? kh[2] : 0);
    C2D_Text tlTxt;
    C2D_TextParse(&tlTxt, textBuf_, buf);
    C2D_TextOptimize(&tlTxt);
    C2D_DrawText(&tlTxt, C2D_WithColor, leftColX + 8.0f, kStatY + 24.0f, 0.6f, 0.30f, 0.30f, kSubInk);

    // ================= Right Column: Mini-Golf =================
    C2D_DrawRectSolid(rightColX, topY, 0.5f, colW, colH, kColBorder);
    C2D_DrawRectSolid(rightColX + 1.0f, topY + 1.0f, 0.5f, colW - 2.0f, colH - 2.0f, kColBg);

    // Header
    C2D_Text gHdr;
    C2D_TextParse(&gHdr, textBuf_, "Mini-Golf");
    C2D_TextOptimize(&gHdr);
    C2D_DrawText(&gHdr, C2D_WithColor, rightColX + 8.0f, topY + 4.0f, 0.6f, 0.40f, 0.40f, C2D_Color32(0x20, 0x80, 0x30, 0xFF));

    snprintf(buf, sizeof(buf), "Trophies:  %d / 30   Cups:  %d / 3", golfWon, cupsWon);
    C2D_Text gtTxt;
    C2D_TextParse(&gtTxt, textBuf_, buf);
    C2D_TextOptimize(&gtTxt);
    C2D_DrawText(&gtTxt, C2D_WithColor, rightColX + 8.0f, topY + 20.0f, 0.6f, 0.32f, 0.32f, kInk);

    // 30 Golf Trophy badges (10 cols x 3 rows)
    const float gbStartX = rightColX + 6.0f;
    const float gbStartY = topY + 36.0f;
    const float gbSize = 9.5f;
    const float gbGap = 3.5f;

    for (int i = 0; i < net::kGolfTrophies; i++) {
        const int col = i % 10;
        const int row = i / 10;
        const float bx = gbStartX + col * (gbSize + gbGap);
        const float by = gbStartY + row * (gbSize + gbGap);
        const bool got = golfTrophies[i];

        C2D_DrawRectSolid(bx, by, 0.5f, gbSize, gbSize, got ? C2D_Color32(0x80, 0x60, 0x10, 0xFF) : C2D_Color32(0x80, 0x78, 0x70, 0xFF));
        C2D_DrawRectSolid(bx + 0.5f, by + 0.5f, 0.5f, gbSize - 1.0f, gbSize - 1.0f, got ? kGoldTrophy : kEmptyBadge);
    }

    // 3 Cup Badges row
    const float cupY = gbStartY + 3 * (gbSize + gbGap) + 6.0f;
    snprintf(buf, sizeof(buf), "Championship Cups:");
    C2D_Text cupHdr;
    C2D_TextParse(&cupHdr, textBuf_, buf);
    C2D_TextOptimize(&cupHdr);
    C2D_DrawText(&cupHdr, C2D_WithColor, rightColX + 8.0f, cupY, 0.6f, 0.30f, 0.30f, kInk);

    for (int c = 0; c < net::kGolfCups; c++) {
        const float cx = rightColX + 10.0f + c * 26.0f;
        const float cy = cupY + 12.0f;
        const bool got = golfCups[c];

        C2D_DrawRectSolid(cx, cy, 0.5f, 18.0f, 14.0f, got ? C2D_Color32(0x70, 0x60, 0x20, 0xFF) : C2D_Color32(0x80, 0x78, 0x70, 0xFF));
        C2D_DrawRectSolid(cx + 1.0f, cy + 1.0f, 0.5f, 16.0f, 12.0f, got ? kSilverCup : kEmptyBadge);

        snprintf(buf, sizeof(buf), "%d", c + 1);
        C2D_Text cupNum;
        C2D_TextParse(&cupNum, textBuf_, buf);
        C2D_TextOptimize(&cupNum);
        C2D_DrawText(&cupNum, C2D_WithColor, cx + 5.0f, cy + 1.0f, 0.7f, 0.30f, 0.30f, kInk);
    }

    // Golf history stats
    const float gStatY = cupY + 32.0f;
    const uint16_t* gh = bdc.book ? bdc.book->golfHistory : nullptr;

    snprintf(buf, sizeof(buf), "Courses Done:  %u", gh ? gh[0] : 0);
    C2D_Text cdTxt;
    C2D_TextParse(&cdTxt, textBuf_, buf);
    C2D_TextOptimize(&cdTxt);
    C2D_DrawText(&cdTxt, C2D_WithColor, rightColX + 8.0f, gStatY, 0.6f, 0.28f, 0.28f, kSubInk);

    snprintf(buf, sizeof(buf), "Under Par:  %u", gh ? gh[1] : 0);
    C2D_Text upTxt;
    C2D_TextParse(&upTxt, textBuf_, buf);
    C2D_TextOptimize(&upTxt);
    C2D_DrawText(&upTxt, C2D_WithColor, rightColX + 8.0f, gStatY + 11.0f, 0.6f, 0.28f, 0.28f, kSubInk);

    snprintf(buf, sizeof(buf), "Holes-in-One:  %u", gh ? gh[2] : 0);
    C2D_Text hioTxt;
    C2D_TextParse(&hioTxt, textBuf_, buf);
    C2D_TextOptimize(&hioTxt);
    C2D_DrawText(&hioTxt, C2D_WithColor, rightColX + 8.0f, gStatY + 22.0f, 0.6f, 0.28f, 0.28f, kSubInk);
}

} // namespace tt::ui
