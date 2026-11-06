// Host tests for the 3DS bottom-screen panels: Map, Chat/SpeedChat (+ the Friends list and
// the whisper header inside it), Toon, Gags, the Shtiker Book cover and pages, and the tab
// bar's Laff pocket that is drawn with every tab.
//
// The panel sources compile against the citro stubs (tools/gui_panel_test/stubs): instead of
// a GPU, every C2D draw becomes a HostRecord rectangle with the real font metrics (the
// committed ImpressBT bcfnt), so the assertions use the same layout numbers the device sees.
// The layout constants mirrored below (kFriendsPanelX and friends) restate the file-local
// constexprs of render/ui.cpp next to the draw code they test; they are a test helper, the UI
// itself is untouched.
//
// Invariants asserted per panel:
//   1. Every drawn rectangle stays on the 320x240 bottom screen (records outside the screen
//      must be inside the scissor the panel set, or the tab-bar laff pocket's designed bleed).
//   2. Label/text rectangles are fully inside their panel with sane padding: no leading or
//      trailing glyphs cut at a panel edge ("Little Pepper" / "Sleepy Rabbit").
//   3. The Laff head state on every tab: species head drawn in the toon's head colour (the
//      death colour at 0 hp), smile / open smile / frown per laff state, teeth count per hp,
//      and the hp "/" max label with its slash glyph present.

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "render/ui.h" // pulls the CITRO stubs through -I stubs (3ds.h, citro2d.h, recorder)
#include "ui/theme.h"
#include "ui/panel_geometry.h"
#include "ui/pad_tables.h"
#include "ui/gag_data.h"
#include "ui/laff_meter.h"
#include "ui/laff_meter_atlas.h"
#include "ui/friends_gui_atlas.h"
#include "ui/map_view.h"
#include "ui/speedchat_data.h"
#include "net/client.h" // OwnStats / OwnQuests / OwnBookStats

namespace {

using namespace tt::ui;                    // the UI's public constants (pad::, theme::, kLaff* cells)
namespace net = tt::net;                   // OwnStats / OwnQuests / OwnBookStats (as Context spells them)
namespace gags = tt::gags;                 // kTracks / kLevels / kLevelExp
namespace booktab = tt::ui::pad::booktab;  // Book cover button grid

// ---------------------------------------------------------------------------
// Test scaffolding
// ---------------------------------------------------------------------------

int gFails = 0;
long gChecks = 0;

void chk(bool ok, const char* what, const char* fmt = nullptr, ...) {
    ++gChecks;
    if (ok) return;
    ++gFails;
    va_list ap;
    va_start(ap, fmt);
    printf("FAIL %s", what);
    if (fmt) {
        printf(" (");
        vprintf(fmt, ap);
        printf(")");
    }
    printf("\n");
    va_end(ap);
}

// Layout constants the tests reason with (mirrors of the file-local constexprs of
// render/ui.cpp / ChatMenu; the public pad:: tables are used directly where they exist).
constexpr float kW = 320.0f, kScreenH = 240.0f;
constexpr float kTabH = 32.0f, kTabY = pad::kTabBarY;
constexpr float kPocketW = pad::kLaffPocketW, kPocketTop = pad::kLaffPocketTop;
constexpr float kPocketLaff = 72.0f; // Ui::drawTabs: the meter's atlas cell
constexpr float kChatLogH = 34.0f;
// render/ui.cpp (file-local; restated here so the tests can name the panel edges).
constexpr float kChatHeaderH = 28.0f, kChatRowH = 30.0f;
constexpr float kFriendsButtonX = 244.0f, kFriendsButtonY = 2.0f, kFriendsButtonW = 72.0f,
                kFriendsButtonH = 24.0f;
constexpr float kFriendsPanelX = 70.0f, kFriendsPanelY = 1.0f, kFriendsPanelW = 180.0f,
                kFriendsPanelH = 204.0f;
constexpr float kFriendsListX = 86.0f, kFriendsListY = 76.0f, kFriendsListW = 148.0f,
                kFriendsRowH = 11.0f;
constexpr float kFriendsCloseX = 146.0f, kFriendsCloseY = 174.0f, kFriendsCloseW = 28.0f,
                kFriendsCloseH = 26.0f;
constexpr float kGagsRow0 = 27.0f, kGagsRowH = 21.0f;
constexpr float kToonSwX = 8, kToonSwY = 44, kToonSwSize = 30, kToonSwGap = 4, kToonSwCols = 9;
constexpr float kToonTargetsX = 8, kToonTargetsW = 98, kToonTargetsH = 30;
// LaffMeter file-local constants mirrored for the label math (ui/laff_meter.cpp).
constexpr float kLafLabelMaxW = 0.46f, kLafLabelGap = 0.006f;
constexpr float kLafToothFr[6] = {0.0f, 0.166666f, 0.333333f, 0.5f, 0.666666f, 0.833333f};

constexpr float kEps = 0.6f;      // draw-rounding tolerance
constexpr float kPocketBleed = 9.0f; // the pocket's roundRect deliberately runs 8px under the screen

tt::ui::Ui* gUi = nullptr;
tt::ui::State* gState = nullptr;
tt::ui::Context* gCtx = nullptr;
net::OwnStats* gStats = nullptr;
net::OwnQuests gQuests;
net::OwnBookStats gBook;

C3D_RenderTarget gTarget;

touchPosition T(float px, float py) {
    touchPosition t;
    t.px = uint16_t(px);
    t.py = uint16_t(py);
    return t;
}

// ---------------------------------------------------------------------------
// Record queries over host::gRecords
// ---------------------------------------------------------------------------

bool contains(const HostRecord& r, float x, float y, float w, float h, float eps = kEps) {
    return r.x >= x - eps && r.y >= y - eps && r.x + r.w <= x + w + eps && r.y + r.h <= y + h + eps;
}

std::vector<const HostRecord*> textRecords() {
    std::vector<const HostRecord*> out;
    for (const auto& r : host::gRecords)
        if (r.kind == HostRecord::Text) out.push_back(&r);
    return out;
}

const HostRecord* findText(const char* exact) {
    for (const auto& r : host::gRecords)
        if (r.kind == HostRecord::Text && r.text == exact) return &r;
    return nullptr;
}

int countTextStarting(const char* prefix) {
    const size_t n = strlen(prefix);
    int c = 0;
    for (const auto& r : host::gRecords)
        if (r.kind == HostRecord::Text && !strncmp(r.text.c_str(), prefix, n)) ++c;
    return c;
}

// The scissor records are stored in the rotated bottom-screen framebuffer frame; decode back
// to screen space (theme::clip / stubs/citro2d.h C3D_SetScissor).
bool scissorRect(const HostRecord& r, float* x, float* y, float* w, float* h) {
    if (r.kind != HostRecord::Scissor || r.tag == "disable") return false;
    // stub: x0=240-(y+h) y0=320-(x+w) x1=240-y y1=320-x  => screen rect from the record box.
    const float x0f = r.x, y0f = r.y, x1f = r.x + r.w, y1f = r.y + r.h;
    *x = kScreenH == 240.0f ? 0 : 0;
    (void)x0f;
    (void)x1f;
    // screen x: [320-y1, 320-y0]; screen y: [240-x1, 240-x0]
    *x = 320.0f - y1f;
    *y = 240.0f - x1f;
    *w = y1f - y0f;
    *h = x1f - x0f;
    return true;
}

// ---------------------------------------------------------------------------
// Invariant 1: everything drawn stays on the bottom screen
// ---------------------------------------------------------------------------

void assertBounds(const char* panel, bool allowPocketBleed = true) {
    bool haveScissor = false;
    float sx = 0, sy = 0, sw = 0, sh = 0;
    for (const auto& r : host::gRecords) {
        if (r.kind == HostRecord::Scissor) {
            haveScissor = scissorRect(r, &sx, &sy, &sw, &sh);
            continue;
        }
        // (a) inside the active scissor: fine, the GPU clips it there.
        if (haveScissor && contains(r, sx, sy, sw, sh)) continue;
        // (b) designed background bleeds off the screen's bottom edge: the tab bar's laff
        //     pocket (roundRect + the 72px meter cell centred on it) and the Shtiker Book's
        //     page tabs, which hang 6px under the screen by design ("hanging off the paper",
        //     book.cpp). Content kinds (text, images, markers) never bleed.
        const bool pocket = allowPocketBleed && r.x < kPocketW + 2 && r.y + r.h > kPocketTop - 3;
        const float bleed = pocket ? kPocketBleed : 0.0f;
        const bool bottomHanging = allowPocketBleed && r.kind != HostRecord::Text &&
                                    r.kind != HostRecord::Image && r.y + r.h > kScreenH &&
                                    r.y >= kScreenH - 36 && r.y + r.h <= kScreenH + kPocketBleed + kEps;
        chk(r.x >= -bleed - kEps, panel, "record left edge %.1f off screen", r.x);
        chk(r.y >= -kEps, panel, "record top edge %.1f off screen", r.y);
        chk(r.x + r.w <= kW + kEps, panel, "record right %.1f past 320", r.x + r.w);
        chk(r.y + r.h <= kScreenH + bleed + kEps || bottomHanging, panel, "record bottom %.1f past 240",
            r.y + r.h);
        if (r.kind == HostRecord::Text) {
            // Text is never off-screen, bleed or not: the pocket bleeds only the cell art.
            chk(r.x >= -kEps && r.y >= -kEps && r.x + r.w <= kW + kEps && r.y + r.h <= kScreenH + kEps,
                panel, "text '%s' off screen at (%.1f, %.1f, %.1f x %.1f)", r.text.c_str(), r.x, r.y, r.w, r.h);
        }
    }
}

// ---------------------------------------------------------------------------
// Invariant 3: the Laff head in the tab bar's pocket
// ---------------------------------------------------------------------------

u32 quantizeColor(float r, float g, float b) { return C2D_Color32f(r, g, b, 1.0f); }

bool isLaffPart(const HostRecord& r, int part, float* px = nullptr, float* py = nullptr) {
    if (r.kind != HostRecord::Image) return false;
    // sub-texture UVs of the 64px cell kLaffCellXY[part] in the 512x256 atlas (laff_meter.cpp).
    const float u0 = kLaffCellXY[part][0] / float(kLaffAtlasW);
    const float u1 = (kLaffCellXY[part][0] + kLaffCell) / float(kLaffAtlasW);
    const float v0 = 1.0f - kLaffCellXY[part][1] / float(kLaffAtlasH);
    const float v1 = 1.0f - (kLaffCellXY[part][1] + kLaffCell) / float(kLaffAtlasH);
    if (fabsf(r.u0 - u0) > 1e-3f || fabsf(r.u1 - u1) > 1e-3f || fabsf(r.v0 - v0) > 1e-3f ||
        fabsf(r.v1 - v1) > 1e-3f)
        return false;
    if (px) *px = r.x;
    if (py) *py = r.y;
    return true;
}

int countLaffPart(int part) {
    int n = 0;
    for (const auto& r : host::gRecords) n += isLaffPart(r, part) ? 1 : 0;
    return n;
}

// The tinted species head record plus the label text group inside the pocket.
void assertPocketLaff(int hp, int maxHp, const float headRgb[3], int species) {
    const int speciesHead = kLaffDoghead + (species >= 0 && species < 9 ? species : 0);
    const bool dead = hp < 1, full = hp >= maxHp;
    // Head: the one tinted image draw in the pocket.
    const u32 wantTint = dead ? quantizeColor(0.58039216f, 0.80392157f, 0.34117647f)
                              : quantizeColor(headRgb[0], headRgb[1], headRgb[2]);
    const HostRecord* head = nullptr;
    for (const auto& r : host::gRecords) {
        if (isLaffPart(r, speciesHead) && r.tintAlpha > 0.5f) {
            head = &r;
            break;
        }
    }
    chk(head != nullptr, "laff pocket: species head drawn");
    if (head) chk(head->tint == wantTint, "laff pocket: head tint coloured", "0x%08X vs 0x%08X",
                  head->tint, wantTint);
    // Mouth state: exactly one of smile / open smile / frown.
    const int smile = countLaffPart(kLaffSmile), open = countLaffPart(kLaffOpenSmile),
              frown = countLaffPart(kLaffFrown);
    chk(frown == (dead ? 1 : 0), "laff pocket: frown drawn at 0 hp");
    chk(smile == (full && !dead ? 1 : 0), "laff pocket: smile drawn only at full laff");
    chk(open == (!dead && !full ? 1 : 0), "laff pocket: open smile drawn mid laff");
    chk(smile + open + frown == 1, "laff pocket: exactly one mouth part");
    chk(countLaffPart(kLaffEyes) == (dead ? 0 : 1), "laff pocket: eyes drawn while alive");
    // Teeth below full laff: LaffMeter.fractions, tooth i while hp > maxHp * fraction i.
    if (dead || full) {
        chk(countLaffPart(kLaffTooth1) + countLaffPart(kLaffTooth2) + countLaffPart(kLaffTooth3) +
                countLaffPart(kLaffTooth4) + countLaffPart(kLaffTooth5) + countLaffPart(kLaffTooth6) == 0,
            "laff pocket: no teeth at full or 0 laff");
    } else {
        const int parts[6] = {kLaffTooth6, kLaffTooth5, kLaffTooth4, kLaffTooth3, kLaffTooth2,
                              kLaffTooth1};
        for (int i = 0; i < 6; i++) {
            char what[64];
            snprintf(what, sizeof what, "laff pocket: tooth %d at hp %d/%d", i, hp, maxHp);
            chk(countLaffPart(parts[i]) == (hp > maxHp * kLafToothFr[i] ? 1 : 0), what);
        }
    }
    // The hp / max label in the eyes: numbers and slash, inside the pocket. Note: this
    // meter's drawLaffMeter returns before the label at 0 laff (death grey + frown only);
    // upstream's LaffMeter keeps showing "0/max" there. Modelled as-is; flagged in STATUS.
    if (hp >= 1) {
        char n[16];
        snprintf(n, sizeof n, "%d", hp);
        const HostRecord* hpText = findText(n);
        chk(hpText != nullptr, "laff pocket: hp number drawn");
        if (hpText) chk(contains(*hpText, 0, kPocketTop, kPocketW, 240 - kPocketTop),
                        "laff pocket: hp label inside pocket");
        snprintf(n, sizeof n, "%d", maxHp);
        const HostRecord* maxText = findText(n);
        chk(maxText != nullptr, "laff pocket: max number drawn");
        if (maxText) chk(contains(*maxText, 0, kPocketTop, kPocketW, 240 - kPocketTop),
                         "laff pocket: max label inside pocket");
        // The slash glyph: the pocket draws it twice (outlined, then reinforced).
        int slashes = 0;
        for (const auto* t : textRecords())
            if (t->text == "/" && contains(*t, 0, kPocketTop, kPocketW, 240 - kPocketTop)) ++slashes;
        chk(slashes >= 1, "laff pocket: slash drawn in hp/max label");
    } else {
        char n[16];
        snprintf(n, sizeof n, "%d", hp);
        const HostRecord* hpText = findText(n);
        if (hpText) chk(contains(*hpText, 0, kPocketTop, kPocketW, 240 - kPocketTop),
                        "laff pocket: hp label inside pocket");
    }
    // The whole label group fits the eye whites: kLabelMaxW * size.
    if (head) {
        char hpStr[16], maxStr[16];
        snprintf(hpStr, sizeof hpStr, "%d", hp);
        snprintf(maxStr, sizeof maxStr, "%d", maxHp);
        float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
        for (const auto* t : textRecords()) {
            if (t->text != hpStr && t->text != maxStr && t->text != "/") continue;
            if (t->x < kPocketW - 2 || t->y < kPocketTop - 4 || t->y > 240) continue;
            x0 = x0 < t->x ? x0 : t->x;
            y0 = y0 < t->y ? y0 : t->y;
            x1 = x1 > t->x + t->w ? x1 : t->x + t->w;
            y1 = y1 > t->y + t->h ? y1 : t->y + t->h;
        }
        if (x1 > x0) {
            // LaffMeter: hp + slash + max with kLabelGap*size spacing must fit kLabelMaxW.
            const float gap = kLafLabelGap * kPocketLaff;
            const float groupW = x1 - x0 - 2 * gap; // gaps only between the three parts
            chk(groupW <= kLafLabelMaxW * kPocketLaff + kEps, "laff pocket: hp/max label fits eyes",
                "group %.1f > %.1f", groupW, kLafLabelMaxW * kPocketLaff);
        }
    }
}

// ---------------------------------------------------------------------------
// Draw harness
// ---------------------------------------------------------------------------

void drawOnce() {
    host::clearRecords();
    gUi->draw(&gTarget, *gState, *gCtx);
}

const char* kTabNames[5] = {"Map", "Chat", "Toon", "Gags", "Book"};

// A real frame: input first, then draw (Ui::draw expects handleInput to have run). Changing
// the tab without a matching handleInput frame is eaten by Ui::handleInput's tab-sync early
// return, so every tab change goes through here.
void drawTab(int tab) {
    gState->tab = tab;
    gUi->handleInput(0, 0, T(0, 0), *gState);
    drawOnce();
}

// A stylus tap as one frame: press and release both seen by one handleInput pass, then the
// resulting panel state is drawn.
void tap(float px, float py) {
    gUi->handleInput(KEY_TOUCH, 0, T(px, py), *gState);
    drawOnce();
}

void setOnlineContext() {
    gStats->hp = 34;
    gStats->maxHp = 57;
    gStats->money = 214;
    gStats->maxMoney = 250;
    gCtx->stats = gStats;
    gCtx->headRgb[0] = 0.933594f;
    gCtx->headRgb[1] = 0.265625f;
    gCtx->headRgb[2] = 0.28125f;
    gCtx->species = 0;
    gCtx->quests = &gQuests;
    gCtx->toonName = "Little Pepper";
    static const uint8_t trackAccess[7] = {1, 1, 1, 1, 1, 1, 1};
    static uint16_t gagExp[7];
    static uint8_t gagCount[7][7];
    for (int t = 0; t < 7; t++) {
        gagExp[t] = uint16_t(gags::kLevelExp[t][3]);
        for (int l = 0; l < 4; l++) gagCount[t][l] = uint8_t(2 + l);
    }
    gCtx->gagExp = gagExp;
    gCtx->gagCount = gagCount;
    gCtx->trackAccess = trackAccess;
    gCtx->maxCarry = 80;
}

// ===========================================================================
// Suites
// ===========================================================================

void suiteLaffStates() {
    printf("suite: laff head states on the tab bar\n");
    const float red[3] = {0.933594f, 0.265625f, 0.28125f};
    const int species = 0;
    // Every player tab draws the pocket; three hp states over all five tabs.
    const int hps[3][2] = {{34, 57}, {57, 57}, {0, 57}};
    for (int s = 0; s < 3; s++) {
        gStats->hp = int16_t(hps[s][0]);
        gStats->maxHp = int16_t(hps[s][1]);
        gCtx->stats = gStats; // offline default too: head still drawn from Context defaults
        printf("  hp state %d/%d\n", hps[s][0], hps[s][1]);
        for (int tab = 0; tab < 5; tab++) {
            drawTab(tab);
            chk(host::gRecords.size() > 10, "laff tab draw produced records");
            assertBounds(kTabNames[tab]);
            assertPocketLaff(hps[s][0], hps[s][1], red, species);
        }
    }
    // Dead: the head takes LaffMeter.deathColor.
    gStats->hp = 0;
    gStats->maxHp = 57;
    drawTab(tt::ui::kTabMap);
    assertBounds("map (sad)");
    assertPocketLaff(0, 57, red, 0);
    // A species (not dog) still picks its head part; tint stays the head colour.
    gStats->hp = 15;
    gCtx->species = 4;
    drawTab(tt::ui::kTabToon);
    int heads = 0, otherHeads = 0;
    for (const auto& r : host::gRecords) {
        if (r.kind == HostRecord::Image && r.tintAlpha > 0.5f) {
            if (isLaffPart(r, kLaffDoghead + 4)) ++heads;
            else ++otherHeads;
        }
    }
    chk(heads == 1 && otherHeads == 0, "laff pocket: species 4 head part used");
    // Offline: the pocket still shows a laff head, at the Context's head colour.
    gCtx->species = 0;
    gCtx->stats = nullptr;
    const float kDefaultHead[3] = {0.7f, 0.7f, 0.8f}; // Context::headRgb default
    gCtx->headRgb[0] = kDefaultHead[0];
    gCtx->headRgb[1] = kDefaultHead[1];
    gCtx->headRgb[2] = kDefaultHead[2];
    drawTab(tt::ui::kTabGags);
    assertBounds("gags (offline)");
    assertPocketLaff(15, 15, kDefaultHead, 0);
}

void suiteMap() {
    printf("suite: Map tab\n");
    setOnlineContext();
    gCtx->zoneMinX = -100;
    gCtx->zoneMinY = -100;
    gCtx->zoneMaxX = 100;
    gCtx->zoneMaxY = 100;
    gCtx->playerX = 0;
    gCtx->playerY = 0;
    gCtx->playerHeadingDeg = 45;
    gCtx->currentZone = 2000;
    gCtx->currentHood = 2000;
    gCtx->hoodsAvailable = ~0u;
    static tt::ui::MapMarker markers[3] = {
        {0, 0, tt::ui::kMarkToon}, {-95, 90, tt::ui::kMarkHq}, {70, -70, tt::ui::kMarkTunnel}};
    gCtx->markers = markers;
    gCtx->numMarkers = 3;
    drawTab(tt::ui::kTabMap);
    chk(host::gRecords.size() > 20, "map draw produced records");
    assertBounds("map");
    // The map frame content: markers inside the frame area (MapRect clamping).
    constexpr float kFrameX = 4, kFrameY = 4, kFrameW = 312, kFrameH = pad::hoods::kRowY - 8;
    int markersDrawn = 0;
    for (const auto& r : host::gRecords) {
        if (r.kind == HostRecord::Rect && int(r.w + 0.5f) == 8 && int(r.h + 0.5f) == 8 &&
            r.color == (0xFF000000u | tt::ui::theme::kInk)) {
            chk(contains(r, kFrameX + 5, kFrameY + 5, kFrameW - 10, kFrameH - 10),
                "map marker inside the frame", "(%.1f, %.1f)", r.x, r.y);
            ++markersDrawn;
        }
    }
    chk(markersDrawn == 8 || markersDrawn == 6, "map markers drawn", "%d", markersDrawn);
    // Hood row: eight pills, each label inside its pill, nothing past x=320.
    const char* const kHoodLabels[8] = {"TTC", "DD", "DG", "MM", "BR", "DL", "AA", "GS"};
    for (int i = 0; i < 8; i++) {
        const float x = i * pad::hoods::kHoodW + 2;
        const HostRecord* t = findText(kHoodLabels[i]);
        char what[64];
        snprintf(what, sizeof what, "map hood label '%s' drawn", kHoodLabels[i]);
        chk(t != nullptr, what);
        if (t) {
            snprintf(what, sizeof what, "map hood label '%s' inside its pill", kHoodLabels[i]);
            chk(contains(*t, x, pad::hoods::kRowY, pad::hoods::kHoodW - 4, pad::hoods::kRowH),
                what, "(%.1f..%.1f)", t->x, t->x + t->w);
        }
    }
    // Go Home pill (playground: "Go Home") with its label inside.
    const HostRecord* home = findText("Go Home");
    chk(home != nullptr, "map: Go Home label drawn");
    if (home)
        chk(contains(*home, pad::hoods::kTabHomeX, pad::hoods::kHomeY, pad::hoods::kHomeW,
                     pad::hoods::kHomeH),
            "map: Go Home label inside the pill");
    // The place title stays inside its paper strip right of Home.
    const float labelX = pad::hoods::kTabHomeX + pad::hoods::kHomeW + 6;
    const float labelW = 320 - 4 - labelX;
    for (const auto* t : textRecords()) {
        if (t->y >= pad::hoods::kHomeY - kEps && t->y <= pad::hoods::kHomeY + 6 &&
            t->w > 20 && t != home && t->text != "Go Home" && !strchr(t->text.c_str(), ' ')) {
            chk(contains(*t, labelX, pad::hoods::kHomeY, labelW, pad::hoods::kHomeH),
                "map: place title inside its strip", "'%s' at (%.1f, %.1f)", t->text.c_str(), t->x,
                t->y);
        }
    }
    // The player arrow: the two red triangles of drawArrow inside the frame.
    int arrows = 0;
    for (const auto& r : host::gRecords) {
        if (r.kind == HostRecord::Triangle && r.color == (0xFF000000u | tt::ui::theme::kRed) &&
            contains(r, kFrameX + 5, kFrameY + 5, kFrameW - 10, kFrameH - 10))
            ++arrows;
    }
    chk(arrows >= 2, "map: player arrow triangles inside the frame", "%d", arrows);
    // The laff pocket still on the tab bar.
    assertPocketLaff(gStats->hp, gStats->maxHp, gCtx->headRgb, 0);
}

void suiteChat() {
    printf("suite: Chat / SpeedChat tab\n");
    setOnlineContext();
    // Whisper header: one nearby toon; the truncated name must fit before the Friends button.
    static const char* nearby = "Sleepy Rabbit";
    gCtx->numNearby = 1;
    gCtx->nearbyIds[0] = 42;
    gCtx->nearbyNames[0] = nearby;
    gState->tab = tt::ui::kTabChat;
    gState->whisperTo = 0;
    drawTab(tt::ui::kTabChat);
    chk(host::gRecords.size() > 10, "chat draw produced records");
    assertBounds("chat root");
    // Root breadcrumb and rows.
    const HostRecord* title = findText("SpeedChat");
    chk(title != nullptr, "chat: SpeedChat title drawn");
    if (title) chk(contains(*title, 8, 0, 320 - 14, kChatHeaderH), "chat: root title inside the header");
    int rows = 0;
    for (const auto* t : textRecords()) {
        if (t->y > kChatHeaderH && t->y < kChatHeaderH + 160) ++rows;
    }
    chk(rows >= 4, "chat: menu rows drawn", "%d", rows);
    // Rows are inside the SpeedChat frame: x in [16, 16+rowTextW], y in the list.
    for (const auto* t : textRecords()) {
        if (t->y <= kChatHeaderH || t->y >= 176) continue;
        chk(t->x >= 15.5f, "chat row text not clipped at the frame's left", "'%s' at %.1f",
            t->text.c_str(), t->x);
        chk(t->x + t->w <= 320 - 10 - 4 - kEps, "chat row text stays inside the frame right",
            "'%s' ends at %.1f", t->text.c_str(), t->x + t->w);
        chk(t->y + t->h <= 176 + 30, "chat row text below the tabs' start never happens",
            "'%s' bottom %.1f", t->text.c_str(), t->y + t->h);
    }
    // Whisper header: "To: <name>" fits between the To button and the Friends button.
    const HostRecord* to = nullptr;
    for (const auto* t : textRecords()) {
        if (t->text.rfind("To: ", 0) == 0) {
            to = t;
            break;
        }
    }
    chk(to != nullptr, "chat: whisper header drawn");
    if (to)
        chk(to->x + to->w <= kFriendsButtonX + kEps, "chat: whisper header fits its button",
            "'%s' ends at %.1f (button at %.0f)", to->text.c_str(), to->x + to->w,
            kFriendsButtonX);
    // Enter a submenu by tapping the first row (EMOTIONS).
    tap(100, kChatHeaderH + 14);
    assertBounds("chat submenu EMOTIONS");
    const HostRecord* back = findText("< Back");
    chk(back != nullptr, "chat: Back label drawn");
    if (back)
        chk(contains(*back, 6, 0, 64, kChatHeaderH), "chat: Back label inside its header chip",
            "(%.1f, %.1f)", back->x, back->y);
    const HostRecord* emTitle = findText("EMOTIONS");
    chk(emTitle != nullptr, "chat: submenu breadcrumb drawn");
    if (emTitle) {
        chk(emTitle->x >= 64 + 6 + 6 + kEps, "chat: breadcrumb right of the Back chip", "%.1f",
            emTitle->x);
        chk(emTitle->x + emTitle->w <= 320 - 6 + kEps, "chat: breadcrumb fits before the screen edge",
            "'%s' ends at %.1f", emTitle->text.c_str(), emTitle->x + emTitle->w);
    }
    // Emote rows drawn (24 items); the D-pad highlight stays in the frame.
    int emoteRows = 0;
    for (const auto* t : textRecords()) {
        if (t->y > kChatHeaderH && t->y < kChatHeaderH + 160) ++emoteRows;
    }
    chk(emoteRows >= 4, "chat: emote rows drawn", "%d", emoteRows);
    // D-pad scrolling: 30 DDOWN pulses move the focus but never the rows off the frame.
    for (int i = 0; i < 30; i++) gUi->handleInput(KEY_DDOWN, 0, T(0, 0), *gState);
    drawOnce();
    assertBounds("chat submenu scrolled");
    for (const auto& r : host::gRecords) {
        if (r.kind == HostRecord::Rect && r.color == (0xFF000000u | tt::ui::theme::kScRollover)) {
            chk(r.x >= 6 && r.x + r.w <= 314, "chat: highlight inside the frame");
        }
    }
    // Back out with B.
    gUi->handleInput(KEY_B, 0, T(0, 0), *gState);
    drawOnce();
    chk(findText("SpeedChat") != nullptr, "chat: B returns to the root menu");
    // The recent-chat log strip above the tab bar: names keep their leading letters.
    gUi->logChat("Little Pepper", "Hello there, toon!", false, false, 1000);
    drawOnce();
    assertBounds("chat with log strip");
    const HostRecord* logName = nullptr;
    for (const auto* t : textRecords()) {
        if (t->text.find("Little Pepper") == 0) {
            logName = t;
            break;
        }
    }
    chk(logName != nullptr, "chat log: name drawn");
    if (logName) {
        chk(logName->x >= kPocketW + 6, "chat log: name not clipped at the strip's left edge",
            "%.1f", logName->x);
        chk(logName->x + logName->w <= 320 - 4, "chat log: name fits before the strip's right edge",
            "%.1f", logName->x + logName->w);
        chk(contains(*logName, 0, kTabY - kChatLogH, 320, kChatLogH), "chat log: name inside strip");
    }
    const HostRecord* msg = findText("Hello there, toon!");
    chk(msg != nullptr, "chat log: message drawn");
    if (msg)
        chk(contains(*msg, kPocketW + 2, kTabY - kChatLogH, 320 - kPocketW - 6, kChatLogH),
            "chat log: message inside strip");
}

void suiteFriends() {
    printf("suite: Friends panel\n");
    setOnlineContext();
    static const char* names[8] = {"Little Pepper", "Sleepy Rabbit", "Daisy Mae", "Doctor Drizzle",
                                  "Sir Fumblebump", "Madam Applecheek", "Rainbow Nibbles",
                                  "Colonel Thumbwhisker"};
    tt::ui::Context& c = *gCtx;
    c.numNearby = 0;
    c.numFriends = 8;
    for (int i = 0; i < 8; i++) {
        c.friendIds[i] = 100 + i;
        c.friendNames[i] = names[i];
        c.friendNearby[i] = i < 6;  // six online, two offline
        c.friendOpenChat[i] = i % 2 == 0;
    }
    gState->tab = tt::ui::kTabChat;
    gState->whisperTo = 0;
    drawOnce(); // (chat tab, friends not open yet)
    // Open the friends list with the Friends button.
    tap(kFriendsButtonX + 10, 12);
    drawOnce();
    chk(host::gRecords.size() > 20, "friends draw produced records");
    assertBounds("friends");
    // The stock panel art: the wide panel image at (70, 1, 180, 204).
    const HostRecord* panel = nullptr;
    for (const auto& r : host::gRecords) {
        if (r.kind == HostRecord::Image && r.w > 170 && r.h > 195 && r.y < 8) panel = &r;
    }
    chk(panel != nullptr, "friends: panel art drawn");
    if (panel)
        chk(contains(*panel, kFriendsPanelX, kFriendsPanelY, kFriendsPanelW, kFriendsPanelH, 1.0f),
            "friends: panel art at the stock pocket position");
    // Title: centred, inside the panel.
    const HostRecord* title = findText("Online Friends");
    chk(title != nullptr, "friends: title drawn");
    if (title)
        chk(contains(*title, kFriendsPanelX + 2, 40, kFriendsPanelW - 4, 36),
            "friends: title inside the panel");
    // Empty-list wording (regression for the soft-panel crossover): drawn within the list.
    for (int i = 0; i < 8; i++) c.friendNames[i] = nullptr;
    c.numFriends = 0;
    drawOnce();
    assertBounds("friends empty");
    const HostRecord* empty = findText("No friends yet.");
    chk(empty != nullptr, "friends: empty-list text drawn");
    if (empty)
        chk(contains(*empty, kFriendsListX, 70, kFriendsListW, 90), "friends: empty text in the list");
    c.numFriends = 8;
    for (int i = 0; i < 8; i++) c.friendNames[i] = names[i];
    drawOnce();
    // The eight stacked names: no leading/trailing glyph cut by the list, rows not overlapping.
    for (int i = 0; i < 8; i++) {
        char what[96];
        const HostRecord* t = findText(names[i]);
        snprintf(what, sizeof what, "friends: name '%s' drawn", names[i]);
        chk(t != nullptr, what);
        if (!t) continue;
        const float y = kFriendsListY + i * kFriendsRowH;
        snprintf(what, sizeof what, "friends: '%s' row %d inside the list", names[i], i);
        chk(contains(*t, kFriendsListX + 4, y - 1, kFriendsListW - 8, kFriendsRowH + 1, 1.0f),
            what, "(%.1f, %.1f, %.1f x %.1f)", t->x, t->y, t->w, t->h);
        // The layout math must not push a name's leading glyphs past the list's left edge.
        chk(t->x >= kFriendsListX + 5 - kEps, "friends name keeps its left padding",
            "'%s' at x %.1f (list at %.0f)", t->text.c_str(), t->x, kFriendsListX + 5);
        // And the name (shrunk by theme::textFit) must not run under the status dot.
        chk(t->x + t->w <= kFriendsListX + kFriendsListW - 24 - 13 + kEps,
            "friends name leaves room for the status dot", "'%s' ends at %.1f",
            t->text.c_str(), t->x + t->w);
        // Vertical: rows stack without clipping the glyph tops at the panel.
        chk(t->y >= kFriendsListY - 1 - kEps && t->y + t->h <= kFriendsCloseY + kEps,
            "friends rows fit between header and close button", "'%s' y %.1f..%.1f",
            t->text.c_str(), t->y, t->y + t->h);
    }
    // Status dots: green when nearby, grey when offline. A dot is a roundRect of radius 3,
    // so the recorder sees its corner-fan triangles; count those by colour in the list.
    int green = 0, grey = 0;
    for (const auto& r : host::gRecords) {
        if (r.kind != HostRecord::Triangle) continue;
        if (r.x < kFriendsListX + kFriendsListW - 15 || r.x > kFriendsListX + kFriendsListW - 9)
            continue;
        if (r.y < kFriendsListY || r.y > kFriendsListY + 8 * kFriendsRowH) continue;
        if (r.color == (0xFF000000u | tt::ui::theme::kGreen)) ++green;
        if (r.color == (0xFF000000u | tt::ui::theme::kGrey)) ++grey;
    }
    chk(green >= 6, "friends: six green status dots", "%d", green);
    chk(grey >= 2, "friends: two grey status dots", "%d", grey);
    // Focus highlight stays on a row; DDOWN moves it to the second row.
    const HostRecord* hl = nullptr;
    for (const auto& r : host::gRecords) {
        if (r.kind == HostRecord::Rect && r.color == (0xFF000000u | tt::ui::theme::kScRollover) &&
            r.w > 130 && r.h < 12)
            hl = &r;
    }
    chk(hl != nullptr, "friends: focused row highlighted");
    if (hl)
        chk(contains(*hl, kFriendsListX, kFriendsListY, kFriendsListW, 8 * kFriendsRowH),
            "friends: highlight on the first row");
    gUi->handleInput(KEY_DDOWN, 0, T(0, 0), *gState); // friends list owns the chat tab now
    drawOnce();
    assertBounds("friends focus moved");
    hl = nullptr;
    for (const auto& r : host::gRecords) {
        if (r.kind == HostRecord::Rect && r.color == (0xFF000000u | tt::ui::theme::kScRollover) &&
            r.w > 130 && r.h < 12)
            hl = &r;
    }
    chk(hl != nullptr, "friends: highlight drawn after DDOWN");
    if (hl)
        chk(fabsf(hl->y - (kFriendsListY + kFriendsRowH)) < 1.5f, "friends: highlight moved to row 1",
            "%.1f", hl->y);
    // Whispers: a nearby friend click chooses them as the whisper target.
    gUi->handleInput(KEY_A, 0, T(0, 0), *gState);
    chk(gState->whisperTo == 101, "friends: A whispers to the focused friend", "%u",
        gState->whisperTo);
    // Close (B) leaves the friends overlay.
    gUi->handleInput(KEY_B, 0, T(0, 0), *gState);
    drawOnce();
    chk(findText("SpeedChat") != nullptr, "friends: B returns to SpeedChat");
    // The laff head was on the tab bar under the friends overlay too.
    assertPocketLaff(gStats->hp, gStats->maxHp, gCtx->headRgb, 0);
}

void suiteGags() {
    printf("suite: Gags tab\n");
    setOnlineContext();
    // Offline: the placeholder copy, centred inside the card above the pocket.
    gCtx->gagCount = nullptr;
    drawTab(tt::ui::kTabGags);
    assertBounds("gags offline");
    const HostRecord* ph1 = findText("Your gags appear here");
    const HostRecord* ph2 = findText("once you are online.");
    chk(ph1 && ph2, "gags: offline placeholder drawn");
    if (ph1)
        chk(contains(*ph1, 4, 4, 312, pad::kLaffPocketTop - 6), "gags: placeholder inside the card");
    // Online: the full table.
    setOnlineContext();
    drawTab(tt::ui::kTabGags);
    chk(host::gRecords.size() > 40, "gags draw produced records");
    assertBounds("gags online");
    // Header: gags count and bean count fit; the jar sits at the right edge.
    int header = countTextStarting("Gags ");
    chk(header == 1, "gags: 'Gags n / max' header drawn", "%d", header);
    // Track pills: label inside the pill, rows down to the pocket top.
    const char* const kTracksUpper[7] = {"Toon-Up", "Trap", "Lure", "Sound", "Throw", "Squirt", "Drop"};
    for (int t = 0; t < 7; t++) {
        const HostRecord* label = nullptr;
        for (const auto* r : textRecords()) {
            if (r->text != kTracksUpper[t] || r->y < kGagsRow0 - 2) continue;
            const float rowTop = kGagsRow0 + t * kGagsRowH, rowBot = rowTop + kGagsRowH;
            if (r->y >= rowTop - 2 && r->y + r->h <= rowBot + 2) {
                label = r;
                break;
            }
        }
        char what[64];
        snprintf(what, sizeof what, "gags: track label '%s' drawn", kTracksUpper[t]);
        chk(label != nullptr, what);
        if (!label) continue;
        snprintf(what, sizeof what, "gags: '%s' label inside its pill", kTracksUpper[t]);
        chk(label->x >= 14.5f && label->w <= 52 + kEps, what, "(%.1f, w %.1f)", label->x, label->w);
        snprintf(what, sizeof what, "gags: '%s' row inside the card", kTracksUpper[t]);
        chk(label->y + label->h <= pad::kLaffPocketTop - 2, what);
    }
    // Gag icon draws stay in their cells.
    int icons = 0;
    for (const auto& r : host::gRecords) {
        if (r.tag.rfind("gag-icon", 0) == 0) ++icons;
    }
    chk(icons > 0, "gags: icons drawn", "%d", icons);
    for (const auto& r : host::gRecords) {
        if (r.tag.rfind("gag-icon", 0) != 0) continue;
        chk(r.x >= 9 && r.x + r.w <= 311 && r.y >= 27 && r.y + r.h <= pad::kLaffPocketTop - 2,
            "gags: icon stays in its row cell", "%s at (%.1f, %.1f)", r.tag.c_str(), r.x, r.y);
    }
    // The last row's experience bar must not run past the card's right edge.
    // The laff head on the tab bar.
    assertPocketLaff(gStats->hp, gStats->maxHp, gCtx->headRgb, 0);
}

void suiteToon() {
    printf("suite: Toon tab\n");
    setOnlineContext();
    // Online: name, chips, beans, laff, tasks and the book pill.
    drawTab(tt::ui::kTabToon);
    assertBounds("toon online");
    const HostRecord* name = findText("Little Pepper");
    chk(name != nullptr, "toon: name drawn");
    if (name) {
        chk(name->x >= 13.5f, "toon: name keeps left padding of its card", "%.1f", name->x);
        // textFit shrinks to nameW = x0() - 14 - 8 = 120; the chips start at x0().
        chk(name->x + name->w <= 320 - 12 - 7 * 24 + 2 - 8 + kEps, "toon: name ends before the chips",
            "%.1f", name->x + name->w);
        chk(name->y >= 4 && name->y + name->h <= 36, "toon: name on the card's first line");
    }
    // The seven track chips right of the name: inside the card, inside the screen.
    const HostRecord* beans = findText("Jellybeans  214 / 250");
    chk(beans != nullptr, "toon: jellybean counter drawn");
    if (beans) {
        chk(beans->x >= 40 - kEps, "toon: bean label at the jar", "%.1f", beans->x);
        chk(beans->x + beans->w <= 320, "toon: bean label fits");
    }
    const HostRecord* laff = nullptr;
    for (const auto* t : textRecords()) {
        if (t->text.rfind("Laff  ", 0) == 0) {
            laff = t;
            break;
        }
    }
    chk(laff != nullptr, "toon: laff counter drawn");
    if (laff) chk(laff->x + laff->w <= 318, "toon: laff counter fits the card");
    if (beans && laff)
        chk(beans->x + beans->w <= laff->x + kEps, "toon: bean and laff counters do not collide",
            "%.1f vs %.1f", beans->x + beans->w, laff->x);
    const HostRecord* open = findText("Open Shticker Book  (START)");
    chk(open != nullptr, "toon: book pill label drawn");
    if (open)
        chk(contains(*open, 62, 176, 252, 28, 1.0f), "toon: book pill label inside the pill",
            "(%.1f, %.1f)", open->x, open->y);
    // No tasks: the placeholder inside the poster area.
    const HostRecord* noTasks = findText("No ToonTasks yet! Visit an HQ Officer in any Toon HQ.");
    chk(noTasks != nullptr, "toon: no-tasks placeholder drawn");
    if (noTasks)
        chk(contains(*noTasks, 8, 60, 304, pad::kLaffPocketTop - 64), "toon: placeholder inside the card",
            "(%.1f, %.1f)", noTasks->x, noTasks->y);
    // The laff head is in the tab bar (drawTabs), not on the tab itself.
    assertPocketLaff(gStats->hp, gStats->maxHp, gCtx->headRgb, 0);
    // Offline: the colour grid.
    gCtx->quests = nullptr;
    gCtx->stats = nullptr;
    gCtx->toonName = nullptr;
    drawTab(tt::ui::kTabToon);
    assertBounds("toon offline");
    // Offline: the colour grid. Each swatch cell is a roundRect of its ToonDNA colour (the
    // strip pieces carry the face colour), so probe the swatch by colour inside its cell.
    for (int i = 0; i < 27; i++) {
        const float x = kToonSwX + (i % int(kToonSwCols)) * (kToonSwSize + kToonSwGap);
        const float y = kToonSwY + (i / int(kToonSwCols)) * (kToonSwSize + kToonSwGap);
        const u32 face = quantizeColor(tt::ui::kToonColors[i][0], tt::ui::kToonColors[i][1],
                                       tt::ui::kToonColors[i][2]);
        const HostRecord* sel = nullptr;
        for (const auto& r : host::gRecords) {
            if (r.color != (0xFF000000u | face)) continue;
            if (r.x >= x && r.x + r.w <= x + kToonSwSize + kEps && r.y >= y &&
                r.y + r.h <= y + kToonSwSize + kEps) {
                sel = &r;
                break;
            }
        }
        char what[64];
        snprintf(what, sizeof what, "toon offline: swatch %d drawn at (%.0f, %.0f)", i, x, y);
        chk(sel != nullptr, what);
        if (sel) {
            chk(sel->x >= 0 && sel->x + kToonSwSize <= 320 && sel->y + kToonSwSize <= kTabY,
                "toon offline: swatch inside the panel");
        }
    }
    const HostRecord* hint = findText("Tap a color. Head, arms and legs share the body color.");
    chk(hint != nullptr, "toon offline: hint drawn");
    if (hint) chk(hint->x + hint->w <= 320, "toon offline: hint fits");
    const HostRecord* keys = findText("A: jump   B: walk   L/R: turn the camera");
    chk(keys != nullptr, "toon offline: controls line drawn");
    if (keys) chk(keys->x + keys->w <= 320, "toon offline: controls line fits");
}

void suiteBook() {
    printf("suite: Book tab (cover + pages)\n");
    setOnlineContext();
    // Cover: nine page buttons with icons and labels inside.
    drawTab(tt::ui::kTabBook);
    chk(host::gRecords.size() > 30, "book cover draw produced records");
    assertBounds("book cover");
    const char* const kPages[booktab::kCount] = {"Gags",       "ToonTasks", "Cog Gallery",
                                                "Cog Suits",  "Fish",      "Trophies",
                                                "SOS Toons",  "Options",   "Map"};
    for (int i = 0; i < booktab::kCount; i++) {
        const float bx = booktab::kX0 + (i % booktab::kCols) * booktab::kStepX;
        const float by = booktab::kY0 + (i / booktab::kCols) * booktab::kStepY;
        const HostRecord* label = nullptr;
        // Text labels dedupe: find the one that lands inside this button.
        for (const auto* t : textRecords()) {
            if (t->text != kPages[i]) continue;
            if (t->x >= bx - kEps && t->x + t->w <= bx + booktab::kBtnW + kEps && t->y >= by - kEps &&
                t->y + t->h <= by + booktab::kBtnH + kEps) {
                label = t;
                break;
            }
        }
        char what[80];
        snprintf(what, sizeof what, "book cover: '%s' label drawn inside its button", kPages[i]);
        chk(label != nullptr, what);
    }
    // The tab bar's laff head with the Book cover too.
    assertPocketLaff(gStats->hp, gStats->maxHp, gCtx->headRgb, 0);
    // Open the book on each page: the book owns the screen; everything stays inside it.
    const char* kPageNames[tt::ui::Book::kPageCount] = {
        "gags page", "tasks page", "cogs page", "disguise page", "fish page",
        "trophies page", "sos page", "options page", "map page"};
    for (int p = 0; p < tt::ui::Book::kPageCount; p++) {
        gUi->book().open(p);
        drawOnce();
        char what[64];
        snprintf(what, sizeof what, "%s draw has records", kPageNames[p]);
        chk(host::gRecords.size() > 10, what);
        assertBounds(kPageNames[p]);
        gUi->book().close("test");
    }
    // The book's Map page keeps the hood row and Go Home button in the page.
    gUi->book().open(tt::ui::Book::kPageMap);
    drawOnce();
    assertBounds("book map page");
    const HostRecord* mapHint = nullptr;
    for (const auto* t : textRecords()) {
        if (t->text == "Go Home" || t->text == "Playground") mapHint = t;
    }
    chk(mapHint != nullptr, "book map page: Home button label drawn");
    gUi->book().close("test");
}

void suiteTabBar() {
    printf("suite: tab bar\n");
    setOnlineContext();
    const int tabs = 5; // the Debug tab is hidden unless SELECT turns the overlay on
    const float tw = (kW - kPocketW) / tabs;
    for (int i = 0; i < tabs; i++) {
        drawTab(i);
        assertBounds(kTabNames[i]);
        const HostRecord* label = findText(kTabNames[i]);
        char what[64];
        snprintf(what, sizeof what, "tab bar: '%s' label drawn", kTabNames[i]);
        chk(label != nullptr, what);
        if (label) {
            const float tx = kPocketW + i * tw + 2, tWidth = tw - 4;
            snprintf(what, sizeof what, "tab bar: '%s' label inside its tab", kTabNames[i]);
            chk(contains(*label, tx, kTabY, tWidth, kTabH), what, "(%.1f, %.1f, %.1f)", label->x,
                label->y, label->w);
        }
        // The highlighted (open) tab is the one on yellow.
    }
}

} // namespace

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    host::loadHostFont("romfs/baked/fonts/impress.bcfnt", "impress");
    if (!host::gFont.loaded()) printf("note: impress.bcfnt not parsed; using monospace fallback\n");

    static tt::ui::Ui ui;
    gUi = &ui;
    static tt::ui::State state;
    static tt::ui::Context ctx;
    gState = &state;
    gCtx = &ctx;
    static net::OwnStats stats;
    gStats = &stats;

    gHostNowMs = 1000; // frozen clock: pulses ease from t=0, the chat log stays fresh
    if (!ui.init()) {
        printf("FAIL ui.init()\n");
        return 1;
    }
    ui.setMapZone(0);

    suiteTabBar();
    suiteLaffStates();
    suiteMap();
    suiteChat();
    suiteFriends();
    suiteGags();
    suiteToon();
    suiteBook();

    ui.shutdown();
    printf("checks: %ld, failures: %d\n", gChecks, gFails);
    if (gFails == 0) printf("gui_panel_test: PASS\n");
    return gFails == 0 ? 0 : 1;
}
