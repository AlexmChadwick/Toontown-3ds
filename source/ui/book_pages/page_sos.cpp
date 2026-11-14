#include "ui/book_pages/page_sos.h"

#include <cmath>

#include "ui/pad_tables.h"
#include "ui/sos_tables.h"
#include "ui/theme.h"

namespace tt::ui {

namespace {

constexpr u32 kStar = theme::rgb(0xF6, 0xC0, 0x10), kStarDark = theme::rgb(0xA8, 0x70, 0x00);

bool inRect(const touchPosition& t, float x, float y, float w, float h) {
    return t.px >= x && t.px < x + w && t.py >= y && t.py < y + h;
}

// A five-pointed star of outer radius r.
void star(float cx, float cy, float r, u32 c) {
    float px[10], py[10];
    for (int i = 0; i < 10; i++) {
        const float a = -1.5707963f + i * 0.6283185f, rr = (i & 1) ? r * 0.45f : r;
        px[i] = cx + cosf(a) * rr;
        py[i] = cy + sinf(a) * rr;
    }
    for (int i = 0; i < 10; i++) {
        const int j = (i + 1) % 10;
        C2D_DrawTriangle(cx, cy, c, px[i], py[i], c, px[j], py[j], c, 0.6f);
    }
}

// The card's colour: its gag track, or SOS special abilities in gold.
u32 cardColor(const SosCardDef* def) {
    if (def && def->track < 7) return theme::kTrackColors[def->track];
    return theme::rgb(0xF6, 0xD2, 0x60);
}

} // namespace

void PageSos::init() {
    text_.init(1536);
    pad::buildBookSosPage(focus_); // padnav
}

void PageSos::shutdown() { text_.shutdown(); }

u32 PageSos::handleInput(u32 keysDown, const touchPosition& touch, BookDrawContext& bdc) {
    using namespace pad::book;
    // ---- padnav: with more than 8 cards, D-Left / D-Right flip "Page n/2" ----
    const int totalCards = bdc.book ? bdc.book->numSosCards : 0;
    if ((keysDown & (KEY_DLEFT | KEY_DRIGHT)) && totalCards > kCardsPerView) {
        viewPage_ = 1 - viewPage_;
        focus_.setFocusById(kIdSosPage);
        focus_.logActivate(kIdSosPage);
        return keysDown & (KEY_DLEFT | KEY_DRIGHT);
    }
    // ---- end padnav ----
    if ((keysDown & KEY_TOUCH) && totalCards > kCardsPerView && inRect(touch, kSosPageX, kSosPageY, kSosPageW, kSosPageH)) {
        viewPage_ = 1 - viewPage_;
        focus_.setFocusById(kIdSosPage); // padnav
        return KEY_TOUCH;
    }
    return 0;
}

// NPCFriendPage: one card per SOS Toon (name, ability, stars and calls left), eight a view.
void PageSos::draw(float x, float y, float w, float h, const BookDrawContext& bdc) {
    using namespace pad::book;
    (void)h;
    const int total = bdc.book ? bdc.book->numSosCards : 0;
    text_.draw(x + 8, y + 3, theme::kTextBody, theme::kInk, "SOS Toons  %d / 16", total);
    if (total > kCardsPerView) {
        theme::button(kSosPageX, kSosPageY, kSosPageW, kSosPageH + 2, theme::kScFrame, theme::kScFrameEdge, false, 5.0f);
        text_.centered(kSosPageX + kSosPageW * 0.5f, kSosPageY, theme::kTextSmall, theme::kBlueDark, kSosPageW - 6,
                       "Page %d / 2", viewPage_ + 1);
    }
    if (total == 0) {
        theme::paper(x + 20, y + 40, w - 40, 84, 8.0f);
        text_.centered(x + w * 0.5f, y + 54, theme::kTextTitle, theme::kRedDark, w - 60, "No SOS Toons yet!");
        text_.centered(x + w * 0.5f, y + 80, theme::kTextSmall, theme::kInk, w - 56, "Rescue Toons from the Sellbot VP");
        text_.centered(x + w * 0.5f, y + 96, theme::kTextSmall, theme::kInk, w - 56, "to call them into battle.");
        return;
    }
    constexpr float kGap = 4.0f;
    const float cardW = (w - 12 - 3 * kGap) / 4, cardH = 78.0f, top = y + 24;
    for (int i = 0; i < kCardsPerView; i++) {
        const int idx = viewPage_ * kCardsPerView + i;
        if (idx >= total) break;
        const auto& e = bdc.book->sosCards[idx];
        const SosCardDef* def = findSosCard(e.npcId);
        const float cx = x + 6 + (i % 4) * (cardW + kGap), cy = top + (i / 4) * (cardH + kGap);
        const u32 col = cardColor(def);
        theme::roundRect(cx, cy, cardW, cardH, 6, theme::shade(col, 0.6f));
        theme::roundRect(cx + 1, cy + 1, cardW - 2, cardH - 2, 5, theme::kPaper);
        theme::roundRect(cx + 1, cy + 1, cardW - 2, 16, 5, col);
        text_.centered(cx + cardW * 0.5f, cy + 2, theme::kTextTiny, theme::kInk, cardW - 6, "%s",
                       def ? sosTrackName(def->track, def->level) : "SOS");
        text_.centered(cx + cardW * 0.5f, cy + 20, theme::kTextTiny, theme::kInk, cardW - 6, "%s",
                       def ? def->name : "Toon");
        const int stars = def ? def->stars : 1;
        for (int s = 0; s < stars; s++) {
            const float sx = cx + cardW * 0.5f + (s - (stars - 1) * 0.5f) * 11.0f;
            star(sx, cy + 41, 5.5f, kStarDark);
            star(sx, cy + 41, 4.2f, kStar);
        }
        if (def && def->hp > 0)
            text_.centered(cx + cardW * 0.5f, cy + 49, theme::kTextTiny, theme::kInkSoft, cardW - 6, "%d points", def->hp);
        text_.centered(cx + cardW * 0.5f, cy + 62, theme::kTextTiny, theme::kGreenDark, cardW - 6, "Calls: %d", e.count);
    }
    if (total > kCardsPerView) focus_.drawHighlight(); // padnav
}

} // namespace tt::ui
