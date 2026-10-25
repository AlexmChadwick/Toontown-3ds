#include "ui/book_pages/page_cogs.h"
#include "ui/pad_tables.h"
#include "ui/suit_heads_atlas.h"
#include <cstdio>

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

constexpr u32 kInk = C2D_Color32(0x20, 0x18, 0x10, 0xFF);
constexpr u32 kCardBg = C2D_Color32(0xFF, 0xFA, 0xEA, 0xFF);
constexpr u32 kCompleteBg = C2D_Color32(0xFF, 0xF0, 0xC0, 0xFF);

} // namespace

void PageCogs::init() {
    textBuf_ = C2D_TextBufNew(4096);
    pad::buildBookDepartments(focus_); // padnav
    SuitHeadAtlas::instance().init();
}

void PageCogs::shutdown() {
    if (textBuf_) {
        C2D_TextBufDelete(textBuf_);
        textBuf_ = nullptr;
    }
}

u32 PageCogs::handleInput(u32 keysDown, const touchPosition& touch, BookDrawContext& bdc) {
    (void)bdc;
    // ---- padnav: D-Left / D-Right step through the department tabs (L / R turn pages) ----
    if (keysDown & (KEY_DLEFT | KEY_DRIGHT)) {
        const int step = (keysDown & KEY_DLEFT) ? pad::book::kDepts - 1 : 1;
        activeDept_ = (activeDept_ + step) % pad::book::kDepts;
        focus_.setFocusById(activeDept_);
        focus_.logActivate(activeDept_);
        return keysDown & (KEY_DLEFT | KEY_DRIGHT);
    }
    // ---- end padnav ----
    if (keysDown & KEY_TOUCH) {
        // Check 4 department tabs at top: y = 20 .. 38
        if (touch.py >= 20 && touch.py <= 38) {
            for (int d = 0; d < 4; d++) {
                const float tx = 14.0f + d * 70.0f;
                if (touch.px >= tx && touch.px <= tx + 66.0f) {
                    activeDept_ = d;
                    focus_.setFocusById(d); // padnav
                    return KEY_TOUCH;
                }
            }
        }
    }
    return 0;
}

void PageCogs::draw(float x, float y, float w, float h, const BookDrawContext& bdc) {
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

    // Grid of 8 cogs (2 rows x 4 columns)
    const float gridStartY = y + 24.0f;
    const float cellW = 68.0f;
    const float cellH = 64.0f;
    const float cellGapX = 4.0f;
    const float cellGapY = 4.0f;

    char buf[128];

    for (int i = 0; i < 8; i++) {
        const int row = i / 4;
        const int col = i % 4;
        const float cx = x + 6.0f + col * (cellW + cellGapX);
        const float cy = gridStartY + row * (cellH + cellGapY);

        const int idx = activeDept_ * 8 + i;
        const uint32_t status = bdc.book ? bdc.book->cogStatus[idx] : net::kCogUnseen;
        const uint32_t count = bdc.book ? bdc.book->cogCount[idx] : 0;
        const bool complete = (status >= net::kCogComplete1);

        // Card box
        C2D_DrawRectSolid(cx, cy, 0.5f, cellW, cellH, complete ? C2D_Color32(0xC0, 0x90, 0x20, 0xFF) : C2D_Color32(0xB0, 0xA8, 0x98, 0xFF));
        C2D_DrawRectSolid(cx + 1.0f, cy + 1.0f, 0.5f, cellW - 2.0f, cellH - 2.0f, complete ? kCompleteBg : kCardBg);

        if (status == net::kCogUnseen) {
            // Unseen / Mystery
            C2D_Text qTxt;
            C2D_TextParse(&qTxt, textBuf_, "?");
            C2D_TextOptimize(&qTxt);
            C2D_DrawText(&qTxt, C2D_WithColor, cx + 28.0f, cy + 10.0f, 0.6f, 0.70f, 0.70f,
                         C2D_Color32(0xAA, 0xA0, 0x98, 0xFF));

            C2D_Text mystTxt;
            C2D_TextParse(&mystTxt, textBuf_, "???");
            C2D_TextOptimize(&mystTxt);
            C2D_DrawText(&mystTxt, C2D_WithColor, cx + 25.0f, cy + 46.0f, 0.6f, 0.30f, 0.30f,
                         C2D_Color32(0x88, 0x80, 0x78, 0xFF));
        } else {
            // Draw head portrait
            atlas.drawHead(activeDept_, i, cx + (cellW - 28.0f) * 0.5f, cy + 3.0f, 28.0f, 0.6f);

            // Cog Name
            C2D_Text nameTxt;
            C2D_TextParse(&nameTxt, textBuf_, kCogNames[activeDept_][i]);
            C2D_TextOptimize(&nameTxt);
            C2D_DrawText(&nameTxt, C2D_WithColor, cx + 4.0f, cy + 33.0f, 0.6f, 0.28f, 0.28f, kInk);

            // Quota
            const int quotaTier = (status >= net::kCogComplete1) ? 1 : 0;
            const int quota = kCogQuotas[quotaTier][i];
            snprintf(buf, sizeof(buf), "%u / %d", unsigned(count), quota);
            C2D_Text quotaTxt;
            C2D_TextParse(&quotaTxt, textBuf_, buf);
            C2D_TextOptimize(&quotaTxt);
            C2D_DrawText(&quotaTxt, C2D_WithColor, cx + 4.0f, cy + 46.0f, 0.6f, 0.26f, 0.26f,
                         complete ? C2D_Color32(0x80, 0x50, 0x10, 0xFF) : C2D_Color32(0x50, 0x48, 0x40, 0xFF));
        }
    }

    // Bottom info: Radars for current department
    const float footerY = gridStartY + 2 * (cellH + cellGapY) + 4.0f;
    const bool cogRadar = bdc.book ? (bdc.book->cogRadar[activeDept_] != 0) : false;
    const uint8_t bldgRadar = bdc.book ? bdc.book->buildingRadar[activeDept_] : 0;

    snprintf(buf, sizeof(buf), "Cog Radar: %s   Building Radar: %s (%d)",
             cogRadar ? "ACTIVE" : "INACTIVE",
             bldgRadar > 0 ? "ACTIVE" : "INACTIVE",
             int(bldgRadar));
    C2D_Text radarTxt;
    C2D_TextParse(&radarTxt, textBuf_, buf);
    C2D_TextOptimize(&radarTxt);
    C2D_DrawText(&radarTxt, C2D_WithColor, x + 10.0f, footerY, 0.5f, 0.32f, 0.32f,
                 C2D_Color32(0x40, 0x38, 0x30, 0xFF));
    focus_.drawHighlight(); // padnav
}

} // namespace tt::ui
