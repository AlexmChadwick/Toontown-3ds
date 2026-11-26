#include "ui/book_pages/page_disguise.h"
#include "ui/pad_tables.h"
#include "ui/disguise_tables.h"
#include "ui/suit_heads_atlas.h"
#include "ui/theme.h"

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

// Where each department's suit parts come from (CogHQ facilities).
const char* const kPartSource[4] = {"the Country Club", "the DA Offices", "the Cashbot Mint", "the Sellbot Factory"};

// SuitPage.PANEL_COLORS / PANEL_COLORS_COMPLETE2 (department tabs and accents, as the gallery).
constexpr u32 kPanel[4] = {theme::rgb(204, 199, 196), theme::rgb(191, 199, 204), theme::rgb(191, 209, 201),
                           theme::rgb(210, 194, 196)};
constexpr u32 kPanelGold[4] = {theme::rgb(230, 185, 82), theme::rgb(210, 185, 115), theme::rgb(204, 191, 83),
                               theme::rgb(223, 172, 89)};
// The department's own colour for the meter fill (Bossbot brown, Lawbot blue, Cashbot green, Sellbot red).
constexpr u32 kDeptInk[4] = {theme::rgb(0x8A, 0x5A, 0x34), theme::rgb(0x3A, 0x5C, 0xA8), theme::rgb(0x2E, 0x8A, 0x4A),
                             theme::rgb(0xA8, 0x30, 0x40)};

bool inRect(const touchPosition& t, float x, float y, float w, float h) {
    return t.px >= x && t.px < x + w && t.py >= y && t.py < y + h;
}

} // namespace

void PageDisguise::init() {
    text_.init(1024);
    pad::buildBookDepartments(focus_); // padnav
    SuitHeadAtlas::instance().init();
}

void PageDisguise::shutdown() { text_.shutdown(); }

u32 PageDisguise::handleInput(u32 keysDown, const touchPosition& touch, BookDrawContext& bdc) {
    (void)bdc;
    using namespace pad::book;
    // ---- padnav: D-Left / D-Right step through the department tabs (L / R turn pages) ----
    if (keysDown & (KEY_DLEFT | KEY_DRIGHT)) {
        const int step = (keysDown & KEY_DLEFT) ? kDepts - 1 : 1;
        activeDept_ = (activeDept_ + step) % kDepts;
        focus_.setFocusById(activeDept_);
        focus_.logActivate(activeDept_);
        return keysDown & (KEY_DLEFT | KEY_DRIGHT);
    }
    // ---- end padnav ----
    if (keysDown & KEY_TOUCH) {
        for (int d = 0; d < kDepts; d++) {
            if (inRect(touch, kDeptX + d * kDeptStep, kDeptY, kDeptW, kDeptH)) {
                activeDept_ = d;
                focus_.setFocusById(d); // padnav
                return KEY_TOUCH;
            }
        }
    }
    return 0;
}

// DisguisePage: the department's Cog suit (name and level), the parts collected toward it and
// the merit meter toward the next promotion.
void PageDisguise::draw(float x, float y, float w, float h, const BookDrawContext& bdc) {
    using namespace pad::book;
    (void)y;
    (void)h;
    for (int d = 0; d < kDepts; d++) {
        const bool on = d == activeDept_;
        const float tx = kDeptX + d * kDeptStep;
        const u32 face = on ? kPanelGold[d] : kPanel[d];
        theme::button(tx, on ? kDeptY : kDeptY + 2, kDeptW, kDeptH + (on ? 2 : 0), face, theme::shade(face, 0.6f),
                      false, 5.0f);
        text_.centered(tx + kDeptW * 0.5f, (on ? kDeptY : kDeptY + 2) + 1, theme::kTextSmall,
                       on ? theme::kInk : theme::kInkSoft, kDeptW - 6, "%s", kDeptNames[d]);
    }
    const int dept = activeDept_;
    const uint8_t type = bdc.book ? (bdc.book->cogTypes[dept] & 7) : 0;
    const uint8_t level = bdc.book ? bdc.book->cogLevels[dept] : 0;
    const uint32_t parts = bdc.book ? bdc.book->cogParts[dept] : 0;
    const uint16_t merits = bdc.book ? bdc.book->cogMerits[dept] : 0;
    const int needed = kPartsPerSuit[dept];
    const int have = __builtin_popcount(parts & ((1u << needed) - 1));
    const bool suit = have >= needed;

    const float py = kDeptY + kDeptH + 5, pw = w - 12, ph = 156;
    theme::roundRect(x + 6, py, pw, ph, 8, theme::shade(kPanel[dept], 0.6f));
    theme::roundRect(x + 8, py + 2, pw - 4, ph - 4, 7, theme::kPaper);

    // The suit's head and identity.
    theme::roundRect(x + 14, py + 7, 50, 48, 8, kPanel[dept]);
    SuitHeadAtlas::instance().drawHead(dept, type, x + 17, py + 9, 44.0f, 0.6f);
    text_.fit(x + 72, py + 9, theme::kTextTitle, suit ? theme::kInk : theme::kInkSoft, pw - 76, "%s",
              kSuitNames[dept][type]);
    if (suit) {
        text_.draw(x + 72, py + 30, theme::kTextBody, theme::kInkSoft, "Level %d", int(level) + 1);
    } else {
        text_.fit(x + 72, py + 30, theme::kTextSmall, theme::kInkSoft, pw - 76, "No Cog suit yet");
    }

    // Suit parts: one pip per part (CogDisguiseGlobals.PartsPerSuit).
    float ly = py + 60;
    text_.draw(x + 14, ly, theme::kTextSmall, suit ? theme::kGreenDark : theme::kInk, "Suit parts  %d / %d%s", have,
               needed, suit ? "  Complete!" : "");
    ly += 16;
    const float pipW = (pw - 16) / needed;
    for (int p = 0; p < needed; p++) {
        const bool got = parts & (1u << p);
        theme::roundRect(x + 14 + p * pipW, ly, pipW - 2, 12, 3, got ? theme::shade(kDeptInk[dept], 0.7f) : theme::kGrey);
        theme::roundRect(x + 15 + p * pipW, ly + 1, pipW - 4, 10, 2, got ? kDeptInk[dept] : theme::kPaperShade);
    }
    ly += 18;
    if (!suit) {
        text_.fit(x + 14, ly, theme::kTextSmall, theme::kInkSoft, pw - 20, "Win parts in %s.", kPartSource[dept]);
        focus_.drawHighlight(); // padnav
        return;
    }

    // The merit meter toward the next promotion.
    const uint16_t need = getNeededMerits(dept, type, level);
    const bool ready = need > 0 && merits >= need;
    text_.draw(x + 14, ly, theme::kTextSmall, theme::kInk, "%s", kMeritNames[dept]);
    ly += 15;
    const float bw = pw - 20;
    float frac = need ? float(merits) / float(need) : 1.0f;
    frac = frac < 0 ? 0 : frac > 1 ? 1 : frac;
    theme::roundRect(x + 14, ly, bw, 16, 6, theme::kGreyDark);
    theme::roundRect(x + 15, ly + 1, bw - 2, 14, 5, theme::kPaperShade);
    if (frac > 0) theme::roundRect(x + 15, ly + 1, (bw - 2) * frac, 14, 5, ready ? theme::kGreen : kDeptInk[dept]);
    text_.centered(x + 14 + bw * 0.5f, ly + 1, theme::kTextSmall, ready ? theme::kWhite : theme::kInk, bw - 8,
                   need ? "%u of %u" : "%u", unsigned(merits), unsigned(need));
    ly += 20;
    if (ready) text_.fit(x + 14, ly, theme::kTextSmall, theme::kGreenDark, pw - 20, "Ready for a promotion!");
    focus_.drawHighlight(); // padnav
}

} // namespace tt::ui
