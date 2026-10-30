#include "ui/book_pages/page_cogs.h"
#include "ui/pad_tables.h"
#include "ui/suit_heads_atlas.h"
#include "ui/theme.h"

namespace tt::ui {

namespace {

const char* const kDeptNames[4] = { "Bossbot", "Lawbot", "Cashbot", "Sellbot" };

constexpr u32 kDeptColors[4] = {
    C2D_Color32(0x9E, 0x88, 0x88, 0xFF), // Bossbot brown/gray
    C2D_Color32(0x80, 0x90, 0xA8, 0xFF), // Lawbot blue
    C2D_Color32(0x78, 0xA8, 0x90, 0xFF), // Cashbot green
    C2D_Color32(0xB0, 0x88, 0x98, 0xFF), // Sellbot purple
};

const char* const kCogNames[4][8] = {
    { "Flunky", "Pencil Pusher", "Yesman", "Micromanager",
      "Downsizer", "Head Hunter", "Corp Raider", "The Big Cheese" },
    { "Bottom Feeder", "Bloodsucker", "Double Talker", "Ambulance Chaser",
      "Back Stabber", "Spin Doctor", "Legal Eagle", "Big Wig" },
    { "Short Change", "Penny Pincher", "Tightwad", "Bean Counter",
      "Number Cruncher", "Money Bags", "Land Shark", "Robber Baron" },
    { "Cold Caller", "Telemarketer", "Name Dropper", "Glad Hander",
      "Mover & Shaker", "Two-Face", "The Mingler", "Mr. Hollywood" },
};

constexpr int kCogQuotas[2][8] = {
    { 30, 25, 20, 15, 10, 5, 2, 1 },
    { 45, 40, 35, 30, 25, 20, 15, 10 }
};

// SuitPage.PANEL_COLORS, PANEL_COLORS_COMPLETE1 / 2 (one per department).
constexpr u32 kPanel[3][4] = {
    {theme::rgb(204, 199, 196), theme::rgb(191, 199, 204), theme::rgb(191, 209, 201), theme::rgb(210, 194, 196)},
    {theme::rgb(179, 185, 139), theme::rgb(159, 185, 166), theme::rgb(153, 191, 134), theme::rgb(172, 172, 140)},
    {theme::rgb(230, 185, 82), theme::rgb(210, 185, 115), theme::rgb(204, 191, 83), theme::rgb(223, 172, 89)},
};

bool inRect(const touchPosition& t, float x, float y, float w, float h) {
    return t.px >= x && t.px < x + w && t.py >= y && t.py < y + h;
}

} // namespace

void PageCogs::init() {
    text_.init(1536);
    pad::buildBookDepartments(focus_); // padnav
    SuitHeadAtlas::instance().init();
}

void PageCogs::shutdown() { text_.shutdown(); }

u32 PageCogs::handleInput(u32 keysDown, const touchPosition& touch, BookDrawContext& bdc) {
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

// SuitPage, one department at a time: a panel per Cog in the department's colour, the head
// and name once battled, "count of quota" once defeated, gold once the quota is complete.
void PageCogs::draw(float x, float y, float w, float h, const BookDrawContext& bdc) {
    using namespace pad::book;
    (void)y;
    (void)h;
    SuitHeadAtlas& atlas = SuitHeadAtlas::instance();
    for (int d = 0; d < kDepts; d++) {
        const bool on = d == activeDept_;
        const float tx = kDeptX + d * kDeptStep;
        theme::button(tx, on ? kDeptY : kDeptY + 2, kDeptW, kDeptH + (on ? 2 : 0), on ? kPanel[2][d] : kPanel[0][d],
                      theme::shade(on ? kPanel[2][d] : kPanel[0][d], 0.6f), false, 5.0f);
        text_.centered(tx + kDeptW * 0.5f, (on ? kDeptY : kDeptY + 2) + 1, theme::kTextSmall,
                       on ? theme::kInk : theme::kInkSoft, kDeptW - 6, "%s", kDeptNames[d]);
    }

    constexpr float kGap = 3.0f;
    const float gridY = kDeptY + kDeptH + 6;
    const float cellW = (w - 12 - 3 * kGap) / 4, cellH = 70.0f;
    for (int i = 0; i < 8; i++) {
        const float cx = x + 6 + (i % 4) * (cellW + kGap), cy = gridY + (i / 4) * (cellH + kGap);
        const int idx = activeDept_ * 8 + i;
        const uint32_t status = bdc.book ? bdc.book->cogStatus[idx] : uint32_t(net::kCogUnseen);
        const unsigned count = bdc.book ? bdc.book->cogCount[idx] : 0;
        const int tier = status >= net::kCogComplete2 ? 2 : status >= net::kCogComplete1 ? 1 : 0;
        const u32 face = kPanel[tier][activeDept_];
        theme::roundRect(cx, cy, cellW, cellH, 6, theme::shade(face, 0.6f));
        theme::roundRect(cx + 1, cy + 1, cellW - 2, cellH - 2, 5, face);
        if (status <= net::kCogUnseen) { // SuitPageMystery in black at text_pos (0, 0.185): the card's top
            text_.centered(cx + cellW * 0.5f, cy + 8, theme::kTextSmall, theme::kInk, 0, "???");
            continue;
        }
        theme::roundRect(cx + (cellW - 38) * 0.5f, cy + 3, 38, 36, 6, theme::mix(face, theme::kWhite, 0.45f));
        atlas.drawHead(activeDept_, i, cx + (cellW - 34) * 0.5f, cy + 4, 34.0f, 0.6f);
        text_.centered(cx + cellW * 0.5f, cy + 41, theme::kTextTiny, theme::kInk, cellW - 6, "%s",
                       kCogNames[activeDept_][i]);
        if (status >= net::kCogDefeated)
            text_.centered(cx + cellW * 0.5f, cy + 54, theme::kTextTiny, tier ? theme::kRedDark : theme::kInkSoft,
                           cellW - 6, "%u of %d", count, kCogQuotas[tier ? 1 : 0][i]);
    }

    // The radars (SuitPage.CogRadar / BuildingRadar), once earned from the Cog Suit quotas.
    const float fy = gridY + 2 * (cellH + kGap) + 2;
    const bool cogRadar = bdc.book && bdc.book->cogRadar[activeDept_];
    const unsigned bldgRadar = bdc.book ? bdc.book->buildingRadar[activeDept_] : 0;
    text_.draw(x + 10, fy, theme::kTextTiny, cogRadar ? theme::kGreenDark : theme::kInkSoft, "Cog Radar: %s",
               cogRadar ? "on" : "not yet earned");
    text_.right(x + w - 10, fy, theme::kTextTiny, bldgRadar ? theme::kGreenDark : theme::kInkSoft, 0,
                "Building Radar: %s", bldgRadar ? "on" : "not yet earned");
    focus_.drawHighlight(); // padnav
}

} // namespace tt::ui
