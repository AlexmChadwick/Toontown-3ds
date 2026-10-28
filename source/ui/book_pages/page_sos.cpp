#include "render/ui_font.h"
#include "ui/book_pages/page_sos.h"
#include "ui/pad_tables.h"
#include "ui/sos_tables.h"
#include <cstdio>

namespace tt::ui {

namespace {

constexpr u32 kInk = C2D_Color32(0x20, 0x18, 0x10, 0xFF);
constexpr u32 kCardBg = C2D_Color32(0xFF, 0xFA, 0xEA, 0xFF);
constexpr u32 kCardBorder = C2D_Color32(0xC0, 0xB0, 0x98, 0xFF);
constexpr u32 kStarGold = C2D_Color32(0xF0, 0xB0, 0x10, 0xFF);
constexpr u32 kTrackBlue = C2D_Color32(0x20, 0x50, 0xA0, 0xFF);

} // namespace

void PageSos::init() {
    textBuf_ = C2D_TextBufNew(4096);
    pad::buildBookSosPage(focus_); // padnav
}

void PageSos::shutdown() {
    if (textBuf_) {
        C2D_TextBufDelete(textBuf_);
        textBuf_ = nullptr;
    }
}

u32 PageSos::handleInput(u32 keysDown, const touchPosition& touch, BookDrawContext& bdc) {
    // ---- padnav: with more than 8 cards, D-Left / D-Right flip "[Page n/2]" ----
    const int totalCards = bdc.book ? bdc.book->numSosCards : 0;
    if ((keysDown & (KEY_DLEFT | KEY_DRIGHT)) && totalCards > kCardsPerView) {
        viewPage_ = 1 - viewPage_;
        focus_.setFocusById(pad::book::kIdSosPage);
        focus_.logActivate(pad::book::kIdSosPage);
        return keysDown & (KEY_DLEFT | KEY_DRIGHT);
    }
    // ---- end padnav ----
    if (keysDown & KEY_TOUCH) {
        // Toggle view page if > 8 cards: x = 240..300, y = 18..36
        if (touch.px >= 240 && touch.px <= 300 && touch.py >= 18 && touch.py <= 36) {
            viewPage_ = 1 - viewPage_;
            focus_.setFocusById(pad::book::kIdSosPage); // padnav
            return KEY_TOUCH;
        }
    }
    return 0;
}

void PageSos::draw(float x, float y, float w, float h, const BookDrawContext& bdc) {
    (void)w; (void)h;
    if (!textBuf_) return;

    C2D_TextBufClear(textBuf_);

    char buf[128];
    const int totalCards = bdc.book ? bdc.book->numSosCards : 0;

    // Header
    snprintf(buf, sizeof(buf), "SOS ToonFriends  %d / 16", totalCards);
    C2D_Text hdrTxt;
    tt::render::textParse(&hdrTxt, textBuf_, buf);
    C2D_TextOptimize(&hdrTxt);
    C2D_DrawText(&hdrTxt, C2D_WithColor, x + 6.0f, y + 2.0f, 0.5f, 0.42f, 0.42f, kInk);

    if (totalCards > 8) {
        snprintf(buf, sizeof(buf), "[Page %d/2]", viewPage_ + 1);
        C2D_Text pgTxt;
        tt::render::textParse(&pgTxt, textBuf_, buf);
        C2D_TextOptimize(&pgTxt);
        C2D_DrawText(&pgTxt, C2D_WithColor, x + w - 70.0f, y + 2.0f, 0.5f, 0.35f, 0.35f, C2D_Color32(0x00, 0x40, 0x90, 0xFF));
    }

    if (totalCards == 0) {
        C2D_Text emptyTxt;
        tt::render::textParse(&emptyTxt, textBuf_, "You have no SOS ToonFriends!\n\nDefeat the VP in Sellbot HQ\nto rescue Toons and earn SOS cards.");
        C2D_TextOptimize(&emptyTxt);
        C2D_DrawText(&emptyTxt, C2D_WithColor, x + 16.0f, y + 45.0f, 0.5f, 0.38f, 0.38f,
                     C2D_Color32(0x60, 0x58, 0x50, 0xFF));
        return;
    }

    // Grid of cards: 4 columns x 2 rows
    const float startY = y + 18.0f;
    const float cardW = 68.0f;
    const float cardH = 72.0f;
    const float gapX = 4.0f;
    const float gapY = 6.0f;

    const int startIdx = viewPage_ * 8;
    for (int i = 0; i < 8; i++) {
        const int cardIdx = startIdx + i;
        if (cardIdx >= totalCards) break;

        const auto& entry = bdc.book->sosCards[cardIdx];
        const int col = i % 4;
        const int row = i / 4;
        const float cx = x + 6.0f + col * (cardW + gapX);
        const float cy = startY + row * (cardH + gapY);

        const SosCardDef* def = findSosCard(entry.npcId);

        // Card box
        C2D_DrawRectSolid(cx, cy, 0.5f, cardW, cardH, kCardBorder);
        C2D_DrawRectSolid(cx + 1.0f, cy + 1.0f, 0.5f, cardW - 2.0f, cardH - 2.0f, kCardBg);

        // Track header banner
        const char* trackName = def ? sosTrackName(def->track, def->level) : "SOS CARD";
        C2D_Text tTxt;
        tt::render::textParse(&tTxt, textBuf_, trackName);
        C2D_TextOptimize(&tTxt);
        C2D_DrawText(&tTxt, C2D_WithColor, cx + 4.0f, cy + 3.0f, 0.6f, 0.26f, 0.26f, kTrackBlue);

        // Star rating
        const int stars = def ? def->stars : 1;
        for (int s = 0; s < stars; s++) {
            C2D_DrawCircleSolid(cx + 6.0f + s * 9.0f, cy + 16.0f, 0.6f, 3.0f, kStarGold);
        }

        // Toon Name
        const char* name = def ? def->name : "Toon Friend";
        C2D_Text nTxt;
        tt::render::textParse(&nTxt, textBuf_, name);
        C2D_TextOptimize(&nTxt);
        C2D_DrawText(&nTxt, C2D_WithColor, cx + 4.0f, cy + 26.0f, 0.6f, 0.30f, 0.30f, kInk);

        // Ability / HP / Info
        if (def && def->hp > 0) {
            snprintf(buf, sizeof(buf), "%d pts", def->hp);
            C2D_Text hpTxt;
            tt::render::textParse(&hpTxt, textBuf_, buf);
            C2D_TextOptimize(&hpTxt);
            C2D_DrawText(&hpTxt, C2D_WithColor, cx + 4.0f, cy + 42.0f, 0.6f, 0.28f, 0.28f, C2D_Color32(0x50, 0x48, 0x40, 0xFF));
        }

        // Calls remaining
        snprintf(buf, sizeof(buf), "Calls: %d", entry.count);
        C2D_Text cTxt;
        tt::render::textParse(&cTxt, textBuf_, buf);
        C2D_TextOptimize(&cTxt);
        C2D_DrawText(&cTxt, C2D_WithColor, cx + 4.0f, cy + 56.0f, 0.6f, 0.28f, 0.28f,
                     C2D_Color32(0x20, 0x60, 0x20, 0xFF));
    }
    if (totalCards > kCardsPerView) focus_.drawHighlight(); // padnav
}

} // namespace tt::ui
