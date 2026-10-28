#include "ui/book_pages/page_fish.h"

#include "ui/pad_tables.h"
#include "ui/theme.h"
#include "world/fish_tables.h"

namespace tt::ui {

namespace {

constexpr int kNumGenera = 14;
// The 14 genus ids in FishGlobals order.
constexpr uint8_t kDistinctGenera[kNumGenera] = {0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26};
// FishGlobals.TrophyDict: one fishing trophy per 10 species.
constexpr int kTrophies = 7;

// The record weight (oz) caught for (genus, species), 0 if never caught.
uint16_t recordWeight(const net::OwnBookStats* book, uint8_t g, uint8_t sp) {
    if (!book) return 0;
    for (int i = 0; i < book->numFishCollected; i++) {
        if (book->fishCollection[i].genus == g && book->fishCollection[i].species == sp)
            return book->fishCollection[i].maxWeight;
    }
    return 0;
}

bool inRect(const touchPosition& t, float x, float y, float w, float h) {
    return t.px >= x && t.px < x + w && t.py >= y && t.py < y + h;
}

// A round arrow button like SpeedChat's: lavender face, blue triangle.
void arrowButton(float x, float y, float w, float h, bool left) {
    theme::button(x, y, w, h, theme::kScFrame, theme::kScFrameEdge, false, 6.0f, 0.55f);
    const float cx = x + w * 0.5f, cy = y + (h - 3) * 0.5f, s = 5.0f;
    const float tip = left ? cx - s : cx + s, base = left ? cx + s * 0.6f : cx - s * 0.6f;
    C2D_DrawTriangle(tip, cy, theme::kBlueDark, base, cy - s, theme::kBlueDark, base, cy + s, theme::kBlueDark, 0.56f);
}

// A small gold (or grey, unearned) loving cup.
void trophyCup(float cx, float y, bool earned) {
    const u32 c = earned ? theme::rgb(0xF2, 0xC0, 0x1E) : theme::kGrey;
    const u32 d = earned ? theme::rgb(0xB0, 0x80, 0x10) : theme::kGreyDark;
    theme::roundRect(cx - 9, y + 1, 4, 7, 2, d);  // handles
    theme::roundRect(cx + 5, y + 1, 4, 7, 2, d);
    theme::roundRect(cx - 7, y, 14, 11, 4, c);    // bowl
    C2D_DrawRectSolid(cx - 1.5f, y + 10, 0.5f, 3, 3, d);
    theme::roundRect(cx - 5, y + 13, 10, 3, 1, d); // foot
}

} // namespace

void PageFish::init() {
    text_.init(1536);
    pad::buildBookFishArrows(focus_); // padnav
}

void PageFish::shutdown() { text_.shutdown(); }

void PageFish::step(int dir) {
    activeGenusIdx_ = (activeGenusIdx_ + dir + kNumGenera) % kNumGenera;
}

u32 PageFish::handleInput(u32 keysDown, const touchPosition& touch, BookDrawContext& bdc) {
    (void)bdc;
    using namespace pad::book;
    if (keysDown & KEY_TOUCH) {
        if (inRect(touch, kGenusPrevX, kGenusY, kGenusW, kGenusH)) {
            step(-1);
            return KEY_TOUCH;
        }
        if (inRect(touch, kGenusNextX, kGenusY, kGenusW, kGenusH)) {
            step(1);
            return KEY_TOUCH;
        }
    }
    // D-Left / D-Right press the < / > arrows (padnav: highlighted and logged).
    if (keysDown & (KEY_DLEFT | KEY_DRIGHT)) {
        const bool left = keysDown & KEY_DLEFT;
        step(left ? -1 : 1);
        const int id = left ? kIdGenusPrev : kIdGenusNext;
        focus_.setFocusById(id);
        focus_.logActivate(id);
        return left ? KEY_DLEFT : KEY_DRIGHT;
    }
    return 0;
}

// FishPage: the album one genus at a time, the species total and the fishing trophies.
void PageFish::draw(float x, float y, float w, float h, const BookDrawContext& bdc) {
    using namespace pad::book;
    const int caught = bdc.book ? bdc.book->numFishCollected : 0;
    const int trophies = caught / 10 < kTrophies ? caught / 10 : kTrophies;
    text_.draw(x + 8, y + 3, theme::kTextBody, theme::kInk, "Species caught  %d / %d", caught, world::fish::kNumSpecies);
    text_.right(x + w - 8, y + 3, theme::kTextBody, theme::kInk, 0, "Trophies  %d / %d", trophies, kTrophies);

    // Genus selector: < Balloon Fish  2 / 5 >
    const uint8_t genus = kDistinctGenera[activeGenusIdx_];
    const char* genusName = genus <= world::fish::kMaxGenus && world::fish::kGenusNames[genus]
                                ? world::fish::kGenusNames[genus] : "Fish";
    int total = 0, got = 0;
    for (int i = 0; i < world::fish::kNumSpecies; i++) {
        const auto& sp = world::fish::kSpecies[i];
        if (sp.genus != genus) continue;
        total++;
        got += recordWeight(bdc.book, sp.genus, sp.species) > 0;
    }
    theme::paper(kGenusPrevX + kGenusW + 3, kGenusY, kGenusNextX - kGenusPrevX - kGenusW - 6, kGenusH - 2, 6.0f,
                 theme::kPaperShade);
    arrowButton(kGenusPrevX, kGenusY, kGenusW, kGenusH, true);
    arrowButton(kGenusNextX, kGenusY, kGenusW, kGenusH, false);
    const float midX = (kGenusPrevX + kGenusNextX + kGenusW) * 0.5f;
    text_.centered(midX, kGenusY + 3, theme::kTextBody, theme::kRedDark, 200, "%s   %d / %d", genusName, got, total);

    // One card per species of the genus; rows shrink so the largest genus (8) still fits.
    const float listY = kGenusY + kGenusH + 3;
    const float trophyY = y + h - 24;
    float rowH = total ? (trophyY - 3 - listY) / total : 20.0f;
    if (rowH > 22.0f) rowH = 22.0f;
    const float tScale = rowH >= 18.0f ? theme::kTextSmall : theme::kTextTiny;
    const float ty = (rowH - 1 - (rowH >= 18.0f ? 15.0f : 13.0f)) * 0.5f;
    int slot = 0;
    for (int i = 0; i < world::fish::kNumSpecies; i++) {
        const auto& sp = world::fish::kSpecies[i];
        if (sp.genus != genus) continue;
        const float ry = listY + slot++ * rowH;
        const uint16_t rec = recordWeight(bdc.book, sp.genus, sp.species);
        theme::roundRect(x + 6, ry, w - 12, rowH - 1, 4, rec ? theme::kGreenDark : theme::kPaperShade);
        theme::roundRect(x + 7, ry + 1, w - 14, rowH - 3, 3, rec ? theme::rgb(0xE6, 0xF8, 0xE0) : theme::kPaper);
        text_.fit(x + 12, ry + ty, tScale, rec ? theme::kGreenDark : theme::kInk, 128, "%s", sp.name);
        text_.draw(x + 146, ry + ty, theme::kTextTiny, theme::kInkSoft, "%d-%d lb", sp.minLb, sp.maxLb);
        if (rec) {
            text_.right(x + w - 12, ry + ty, tScale, theme::kGreenDark, 100, "%d lb %d oz", rec / 16, rec % 16);
        } else {
            text_.right(x + w - 12, ry + ty, theme::kTextTiny, theme::kGreyDark, 100, "Rarity %d", sp.rarity);
        }
    }

    // The fishing trophies (one per 10 species), as gold cups.
    for (int t = 0; t < kTrophies; t++) {
        const float cx = x + 22 + t * ((w - 44) / (kTrophies - 1));
        trophyCup(cx - 12, trophyY + 3, caught >= (t + 1) * 10);
        text_.draw(cx - 1, trophyY + 4, theme::kTextTiny, caught >= (t + 1) * 10 ? theme::kInk : theme::kInkSoft,
                   "%d", (t + 1) * 10);
    }
    focus_.drawHighlight(); // padnav
}

} // namespace tt::ui
