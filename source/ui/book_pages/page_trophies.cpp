#include "ui/book_pages/page_trophies.h"

#include "ui/theme.h"

namespace tt::ui {

namespace {

constexpr u32 kGold = theme::rgb(0xF2, 0xC0, 0x1E), kGoldDark = theme::rgb(0xA8, 0x78, 0x10);
constexpr u32 kSilver = theme::rgb(0xD8, 0xE0, 0xE8), kSilverDark = theme::rgb(0x88, 0x92, 0x9C);

// A loving cup `s` px wide: bowl, handles, stem and foot; grey until won.
void cup(float x, float y, float s, bool won, u32 face = kGold, u32 dark = kGoldDark) {
    const u32 c = won ? face : theme::kGrey, d = won ? dark : theme::kGreyDark;
    const float hw = s * 0.18f;
    theme::roundRect(x, y + s * 0.08f, hw + 1, s * 0.4f, hw * 0.5f, d);
    theme::roundRect(x + s - hw - 1, y + s * 0.08f, hw + 1, s * 0.4f, hw * 0.5f, d);
    theme::roundRect(x + hw * 0.6f, y, s - hw * 1.2f, s * 0.6f, s * 0.22f, c);
    C2D_DrawRectSolid(x + s * 0.42f, y + s * 0.58f, 0.5f, s * 0.16f, s * 0.2f, d);
    theme::roundRect(x + s * 0.24f, y + s * 0.76f, s * 0.52f, s * 0.2f, 1, d);
}

void column(float x, float y, float w, float h, u32 accent) {
    theme::roundRect(x, y, w, h, 7, theme::shade(accent, 0.7f));
    theme::roundRect(x + 2, y + 2, w - 4, h - 4, 6, theme::kPaper);
    theme::roundRect(x + 2, y + 2, w - 4, 20, 6, theme::mix(accent, theme::kWhite, 0.65f));
}

} // namespace

void PageTrophies::init() { text_.init(1024); }

void PageTrophies::shutdown() { text_.shutdown(); }

// TrophyPage (Go-Karting: KartingTrophies, tickets, RaceGlobals history) and the golf
// trophies and cups (GolfGlobals.calcTrophyListFromHistory / calcCupListFromHistory).
void PageTrophies::draw(float x, float y, float w, float h, const BookDrawContext& bdc) {
    const net::OwnBookStats* b = bdc.book;
    const float colW = (w - 15) * 0.5f, colH = h - 6, top = y + 3;
    const float kx = x + 5, gx = kx + colW + 5;
    const u32 kartRed = theme::rgb(0xD0, 0x40, 0x30), golfGreen = theme::rgb(0x3C, 0x9C, 0x3C);

    // ---- Go-Karting ----
    column(kx, top, colW, colH, kartRed);
    int kartWon = 0;
    for (int i = 0; b && i < net::kKartingTrophies; i++) kartWon += b->kartingTrophies[i] != 0;
    text_.draw(kx + 8, top + 4, theme::kTextBody, theme::kRedDark, "Go-Karting");
    text_.right(kx + colW - 8, top + 5, theme::kTextSmall, theme::kInk, 0, "%d / %d", kartWon, net::kKartingTrophies);
    constexpr int kKartCols = 11;
    const float ks = (colW - 14) / kKartCols;
    for (int i = 0; i < net::kKartingTrophies; i++)
        cup(kx + 7 + (i % kKartCols) * ks, top + 27 + (i / kKartCols) * (ks + 2), ks - 2, b && b->kartingTrophies[i]);
    float ly = top + 27 + 3 * (ks + 2) + 4;
    const uint8_t* kh = b ? b->kartingHistory : nullptr;
    // RaceGlobals history slots: 0-2 qualifications, 3-5 wins per track type, 6 Grand Prix wins.
    const unsigned quals = kh ? kh[0] + kh[1] + kh[2] : 0, wins = kh ? kh[3] + kh[4] + kh[5] : 0;
    text_.draw(kx + 8, ly, theme::kTextSmall, theme::kInk, "Tickets  %u", b ? unsigned(b->tickets) : 0u);
    ly += 15;
    text_.draw(kx + 8, ly, theme::kTextTiny, theme::kInkSoft, "Races won  %u", wins);
    ly += 13;
    text_.draw(kx + 8, ly, theme::kTextTiny, theme::kInkSoft, "Qualified  %u", quals);
    ly += 13;
    text_.draw(kx + 8, ly, theme::kTextTiny, theme::kInkSoft, "Grand Prix wins  %u", kh ? unsigned(kh[6]) : 0u);

    // ---- Mini-Golf ----
    column(gx, top, colW, colH, golfGreen);
    bool trophies[net::kGolfTrophies] = {};
    bool cups[net::kGolfCups] = {};
    const int golfWon = b ? net::calcGolfTrophies(b->golfHistory, trophies) : 0;
    if (b) net::calcGolfCups(b->golfHistory, cups);
    text_.draw(gx + 8, top + 4, theme::kTextBody, theme::kGreenDark, "Mini-Golf");
    text_.right(gx + colW - 8, top + 5, theme::kTextSmall, theme::kInk, 0, "%d / %d", golfWon, net::kGolfTrophies);
    constexpr int kGolfCols = 10;
    const float gs = (colW - 14) / kGolfCols;
    for (int i = 0; i < net::kGolfTrophies; i++)
        cup(gx + 7 + (i % kGolfCols) * gs, top + 27 + (i / kGolfCols) * (gs + 2), gs - 2, trophies[i]);
    ly = top + 27 + 3 * (gs + 2) + 2;
    // The three championship cups, larger and silver.
    for (int c = 0; c < net::kGolfCups; c++)
        cup(gx + 14 + c * ((colW - 28) / 3), ly, 22, cups[c], kSilver, kSilverDark);
    text_.right(gx + colW - 8, ly + 6, theme::kTextTiny, theme::kInkSoft, 0, "Cups");
    ly += 27;
    const uint16_t* gh = b ? b->golfHistory : nullptr;
    text_.draw(gx + 8, ly, theme::kTextTiny, theme::kInkSoft, "Courses played  %u", gh ? unsigned(gh[0]) : 0u);
    ly += 13;
    text_.draw(gx + 8, ly, theme::kTextTiny, theme::kInkSoft, "Under par  %u", gh ? unsigned(gh[1]) : 0u);
    ly += 13;
    text_.draw(gx + 8, ly, theme::kTextTiny, theme::kInkSoft, "Holes in one  %u", gh ? unsigned(gh[2]) : 0u);
}

} // namespace tt::ui
