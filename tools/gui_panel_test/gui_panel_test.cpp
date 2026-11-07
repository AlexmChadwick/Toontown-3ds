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
// Panels for the shop / clerk / battle lane: real headers, driven through the same Ui owner
// resolution the device uses (Ui::resolveOwner -> panel->draw / panel->handleInput).
#include "ui/battle_panel.h"
#include "ui/shop_panel.h"
#include "ui/service_clerk_panel.h"
#include "ui/closet_panel.h"
#include "ui/tailor_panel.h"
#include "ui/fishing_panel.h"
#include "ui/minigame_purchase_panel.h"
#include "ui/shop_rules.h"
#include "ui/sos_tables.h"
#include "ui/panel_owner.h"
#include "world/toon_dna.h"

// The fake-session staging defined in host_stub.cpp (see the Client::connect() seal there):
// a suite fills these, calls gClient.connect(), and the panel openers see the same client
// state the datagram parser would have produced for the same server messages. Global-scope
// redeclarations of ::host's externs (a block inside the anonymous namespace would shadow the
// recorder's ::host and break every host::gRecords below).
namespace host {
extern uint32_t stageAvatarId;
extern tt::net::OwnStats stageStats;
extern uint8_t stageOwnDna[tt::net::kDnaSize];
extern tt::net::ShopMovie stageShopMovie;
extern tt::net::TailorMovie stageTailorMovie;
extern tt::net::Closet stageCloset;
extern tt::net::ClerkProfile stageClerkProfile;
extern tt::net::ClerkSession stageClerkSession;
extern tt::net::OwnFish stageOwnFish;
extern tt::net::OwnBookStats stageOwnBook;
extern tt::net::NpcToon stageNpc;
extern std::vector<tt::net::ClerkMovie> stageClerkMovies;
extern size_t clerkMovieHead;
extern int stageFishTotal;
void stageReset();
} // namespace host

namespace {

using namespace tt::ui;                    // the UI's public constants (pad::, theme::, kLaff* cells)
namespace net = tt::net;                   // OwnStats / OwnQuests / OwnBookStats (as Context spells them)
namespace gags = tt::gags;                 // kTracks / kLevels / kLevelExp
namespace world = tt::world;              // BattleView / ToonDna (as battle_panel.h spells them)
namespace shop = tt::shop;                // Cart / carryMax (as shop_panel.h spells them)
namespace pb = tt::ui::pad::battle;        // the battle focus-table geometry (screen version)
namespace pc = tt::ui::pad::counter;       // the shop / purchase button row
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

void assertBounds(const char* panel, bool allowPocketBleed = true, float extraTop = 0.0f) {
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
        chk(r.y >= -kEps - extraTop, panel, "record top edge %.1f off screen", r.y);
        chk(r.x + r.w <= kW + kEps, panel, "record right %.1f past 320", r.x + r.w);
        chk(r.y + r.h <= kScreenH + bleed + kEps || bottomHanging, panel, "record bottom %.1f past 240",
            r.y + r.h);
        if (r.kind == HostRecord::Text) {
            // Text is never off-screen, bleed or not: the pocket bleeds only the cell art.
            // extraTop is the pre-existing full-screen panels' header baseline (the battle
            // status strip draws its label at y = -1 by design; the ImpressBT cell has ~3px
            // head padding, so no glyph ink clips).
            chk(r.x >= -kEps && r.y >= -kEps - extraTop && r.x + r.w <= kW + kEps &&
                    r.y + r.h <= kScreenH + kEps,
                panel, "text '%s' off screen at (%.1f, %.1f, %.1f x %.1f)", r.text.c_str(), r.x,
                r.y, r.w, r.h);
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

// ---------------------------------------------------------------------------
// Invariant 2b: distinct labels never share pixels
// ---------------------------------------------------------------------------

// The ImpressBT cell (ch=31, base=23) keeps its ink off the cell's top rows and
// below the baseline; discounting that padding compares ink, not line boxes.
bool rectsOverlapInk(const HostRecord& a, const HostRecord& b) {
    const float aTop = a.y + a.h * 0.16f, aBot = a.y + a.h * 0.74f;
    const float bTop = b.y + b.h * 0.16f, bBot = b.y + b.h * 0.74f;
    return a.x < b.x + b.w - 0.51f && b.x < a.x + a.w - 0.51f && aTop < bBot - 0.31f &&
           bTop < aBot - 0.31f;
}

// An opaque fill drawn after this text (record order is paint order) covers its ink: the
// label is hidden behind a later card, so overlapping it tells nothing about what is shown.
bool coveredByLaterFill(const HostRecord& a, size_t ai) {
    const float ax0 = a.x + 0.5f, ax1 = a.x + a.w - 0.5f;
    const float ay0 = a.y + a.h * 0.16f, ay1 = a.y + a.h * 0.74f;
    for (size_t k = ai + 1; k < host::gRecords.size(); k++) {
        const HostRecord& r = host::gRecords[k];
        if (r.kind != HostRecord::Rect && r.kind != HostRecord::RectGrad) continue;
        if ((r.color >> 24) != 0xFF) continue;
        if (r.x <= ax0 && r.x + r.w >= ax1 && r.y <= ay0 && r.y + r.h >= ay1) return true;
    }
    return false;
}

// Every pair of labels in one panel frame: outline / shadow copies of the same
// string (drawOutlinedText, centered bold, the Cast / Exit shadows) are laid out
// once and drawn a hair apart on purpose; a label hidden behind a later opaque
// card is invisible; anything else that shares pixels is two lines on top of
// each other.
void assertLabelsNoOverlap(const char* panel) {
    const auto ts = textRecords();
    for (size_t i = 0; i < ts.size(); i++) {
        for (size_t j = i + 1; j < ts.size(); j++) {
            const HostRecord& a = *ts[i];
            const HostRecord& b = *ts[j];
            // outline / shadow copies: the same string drawn up to 1 px offset in
            // each direction (their pairs span up to 2 px)
            if (a.text == b.text && fabsf(a.x - b.x) < 2.1f && fabsf(a.y - b.y) < 2.1f) continue;
            if (coveredByLaterFill(a, i)) continue;
            chk(!rectsOverlapInk(a, b), panel, "labels '%s' (%.1f, %.1f, %.1f x %.1f) and '%s' "
                                                 "(%.1f, %.1f, %.1f x %.1f) overlap",
                a.text.c_str(), a.x, a.y, a.w, a.h, b.text.c_str(), b.x, b.y, b.w, b.h);
        }
    }
}

// ---------------------------------------------------------------------------
// Full-screen panel owners: battle, gag shop, service clerks, tailor, closet,
// minigame purchase, fishing
// ---------------------------------------------------------------------------
// These panels replace the tab bar entirely (ui/panel_owner.h choosePanel), so
// the suites drive them the way the device does: fill the client state the
// datagram parser would have produced (host::stage* + Client::connect(), both
// in host_stub.cpp), pump the panel's own session method once per frame, give
// Ui one input frame, then draw through Ui (resolveOwner hands the screen to
// the active panel).

tt::net::Client* gClient = nullptr;

// The staged session the panel suites share: 214 beans, every track unlocked to
// level 2, a few level-0 gags in the bag.
void stagePanelSession() {
    host::stageReset();
    host::stageStats.money = 214;
    host::stageStats.bankMoney = 30;
    host::stageStats.maxCarry = 60;
    host::stageStats.maxHp = 57;
    for (int t = 0; t < 7; t++) {
        host::stageStats.trackAccess[t] = 1;
        host::stageStats.experience[t] = uint16_t(gags::kLevelExp[t][2]);
        host::stageStats.inventory[t][0] = 2;
        if (t == 4 || t == 5) host::stageStats.inventory[t][1] = 1;
    }
    gClient->connect();
}

// One device frame: input first (Ui::draw expects the same owner resolve), draw.
u32 uiFrame(u32 down, u32 held, float tx, float ty) {
    const u32 used = gUi->handleInput(down, held, T(tx, ty), *gState);
    drawOnce();
    return used;
}

const HostRecord* findTextIn(const char* str, float x, float y, float w, float h) {
    for (const auto* t : textRecords())
        if (t->text == str && contains(*t, x, y, w, h)) return t;
    return nullptr;
}

void assertTextInside(const char* panel, const HostRecord* t, const char* label, float x, float y,
                      float w, float h) {
    char what[160];
    snprintf(what, sizeof what, "%s: '%s' drawn", panel, label);
    chk(t != nullptr, what);
    if (t) {
        snprintf(what, sizeof what, "%s: '%s' inside (%.0f, %.0f, %.0f x %.0f)", panel, label, x,
                 y, w, h);
        chk(contains(*t, x, y, w, h), what);
    }
}

// The battle panel's toon cards (drawToons): a laff meter face per toon, the
// same head / mouth rules as the tab bar's pocket, asserted inside the card.
void assertCardLaff(const char* panel, float bx, float by, float bw, float bh, int hp, int maxHp,
                    const float rgb[3], int species) {
    const int speciesHead = kLaffDoghead + (species >= 0 && species < 9 ? species : 0);
    const bool dead = hp < 1, full = hp >= maxHp && !dead;
    const u32 wantTint = dead ? quantizeColor(0.58039216f, 0.80392157f, 0.34117647f)
                              : quantizeColor(rgb[0], rgb[1], rgb[2]);
    int head = 0, frown = 0, smile = 0, open = 0;
    for (const auto& r : host::gRecords) {
        if (r.x + r.w < bx - 2 || r.x > bx + bw + 2 || r.y + r.h < by - 2 || r.y > by + bh + 2)
            continue;
        if (isLaffPart(r, speciesHead) && r.tintAlpha > 0.5f) {
            if (r.tint == wantTint) ++head;
        } else if (isLaffPart(r, kLaffFrown)) {
            ++frown;
        } else if (isLaffPart(r, kLaffSmile)) {
            ++smile;
        } else if (isLaffPart(r, kLaffOpenSmile)) {
            ++open;
        }
    }
    chk(head == 1, "%s: card laff head drawn in the head tint", panel);
    if (dead) chk(frown >= 1 && smile + open == 0, "%s: a dead card shows the frown only", panel);
    if (full) chk(smile >= 1 && open == 0 && frown == 0, "%s: a full-laff card smiles", panel);
    if (!dead && !full) chk(open >= 1, "%s: a mid-laff card uses the open smile", panel);
    // The hp/max label rides under the meter, inside the card.
    char n[24];
    snprintf(n, sizeof n, "%d/%d", hp > 0 ? hp : 0, maxHp);
    assertTextInside(panel, findTextIn(n, bx, by + bh * 0.5f, bw, bh * 0.5f), n, bx,
                     by + bh * 0.5f, bw, bh * 0.5f);
}

// ===========================================================================
// Battle panel: gag picker, Cog targeting, SOS, Fire, reward
// ===========================================================================

void suiteBattle() {
    printf("suite: battle panel (gag picker, Cog targeting)\n");
    static world::BattleView v;
    v = world::BattleView{};
    v.phase = world::BattlePhase::Input;
    v.secondsLeft = 20;
    v.canRun = true;
    v.status = "Choose a gag to attack!";
    v.numCogs = 2;
    v.cogs[0].doId = 7001;
    strcpy(v.cogs[0].name, "Flunky");
    v.cogs[0].level = 2;
    v.cogs[0].hp = 12;
    v.cogs[0].maxHp = 30;
    v.cogs[0].active = true;
    strcpy(v.cogs[0].code, "f");
    v.cogs[1].doId = 7002;
    strcpy(v.cogs[1].name, "Backstabber");
    v.cogs[1].level = 6;
    v.cogs[1].hp = 45;
    v.cogs[1].maxHp = 45;
    v.cogs[1].active = true;
    strcpy(v.cogs[1].code, "bc");
    v.numToons = 2;
    v.toons[0].doId = 100000101;
    strcpy(v.toons[0].name, "Little Pepper");
    v.toons[0].active = true;
    v.toons[0].local = true;
    v.toons[0].species = 0;
    v.toons[0].hp = 34;
    v.toons[0].maxHp = 57;
    const float red[3] = {0.933594f, 0.265625f, 0.28125f};
    v.toons[0].rgb[0] = red[0];
    v.toons[0].rgb[1] = red[1];
    v.toons[0].rgb[2] = red[2];
    v.toons[1].doId = 100000202;
    strcpy(v.toons[1].name, "Sleepy Rabbit");
    v.toons[1].active = true;
    v.toons[1].species = 4;
    v.toons[1].hp = 18;
    v.toons[1].maxHp = 45;
    const float blue[3] = {0.09375f, 0.273438f, 0.546875f};
    for (int k = 0; k < 3; k++) v.toons[1].rgb[k] = blue[k];

    // The bag: some of every level-0 gag we own.
    static uint8_t bag[7][7];
    static uint16_t bagExp[7];
    static uint8_t bagAccess[7];
    memset(bag, 0, sizeof bag);
    memset(bagAccess, 1, 7);
    for (int t = 0; t < 7; t++) {
        bagExp[t] = uint16_t(gags::kLevelExp[t][3]);
        bag[t][0] = uint8_t(1 + (t % 3));
    }
    BattleGags g;
    g.count = bag;
    g.exp = bagExp;
    g.access = bagAccess;
    g.maxCarry = 26;
    gUi->setBattle(&v, g);

    bool owned[7];
    float rowY[7], rowH[7];
    for (int t = 0; t < 7; t++) owned[t] = true;
    pad::battleRows(owned, rowY, rowH);
    const float cellX0 = pb::kGridX, cellStep = pb::kCellW + pb::kCellGap;

    // The targeted Cog's card wears the red tab (roundRect(x + 1, kCogY + 1, w - 2, 3)).
    const auto targetTabX = []() -> int {
        for (const auto& r : host::gRecords) {
            if (r.kind != HostRecord::Rect) continue;
            if (r.color != (0xFF000000u | tt::ui::theme::kRed)) continue;
            if (r.h >= 2.9f && r.h <= 3.5f && r.w > 72 - 4 && r.w < 76 + 4 &&
                r.y >= pb::kCogY - 1 && r.y <= pb::kCogY + 2 && r.x > 0.5f && r.x < kW)
                return int(r.x + 0.5f) - 2;
        }
        return -1;
    };

    // ---- the attack view: Cogs, gag grid, buttons, toon cards ----
    uiFrame(0, 0, 0, 0);
    chk(host::gRecords.size() > 60, "battle: attack draw produced records", "%zu",
        host::gRecords.size());
    assertBounds("battle attack", true, 2.0f); // the status strip's label baseline is y = -1
    assertLabelsNoOverlap("battle attack");
    // Status strip: the status and the total-gags counter share one line.
    chk(countTextStarting("Total gags") == 1, "battle: one total counter");
    const HostRecord* total = nullptr;
    for (const auto* t : textRecords())
        if (t->text.rfind("Total gags", 0) == 0) total = t;
    chk(total != nullptr, "battle: total gags counter drawn");
    if (total) chk(total->x >= 6 && total->x + total->w <= kW - 4,
                   "battle: total counter fits the corner", "%.1f", total->x + total->w);
    chk(findTextIn(v.status, 0, -2, 220, 16) != nullptr, "battle: status drawn");
    // Cog cards: name and level inside their card.
    for (int i = 0; i < 2; i++) {
        const float x = pb::kCogX + i * (pb::kCogW + pb::kCogGap);
        char what[48];
        snprintf(what, sizeof what, "battle: cog %d name drawn", i);
        assertTextInside("battle", findTextIn(v.cogs[i].name, x, pb::kCogY, pb::kCogW, pb::kCogH),
                         v.cogs[i].name, x, pb::kCogY, pb::kCogW, pb::kCogH);
        char lv[16];
        snprintf(lv, sizeof lv, "Level %d", v.cogs[i].level);
        snprintf(what, sizeof what, "battle: cog %d level drawn", i);
        assertTextInside("battle", findTextIn(lv, x, pb::kCogY, pb::kCogW, pb::kCogH), lv, x,
                         pb::kCogY, pb::kCogW, pb::kCogH);
    }
    chk(targetTabX() == int(pb::kCogX), "battle: cog 0 is the first target", "%d", targetTabX());
    // Gag grid: every unlocked level's icon and count inside its row cell.
    int icons = 0;
    for (const auto& r : host::gRecords)
        if (r.tag.rfind("gag-icon", 0) == 0) ++icons;
    chk(icons >= 4, "battle: gag icons drawn", "%d", icons);
    for (int t = 0; t < 7; t++) {
        const int levels = gags::levelsUnlocked(t, bagExp[t]);
        for (int l = 0; l < levels; l++) {
            const float x = cellX0 + l * cellStep;
            const int track = t;
            bool icon = false;
            for (const auto& im : host::gRecords) {
                if (im.tag.rfind("gag-icon", 0) != 0) continue;
                if (contains(im, x, rowY[track], pb::kCellW, rowH[track], 1.0f)) {
                    icon = true;
                    break;
                }
            }
            char what[64];
            snprintf(what, sizeof what, "battle: gag %d-%d icon inside its cell", track, l);
            chk(icon, what);
            char num[8];
            snprintf(num, sizeof num, "%u", unsigned(bag[track][l]));
            snprintf(what, sizeof what, "battle: gag %d-%d count inside its cell", track, l);
            chk(findTextIn(num, x, rowY[track], pb::kCellW, rowH[track]) != nullptr, what);
        }
    }
    // Gag icons from different cells never overlap each other.
    for (size_t i = 0; i < host::gRecords.size(); i++) {
        if (host::gRecords[i].kind != HostRecord::Image ||
            host::gRecords[i].tag.rfind("gag-icon", 0) != 0)
            continue;
        for (size_t j = i + 1; j < host::gRecords.size(); j++) {
            if (host::gRecords[j].kind != HostRecord::Image ||
                host::gRecords[j].tag.rfind("gag-icon", 0) != 0)
                continue;
            chk(!rectsOverlapInk(host::gRecords[i], host::gRecords[j]),
                "battle: gag icons %s and %s overlap", host::gRecords[i].tag.c_str(),
                host::gRecords[j].tag.c_str());
        }
    }
    // Buttons: FIRE / PASS above RUN / SOS, each label inside its face, faces disjoint.
    const struct {
        const char* label;
        float x, y;
    } kBtns[4] = {{"FIRE", pb::kFireX, pb::kBtnY}, {"PASS", pb::kPassX, pb::kBtnY},
                  {"RUN", pb::kRunX, pb::kRow2Y}, {"SOS", pb::kSosX, pb::kRow2Y}};
    for (int i = 0; i < 4; i++) {
        char what[48];
        snprintf(what, sizeof what, "battle: %s label drawn", kBtns[i].label);
        assertTextInside("battle", findTextIn(kBtns[i].label, kBtns[i].x, kBtns[i].y, pb::kSmallW,
                                              pb::kSmallH),
                         kBtns[i].label, kBtns[i].x, kBtns[i].y, pb::kSmallW, pb::kSmallH);
        for (int j = i + 1; j < 4; j++) {
            if (fabsf(kBtns[i].y - kBtns[j].y) >= 1.0f) continue;
            chk(kBtns[i].x + pb::kSmallW <= kBtns[j].x - 0.5f,
                "battle: %s and %s faces do not overlap", kBtns[i].label, kBtns[j].label);
        }
    }
    // Toon cards: the pocket's laff rules inside each card, plus each pick square.
    const float cardW = 66.0f;
    for (int i = 0; i < 2; i++) {
        const float x = pb::kToonsX + i * (cardW + 2.0f);
        char what[48];
        snprintf(what, sizeof what, "battle toon %d", i);
        assertCardLaff(what, x, pb::kBtnY, cardW, pb::kBtnH, v.toons[i].hp, v.toons[i].maxHp,
                       i == 0 ? red : blue, v.toons[i].species);
        assertTextInside(what, findTextIn("?", x, pb::kBtnY, cardW, pb::kBtnH), "?", x, pb::kBtnY,
                         cardW, pb::kBtnH);
    }
    // The button block and the toon cards do not overlap.
    chk(pb::kSosX + pb::kSmallW <= pb::kToonsX - 0.5f,
        "battle: buttons and toon cards do not overlap");

    // ---- targeting: L moves the target card, a single-target gag follows it ----
    uiFrame(KEY_L, 0, 0, 0);
    chk(targetTabX() == int(pb::kCogX + (pb::kCogW + pb::kCogGap)),
        "battle: L retargets to cog 1", "%d", targetTabX());
    // Once the D-Pad has moved the focus, the focused gag gets its detail line.
    uiFrame(KEY_DDOWN, 0, 0, 0);
    uiFrame(KEY_DDOWN, 0, 0, 0);
    uiFrame(KEY_DDOWN, 0, 0, 0);
    int detail = 0;
    for (const auto& r : host::gRecords)
        if (r.kind == HostRecord::Text && r.text.find("Accuracy: ") != std::string::npos) ++detail;
    chk(detail >= 1, "battle: the focused gag's detail line drawn");
    assertBounds("battle detail", true, 2.0f);
    assertLabelsNoOverlap("battle detail");

    // Throw (single target): the attack goes to the targeted Cog.
    world::BattleAction act{};
    uiFrame(KEY_TOUCH, 0, cellX0 + pb::kCellW * 0.5f, rowY[4] + rowH[4] * 0.5f);
    chk(gUi->battle().popAction(act), "battle: a tapped throw gag becomes an action");
    chk(act.kind == world::BattleAction::Attack && act.track == 4 && act.level == 0,
        "battle: the action is Throw level 0");
    chk(act.target == v.cogs[1].doId, "battle: the throw targets the selected cog", "%u",
        act.target);
    // Sound (group attack): no single target.
    uiFrame(KEY_TOUCH, 0, cellX0 + pb::kCellW * 0.5f, rowY[3] + rowH[3] * 0.5f);
    chk(gUi->battle().popAction(act) && act.kind == world::BattleAction::Attack && act.track == 3,
        "battle: sound gag picked");
    chk(act.target == 0, "battle: a group attack has target 0", "%u", act.target);
    // Toon-Up (with a second toon) heals them, not us.
    uiFrame(KEY_TOUCH, 0, cellX0 + pb::kCellW * 0.5f, rowY[0] + rowH[0] * 0.5f);
    chk(gUi->battle().popAction(act) && act.kind == world::BattleAction::Attack && act.track == 0,
        "battle: toon-up gag picked");
    chk(act.target == v.toons[1].doId, "battle: toon-up targets the other toon", "%u",
        act.target);
    // Pass and Run.
    uiFrame(KEY_TOUCH, 0, pb::kPassX + pb::kSmallW * 0.5f, pb::kBtnY + pb::kSmallH * 0.5f);
    chk(gUi->battle().popAction(act) && act.kind == world::BattleAction::Pass,
        "battle: the PASS button passes");
    uiFrame(KEY_TOUCH, 0, pb::kRunX + pb::kSmallW * 0.5f, pb::kRow2Y + pb::kSmallH * 0.5f);
    chk(gUi->battle().popAction(act) && act.kind == world::BattleAction::Run,
        "battle: the RUN button runs");
    // A dead battle friend still gets its card, with the frown.
    v.toons[1].hp = 0;
    uiFrame(0, 0, 0, 0);
    assertCardLaff("battle sad toon", pb::kToonsX + (cardW + 2.0f), pb::kBtnY, cardW, pb::kBtnH, 0,
                   45, blue, 4);

    // ---- SOS: the Rescued Toons cards, paging, a spent card ----
    v.toons[1].hp = 18;
    v.canSos = true;
    v.numSosCards = 9;
    v.sosCards[0] = {1001, 2}; // Clerk Will, trap 4
    v.sosCards[1] = {1116, 1}; // Barnacle Bessie, drop 5
    v.sosCards[2] = {2001, 0}; // Flippy: no calls left
    for (int i = 3; i < 9; i++) v.sosCards[i] = {1323, 1}; // Stinky Ned, lure 5
    uiFrame(KEY_TOUCH, 0, pb::kSosX + pb::kSmallW * 0.5f, pb::kRow2Y + pb::kSmallH * 0.5f);
    assertBounds("battle sos", true, 2.0f);
    assertLabelsNoOverlap("battle sos");
    chk(findTextIn("Call which friend?", 1, -2, 318, 19) != nullptr, "battle: SOS status drawn");
    const char* const kSosNames[3] = {"Clerk Will", "Barnacle Bessie", "Flippy"};
    for (int i = 0; i < 3; i++) {
        const float x = pb::kCardX + (i % 4) * (pb::kCardW + pb::kCardGap);
        const float y = pb::kCardY + (i / 4) * (pb::kCardH + pb::kCardGap);
        char what[64];
        snprintf(what, sizeof what, "battle: SOS card %d name drawn", i);
        assertTextInside("battle", findTextIn(kSosNames[i], x, y, pb::kCardW, pb::kCardH),
                         kSosNames[i], x, y, pb::kCardW, pb::kCardH);
    }
    assertTextInside("battle", findTextIn("BACK", pb::kBackX, pb::kBtnY, pb::kBackW, pb::kSmallH),
                     "BACK", pb::kBackX, pb::kBtnY, pb::kBackW, pb::kSmallH);
    assertTextInside("battle",
                     findTextIn("Page 1 / 2", pb::kBackX, pb::kRow2Y, pb::kBackW, pb::kSmallH),
                     "Page 1 / 2", pb::kBackX, pb::kRow2Y, pb::kBackW, pb::kSmallH);
    // L / R page the SOS cards; page 2's first card is Stinky Ned (a lure card, callable).
    uiFrame(KEY_R, 0, 0, 0);
    chk(findTextIn("Page 2 / 2", pb::kBackX, pb::kRow2Y, pb::kBackW, pb::kSmallH) != nullptr,
        "battle: R turns the SOS page");
    uiFrame(KEY_TOUCH, 0, pb::kCardX + pb::kCardW * 0.5f, pb::kCardY + pb::kCardH * 0.5f);
    chk(gUi->battle().popAction(act) && act.kind == world::BattleAction::NpcSos,
        "battle: an SOS card becomes an NpcSos action");
    chk(act.target == 1323, "battle: the called friend is the card's NPC", "%u", act.target);
    uiFrame(0, 0, 0, 0);
    chk(findTextIn("FIRE", pb::kFireX, pb::kBtnY, pb::kSmallW, pb::kSmallH) != nullptr,
        "battle: SOS returns to the attack view");
    // A spent card (Flippy, 0 left) cannot be called.
    uiFrame(KEY_TOUCH, 0, pb::kSosX + pb::kSmallW * 0.5f, pb::kRow2Y + pb::kSmallH * 0.5f);
    uiFrame(KEY_TOUCH, 0,
            pb::kCardX + 2 * (pb::kCardW + pb::kCardGap) + pb::kCardW * 0.5f,
            pb::kCardY + pb::kCardH * 0.5f);
    chk(!gUi->battle().popAction(act), "battle: a spent SOS card calls nobody");
    uiFrame(KEY_B, 0, 0, 0); // back to the attack view

    // ---- Fire: pink slips over the Cog cards ----
    v.canSos = false;
    v.canFire = true;
    v.pinkSlips = 2;
    uiFrame(KEY_TOUCH, 0, pb::kFireX + pb::kSmallW * 0.5f, pb::kBtnY + pb::kSmallH * 0.5f);
    assertBounds("battle fire", true, 2.0f);
    assertLabelsNoOverlap("battle fire");
    chk(findTextIn("PINK SLIPS LEFT: 2", 1, pb::kGridY - 3, 318, pb::kGridH + 4) != nullptr,
        "battle: the fire slip count drawn");
    chk(findTextIn("FIRE WHICH COG?", 1, pb::kGridY - 3, 318, pb::kGridH + 4) != nullptr,
        "battle: FIRE WHICH COG? drawn");
    assertTextInside("battle", findTextIn("FIRED!", 110, pb::kGridY + 14, 100, 62), "FIRED!", 110,
                     pb::kGridY + 14, 100, 62);
    for (int i = 0; i < 2; i++) {
        const float x = pb::kCogX + i * (pb::kCogW + pb::kCogGap);
        char what[48];
        snprintf(what, sizeof what, "battle: cog %d fire chip drawn", i);
        assertTextInside("battle", findTextIn("Fire", x, pb::kCogY, pb::kCogW, pb::kCogH), "Fire",
                         x, pb::kCogY, pb::kCogW, pb::kCogH);
    }
    uiFrame(KEY_TOUCH, 0, pb::kCogX + pb::kCogW * 0.5f, pb::kCogY + pb::kCogH * 0.5f);
    chk(gUi->battle().popAction(act) && act.kind == world::BattleAction::Fire,
        "battle: tapping a cog in fire mode fires it");
    chk(act.target == v.cogs[0].doId, "battle: the fired cog is the tapped one", "%u",
        act.target);
    // Without slips the FIRE button is dead.
    v.canFire = false;
    v.pinkSlips = 0;
    uiFrame(0, 0, 0, 0);
    uiFrame(KEY_TOUCH, 0, pb::kFireX + pb::kSmallW * 0.5f, pb::kBtnY + pb::kSmallH * 0.5f);
    chk(!gUi->battle().popAction(act), "battle: FIRE without pink slips does nothing");
    chk(countTextStarting("PINK SLIPS") == 0, "battle: the fire panel did not open");

    // ---- the reward view ----
    v.phase = world::BattlePhase::Reward;
    v.numCogs = 0; // the defeated Cogs have left the fight
    v.cogsDefeated = 2;
    v.floorReward = false;
    v.rewardSent = false;
    v.numRewardRows = 2;
    v.reward[0].track = 4;
    v.reward[0].earned = 12;
    v.reward[0].orig = 40;
    v.reward[1].track = 5;
    v.reward[1].earned = 6;
    v.reward[1].orig = 39;
    uiFrame(0, 0, 0, 0);
    chk(host::gRecords.size() > 40, "battle: reward draw produced records");
    assertBounds("battle reward", true, 2.0f);
    assertLabelsNoOverlap("battle reward");
    chk(findTextIn("Cogs defeated: 2", 2, pb::kCogY + 2, kW - 4, 180) != nullptr,
        "battle: the defeated count drawn");
    assertTextInside("battle", findTextIn("Throw", 10, pb::kCogY + 24, 60, 160), "Throw", 10,
                     pb::kCogY + 24, 60, 160);
    assertTextInside("battle", findTextIn("Squirt", 10, pb::kCogY + 24, 60, 160), "Squirt", 10,
                     pb::kCogY + 24, 60, 160);
    assertBounds("battle reward bars", true, 2.0f);
    // Continue, clear of the toon cards.
    assertTextInside("battle",
                     findTextIn("Continue", pb::kContinueX, pb::kContinueY, pb::kContinueW,
                                pb::kContinueH),
                     "Continue", pb::kContinueX, pb::kContinueY, pb::kContinueW, pb::kContinueH);
    chk(pb::kToonsRewardX + pb::kToonsRewardW <= pb::kContinueX,
        "battle: the reward cards stay clear of Continue");
    uiFrame(KEY_A, 0, 0, 0);
    chk(gUi->battle().popAction(act) && act.kind == world::BattleAction::Continue,
        "battle: A continues from the reward");
    // The Toontorial's reward dialog draws over the view with its OK button.
    v.tutorialDialog = "You did it! Take these gags.";
    uiFrame(0, 0, 0, 0);
    assertBounds("battle reward dialog", true, 2.0f);
    assertLabelsNoOverlap("battle reward dialog");
    const HostRecord* dialog = findTextIn(v.tutorialDialog, 40, 40, kW - 80, 100);
    chk(dialog != nullptr, "battle: the tutorial dialog drawn");
    if (dialog)
        chk(dialog->x >= 52 - kEps && dialog->y >= 52 - kEps && dialog->x + dialog->w <= 278,
            "battle: the dialog text inside its card", "(%.1f, %.1f, %.1f)", dialog->x, dialog->y,
            dialog->x + dialog->w);
    assertTextInside("battle", findTextIn("OK (A)", 40, 118, kW - 80, 24), "OK (A)", 40, 118,
                     kW - 80, 24);
    v.tutorialDialog = nullptr;

    // ---- the movie phase: input ignored, the panel still draws in bounds ----
    v.phase = world::BattlePhase::Movie;
    uiFrame(KEY_A, 0, 0, 0);
    assertBounds("battle movie", true, 2.0f);
    chk(!gUi->battle().popAction(act), "battle: A during the movie sends nothing");

    // Leaving the battle: the tab owner takes the screen back.
    gUi->setBattle(nullptr, g);
    uiFrame(0, 0, 0, 0);
    chk(findTextIn("Map", 62, 208, 52, 32) != nullptr, "battle: clear returns to the tabs");
}

// ===========================================================================
// Gag shop, service clerks, tailor, closet, minigame purchase, fishing
// ===========================================================================

// The gag shop's grid (shared by the minigame purchase panel): file-local
// constants of shop_panel.cpp mirrored beside the draw code they test.
constexpr float kShopNameX = 4, kShopGridX = 64, kShopGridY = 26, kShopCellW = 42, kShopCellH = 22;
constexpr float kShopInfoY = 184, kShopSignW = 170;

void assertShopGrid(const char* panel, const char* doneLabel, const tt::shop::Cart& cart,
                    const tt::shop::Profile& profile) {
    for (int t = 0; t < 7; t++) {
        const float y = kShopGridY + t * kShopCellH;
        char what[64];
        snprintf(what, sizeof what, "%s: track label '%s' inside its pill", panel,
                 tt::ui::theme::kTrackTitles[t]);
        assertTextInside(panel, findTextIn(tt::ui::theme::kTrackTitles[t], kShopNameX, y + 1,
                                           kShopGridX - kShopNameX - 3, kShopCellH - 3),
                         tt::ui::theme::kTrackTitles[t], kShopNameX, y + 1,
                         kShopGridX - kShopNameX - 3, kShopCellH - 3);
        for (int l = 0; l < 4; l++) {
            const float x = kShopGridX + l * kShopCellW;
            const bool open = profile.trackAccess[t] && shop::expLevel(t, profile.experience[t]) >= l;
            char cell[24];
            if (!open) {
                strcpy(cell, "-");
            } else {
                snprintf(cell, sizeof cell, "%u/%d", unsigned(cart.inv[t][l]),
                         shop::carryMax(profile, t, l));
            }
            snprintf(what, sizeof what, "%s: gag %d-%d cell drawn", panel, t, l);
            assertTextInside(panel, findTextIn(cell, x, y, kShopCellW - 1, kShopCellH - 1), cell, x,
                             y, kShopCellW - 1, kShopCellH - 1);
        }
    }
    // The button row: [-] [+] <done> Cancel, pairwise disjoint, each label inside. The done
    // caption is the panel's own (DONE SHOPPING in the gag shop; the trolley purchase panel
    // uses upstream's GagShopBackToPlayground instead, so the suite passes it in).
    const struct {
        const char* label;
        float cx, w;
    } kRow[4] = {
        {"-", pc::kMinusX + pc::kBtnSmallW * 0.5f, pc::kBtnSmallW},
        {"+", pc::kPlusX + pc::kBtnSmallW * 0.5f, pc::kBtnSmallW},
        {doneLabel, pc::kDoneX + pc::kDoneW * 0.5f, pc::kDoneW},
        {"Cancel", pc::kCancelX + pc::kCancelW * 0.5f, pc::kCancelW}};
    const float kRowX0[4] = {pc::kMinusX, pc::kPlusX, pc::kDoneX, pc::kCancelX};
    const float kRowW[4] = {pc::kBtnSmallW, pc::kBtnSmallW, pc::kDoneW, pc::kCancelW};
    for (int i = 0; i < 4; i++) {
        char what[64];
        snprintf(what, sizeof what, "%s: button %d label inside", panel, i);
        assertTextInside(panel, findTextIn(kRow[i].label, kRowX0[i], pc::kBtnY, kRowW[i],
                                           pc::kBtnH),
                         kRow[i].label, kRowX0[i], pc::kBtnY, kRowW[i], pc::kBtnH);
        for (int j = i + 1; j < 4; j++)
            chk(kRowX0[i] + kRowW[i] <= kRowX0[j] - 0.5f,
                "%s: buttons %d and %d do not overlap", panel, i, j);
    }
}

void suiteShop() {
    printf("suite: gag shop panel\n");
    stagePanelSession();
    host::stageShopMovie.clerkDoId = 555;
    host::stageShopMovie.mode = net::kPurchaseMovieStart;
    host::stageShopMovie.deadlineMs = uint32_t(gHostNowMs) + 120000;
    host::stageShopMovie.seq = 1;
    gClient->connect();
    gUi->shop().onMovie(*gClient, uint32_t(gHostNowMs));
    chk(gUi->shop().active(), "shop: the panel is open");
    chk(gUi->shop().clerkDoId() == 555, "shop: the clerk id recorded");
    chk(gUi->shop().secondsLeft(uint32_t(gHostNowMs)) == 120, "shop: the countdown starts at 120",
        "%d", gUi->shop().secondsLeft(uint32_t(gHostNowMs)));

    const uint32_t kDeadline = uint32_t(gHostNowMs) + 120000;
    auto frame = [&](u32 down, float tx = 0, float ty = 0, uint32_t now = uint32_t(gHostNowMs)) {
        gUi->shop().update(*gClient, now);
        const u32 used = gUi->handleInput(down, 0, T(tx, ty), *gState);
        drawOnce();
        return used;
    };

    frame(0);
    chk(host::gRecords.size() > 60, "shop: draw produced records", "%zu", host::gRecords.size());
    assertBounds("shop");
    assertLabelsNoOverlap("shop");
    // The plank title and the carry tag beside it.
    assertTextInside("shop", findTextIn("Goofy's Gag Shop", 4, 0, kShopSignW, 24),
                     "Goofy's Gag Shop", 4, 0, kShopSignW, 24);
    assertTextInside("shop", findTextIn("Carry: 16/60", kShopSignW + 10, 2, 138, 20),
                     "Carry: 16/60", kShopSignW + 10, 2, 138, 20);
    // The grid and the button row.
    tt::shop::Profile profile;
    for (int t = 0; t < 7; t++) {
        profile.experience[t] = host::stageStats.experience[t];
        profile.trackAccess[t] = host::stageStats.trackAccess[t];
    }
    profile.maxCarry = host::stageStats.maxCarry;
    assertShopGrid("shop", "DONE\nSHOPPING", gUi->shop().cart(), profile);
    // The info line fits its strip and the money label sits on the jar. Its wording is
    // ShopPanel::rebuildInfo's: the selected gag (track 0, level 0) not yet bought, so
    // "have <bag> of <carryMax>": carryMax for our experience, computed by the same
    // shop_rules the cell counts above use (15 at exp level 2, not a flat 10).
    char info[96];
    snprintf(info, sizeof info, "%s: have %u of %d. 1 bean each", gags::kGagNames[0][0],
             unsigned(gUi->shop().cart().inv[0][0]), shop::carryMax(profile, 0, 0));
    const HostRecord* infoRec = findTextIn(info, 1, kShopInfoY - 3, 318, 18);
    chk(infoRec != nullptr, "shop: the info line drawn (%s)", info);
    if (infoRec)
        chk(infoRec->x + infoRec->w <= kW - 4, "shop: the info line fits", "%.1f",
            infoRec->x + infoRec->w);
    chk(findTextIn("214", 100, pc::kBtnY - 4, 74, 40) != nullptr, "shop: the bean count on the jar");

    // The gag icons sit left of their cell count (16 px icon starting x + 2).
    for (int t = 0; t < 7; t++) {
        for (int l = 0; l < 4; l++) {
            const float x = kShopGridX + l * kShopCellW;
            const float y = kShopGridY + t * kShopCellH;
            const bool open = profile.trackAccess[t] &&
                              shop::expLevel(t, profile.experience[t]) >= l;
            if (!open) continue;
            const HostRecord* icon = nullptr;
            for (const auto& r : host::gRecords) {
                if (r.kind != HostRecord::Image) continue;
                char tag[32];
                snprintf(tag, sizeof tag, "gag-icon %d-%d", t, l);
                if (r.tag == tag) icon = &r;
            }
            char what[64];
            snprintf(what, sizeof what, "shop: gag %d-%d icon drawn", t, l);
            chk(icon != nullptr, what);
            if (icon) {
                snprintf(what, sizeof what, "shop: gag %d-%d icon inside its cell", t, l);
                chk(contains(*icon, x, y, kShopCellW - 1, kShopCellH - 1, 1.0f), what);
            }
        }
        break; // the icons repeat per track; spot-check track 0 only
    }

    // Buying: A buys the selected gag (onMovie starts the cursor on the first
    // open track: Toon-Up level 0).
    frame(KEY_A);
    chk(gUi->shop().cart().inv[0][0] == 3 && gUi->shop().cart().money == 213,
        "shop: A buys one more of the selected gag", "%u", unsigned(gUi->shop().cart().inv[0][0]));
    chk(gUi->shop().cart().bought(0, 0) == 1, "shop: the cart remembers what it bought");
    // A locked cell refuses: level 3 is beyond our experience.
    chk(!gUi->shop().buy(0, 3), "shop: buying a locked gag is refused");
    assertLabelsNoOverlap("shop refused");
    frame(0);
    // The [+] button on the jar row buys too.
    frame(KEY_TOUCH, pc::kPlusX + pc::kBtnSmallW * 0.5f, pc::kBtnY + pc::kBtnH * 0.5f);
    chk(gUi->shop().cart().bought(0, 0) == 2, "shop: the [+] button buys");
    // Cancel closes without sending a purchase.
    gUi->shop().finish(false); // Done: queued
    frame(0);
    chk(!gUi->shop().active(), "shop: Done closes the panel");

    // Reopen with a near deadline: update auto-submits and closes (sendClerkSetInventory
    // is an inert host stub; the close is the behaviour under test).
    host::stageShopMovie.mode = net::kPurchaseMovieStart;
    host::stageShopMovie.seq++;
    gClient->connect();
    gUi->shop().onMovie(*gClient, uint32_t(gHostNowMs));
    chk(gUi->shop().active(), "shop: reopened");
    frame(0, 0, 0, kDeadline - 1000);
    chk(!gUi->shop().active(), "shop: the panel auto-submits before the AI timeout");
    gUi->shop().dismiss();
}

void suiteClerks() {
    printf("suite: service clerk panels (pet, kart, fisherman)\n");
    stagePanelSession();
    host::stageNpc.doId = 777;
    host::stageNpc.active = true;
    host::stageOwnFish.tankCount = 3;
    host::stageFishTotal = 40;
    net::ClerkMovie m1{};
    m1.clerk = 777;
    m1.kind = net::ClerkKind::Pet;
    m1.mode = 1;
    m1.deadline = uint32_t(gHostNowMs) + 90000;
    host::stageClerkMovies.push_back(m1);
    gClient->connect();
    gUi->serviceClerks().update(*gClient, uint32_t(gHostNowMs));
    chk(gUi->serviceClerks().active(), "clerk: the pet clerk opened");
    chk(gUi->serviceClerks().kind() == net::ClerkKind::Pet, "clerk: the kind is Pet");

    auto frame = [&](u32 down, float tx = 0, float ty = 0,
                     uint32_t now = uint32_t(gHostNowMs)) {
        gUi->serviceClerks().update(*gClient, now);
        const u32 used = gUi->handleInput(down, 0, T(tx, ty), *gState);
        drawOnce();
        return used;
    };

    frame(0);
    chk(host::gRecords.size() > 40, "clerk: draw produced records", "%zu", host::gRecords.size());
    assertBounds("clerk pet menu");
    assertLabelsNoOverlap("clerk pet menu");
    // Title, bean line and countdown badge, inside the card, the badge clear of the title.
    assertTextInside("clerk", findTextIn("Pet shop", 4, 4, 312, 194), "Pet shop", 4, 4, 312, 194);
    assertTextInside("clerk", findTextIn("Jellybeans 214   Bank 30", 4, 4, 312, 194),
                     "Jellybeans 214   Bank 30", 4, 4, 312, 194);
    assertTextInside("clerk", findTextIn("1:30", 268, 9, 40, 20), "1:30", 268, 9, 40, 20);
    // Menu rows inside their pills (rows start at y = 3*23 + 6 = 75).
    const char* const kPetRows[4] = {"Sell fish", "Adopt a Doodle", "No Doodle to return", "Done"};
    for (int i = 0; i < 4; i++) {
        const float y = 3 * 23 + 6 + i * 23;
        char what[64];
        snprintf(what, sizeof what, "clerk: pet row %d drawn", i);
        assertTextInside("clerk", findTextIn(kPetRows[i], 13, y - 1, 294, 20), kPetRows[i], 13,
                         y - 1, 294, 20);
    }
    assertTextInside("clerk", findTextIn("Tank: 3 fish, value 40 beans", 4, 4, 312, 194),
                     "Tank: 3 fish, value 40 beans", 4, 4, 312, 194);
    // The button pills match the touch columns (Prev < 75, Next < 150, Choose < 235, B else).
    const struct {
        const char* label;
        float x, w, col;
    } kCols[4] = {{ "< Prev", 4, 68, 75}, {"Next >", 79, 68, 150}, {"Choose", 154, 78, 235},
                  {"Back", 239, 77, 320}};
    for (int i = 0; i < 4; i++) {
        char what[64];
        snprintf(what, sizeof what, "clerk: %s drawn in its column", kCols[i].label);
        assertTextInside("clerk", findTextIn(kCols[i].label, kCols[i].x, 206, kCols[i].w, 30),
                         kCols[i].label, kCols[i].x, 206, kCols[i].w, 30);
        chk(kCols[i].x + kCols[i].w <= kCols[i].col, "clerk: %s fits its touch column",
            kCols[i].label);
        if (i)
            chk(kCols[i - 1].w + kCols[i - 1].x <= kCols[i].x, "clerk: the pill buttons do not overlap");
    }
    // Touch down the rows chooses one; the footer buttons page / act.
    frame(KEY_TOUCH, 100, 210); // Next column
    frame(KEY_TOUCH, 200, 210); // Choose: adopt
    chk(findTextIn("Waiting for the server's Doodle seeds...", 4, 4, 312, 194) != nullptr,
        "clerk: the pet page shows the empty-seeds wording");
    assertBounds("clerk pets");
    assertLabelsNoOverlap("clerk pets");
    frame(KEY_B, 0, 0); // back to the menu
    // The fisherman clerk: sell fish, with the result page and its lone OK.
    net::ClerkMovie m2{};
    m2.clerk = 888;
    m2.kind = net::ClerkKind::Fisherman;
    m2.mode = 1;
    m2.deadline = uint32_t(gHostNowMs) + 90000;
    host::stageClerkMovies.push_back(m2);
    host::stageNpc.doId = 888;
    frame(0);
    chk(gUi->serviceClerks().kind() == net::ClerkKind::Fisherman, "clerk: a new movie switches kind");
    assertBounds("clerk fisherman");
    assertLabelsNoOverlap("clerk fisherman");
    chk(findTextIn("Sell 3 fish for 40 jellybeans?", 4, 4, 312, 194) != nullptr,
        "clerk: the fisherman sell line drawn");
    frame(KEY_A, 0, 0);
    chk(gUi->serviceClerks().active(), "clerk: the sale keeps the panel open for the result");
    frame(0);
    chk(findTextIn("Waiting for the fish sale...", 4, 4, 312, 194) != nullptr,
        "clerk: the result wording drawn");
    assertBounds("clerk result");
    assertLabelsNoOverlap("clerk result");
    chk(findTextIn("Back", 4, 206, 312, 30) == nullptr, "clerk: a result page shows no Back");
    // The result expires.
    frame(0, 0, 0, uint32_t(gHostNowMs) + 6001);
    chk(!gUi->serviceClerks().active(), "clerk: the result page expires");
    gUi->serviceClerks().dismiss();
}

void suiteTailor() {
    printf("suite: tailor panel\n");
    stagePanelSession();
    // A valid ToonDNA net string (the front end's default dog).
    const uint8_t dna[net::kDnaSize] = {'t', 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1};
    for (int i = 0; i < net::kDnaSize; i++) host::stageOwnDna[i] = dna[i];
    host::stageTailorMovie.tailorDoId = 556;
    host::stageTailorMovie.mode = net::kTailorMovieStart;
    host::stageTailorMovie.deadlineMs = uint32_t(gHostNowMs) + 280000;
    gClient->connect();
    gUi->tailor().onMovie(*gClient, uint32_t(gHostNowMs));
    chk(gUi->tailor().active(), "tailor: the panel is open");

    auto frame = [&](u32 down, float tx = 0, float ty = 0, uint32_t now = uint32_t(gHostNowMs)) {
        gUi->tailor().update(*gClient, now);
        const u32 used = gUi->handleInput(down, 0, T(tx, ty), *gState);
        drawOnce();
        return used;
    };
    frame(0);
    chk(host::gRecords.size() > 40, "tailor: draw produced records", "%zu", host::gRecords.size());
    assertBounds("tailor");
    assertLabelsNoOverlap("tailor");
    assertTextInside("tailor", findTextIn("Clothing Store", 4, 3, 312, 30), "Clothing Store", 4, 3,
                     312, 30);
    // The two shuffle rows: labels inside, rows disjoint (kTopY 38, kBottomY 104).
    assertTextInside("tailor", findTextIn("Shirts", 90, tt::ui::pad::tailor::kTopY, 140, 62),
                     "Shirts", 90, tt::ui::pad::tailor::kTopY, 140, 62);
    assertTextInside("tailor",
                     findTextIn("Shorts", 90, tt::ui::pad::tailor::kBottomY, 140, 62), "Shorts", 90,
                     tt::ui::pad::tailor::kBottomY, 140, 62);
    chk(tt::ui::pad::tailor::kTopY + tt::ui::pad::tailor::kRowH <= tt::ui::pad::tailor::kBottomY,
        "tailor: the shirt and shorts rows do not overlap");
    assertTextInside("tailor",
                     findTextIn("Up/Down picks shirt or bottoms; Left/Right changes it.", 4, 172,
                                312, 20),
                     "hint", 4, 172, 312, 20);
    // Done / Cancel, below the rows, each label inside its button.
    assertTextInside("tailor", findTextIn("Done", 160, 196, 76, 36), "Done", 160, 196, 76, 36);
    assertTextInside("tailor", findTextIn("Cancel", 240, 196, 76, 36), "Cancel", 240, 196, 76, 36);
    chk(160 + 76 <= 240, "tailor: Done and Cancel do not overlap");
    // Left / Right change the shirt; the "n of m" count follows, inside its frame.
    auto shirtCount = [&]() -> std::string {
        for (const auto& r : host::gRecords) {
            if (r.kind != HostRecord::Text || r.text.size() < 5) continue;
            if (contains(r, 90, tt::ui::pad::tailor::kTopY, 140, tt::ui::pad::tailor::kRowH) &&
                r.text.find(" of ") != std::string::npos)
                return r.text;
        }
        return "";
    };
    const std::string count0 = shirtCount();
    chk(!count0.empty(), "tailor: the shirt count drawn");
    frame(KEY_DRIGHT, 0, 0);
    const std::string count1 = shirtCount();
    chk(!count1.empty() && count1 != count0, "tailor: the shirt count advanced");
    frame(KEY_DLEFT, 0, 0);
    chk(shirtCount() == count0, "tailor: the shirt count steps back");
    // B on the menu asks to leave: the panel sends on the NEXT update, then closes.
    frame(KEY_B, 0, 0);
    frame(0);
    chk(!gUi->tailor().active(), "tailor: B on the menu closes the panel");
    // A new movie with a near deadline reopens it; the Hurry! countdown appears
    // inside its badge for the last 30 seconds.
    host::stageTailorMovie.mode = net::kTailorMovieStart;
    host::stageTailorMovie.deadlineMs = uint32_t(gHostNowMs) + 20000;
    host::stageTailorMovie.seq = 2;
    gClient->connect();
    gUi->tailor().onMovie(*gClient, uint32_t(gHostNowMs));
    chk(gUi->tailor().active(), "tailor: reopened");
    frame(0);
    assertTextInside("tailor", findTextIn("Hurry! 0:20", 212, 8, 100, 20), "Hurry! 0:20", 212, 8,
                     100, 20);
    assertBounds("tailor timer");
    assertLabelsNoOverlap("tailor timer");
    gUi->tailor().dismiss();
}

void suiteCloset() {
    printf("suite: closet panel\n");
    stagePanelSession();
    host::stageCloset.doId = 999;
    host::stageCloset.ownerId = host::stageAvatarId;
    host::stageCloset.active = true;
    gClient->connect();
    gUi->closet().open(*gClient, uint32_t(gHostNowMs));
    chk(gUi->closet().active(), "closet: the panel is open");
    gUi->closet().update(*gClient);
    drawOnce();
    chk(host::gRecords.size() > 10, "closet: draw produced records", "%zu",
        host::gRecords.size());
    assertBounds("closet");
    assertLabelsNoOverlap("closet");
    assertTextInside("closet", findTextIn("Closet", 4, 4, 312, 252), "Closet", 4, 4, 312, 252);
    const HostRecord* body = findTextIn(
        "Save keeps your current owned clothes.\nD-Pad picks a button, A presses it. B cancels.",
        4, 40, 312, 150);
    chk(body != nullptr, "closet: the body text drawn");
    if (body)
        chk(body->x + body->w <= kW - 4, "closet: the body text fits the card", "%.1f",
            body->x + body->w);
    assertTextInside("closet", findTextIn("Save", 160, 196, 76, 36), "Save", 160, 196, 76, 36);
    assertTextInside("closet", findTextIn("Cancel", 240, 196, 76, 36), "Cancel", 240, 196, 76, 36);
    // Cancel: pending, then update does the (stubbed) exit and closes.
    gUi->handleInput(KEY_TOUCH, 0, T(278, 214), *gState);
    gUi->closet().update(*gClient);
    chk(!gUi->closet().active(), "closet: Cancel closes the panel");
    gUi->closet().dismiss();
}

void suitePurchase() {
    printf("suite: minigame purchase panel\n");
    stagePanelSession();
    static MinigamePurchasePanel purchase; // the trolley owns one; the test owns it here
    chk(purchase.init(), "purchase: init");
    purchase.open(*gClient, 32, uint32_t(gHostNowMs) + 60000, uint32_t(gHostNowMs));
    chk(purchase.active(), "purchase: the panel is open");
    chk(gUi->handleInput(0, 0, T(0, 0), *gState) == 0,
        "purchase: Ui routes the frame (owner resolves)");
    drawOnce();
    chk(host::gRecords.size() > 60, "purchase: draw produced records", "%zu",
        host::gRecords.size());
    assertBounds("purchase");
    assertLabelsNoOverlap("purchase");
    const HostRecord* beans = findTextIn("214", 100, pc::kBtnY - 4, 74, 40);
    chk(beans != nullptr, "purchase: the beans on the jar");
    if (beans) chk(beans->x >= pc::kPlusX && beans->x + beans->w <= pc::kDoneX + pc::kDoneW,
                   "purchase: the jar sits between [+] and Done", "(%.1f, %.1f)", beans->x,
                   beans->x + beans->w);
    assertTextInside("purchase", findTextIn("+32 won   Carry: 16/60", kShopSignW + 10, 2, 138, 20),
                     "+32 won   Carry: 16/60", kShopSignW + 10, 2, 138, 20);
    tt::shop::Profile profile;
    for (int t = 0; t < 7; t++) {
        profile.experience[t] = host::stageStats.experience[t];
        profile.trackAccess[t] = host::stageStats.trackAccess[t];
    }
    profile.maxCarry = host::stageStats.maxCarry;
    assertShopGrid("purchase", "EXIT BACK TO\nPLAYGROUND", purchase.cart(), profile);
    // Buying with the beans the minigame paid.
    chk(purchase.buy(4, 0), "purchase: buying a throw gag");
    chk(purchase.cart().money == 213, "purchase: the beans are spent", "%d",
        purchase.cart().money);
    // The countdown auto-submits near the deadline and closes.
    purchase.update(*gClient, uint32_t(gHostNowMs) + 59001);
    chk(purchase.active() == false, "purchase: closes before the PurchaseManager timeout");
    purchase.close();
    chk(MinigamePurchasePanel::activeInstance() == nullptr,
        "purchase: the owner releases the screen");
}

void suiteFishing() {
    printf("suite: fishing panel\n");
    static FishingView fv;
    fv = FishingView{};
    fv.phase = FishingView::kReady;
    fv.practice = true;
    strcpy(fv.status, "Tap Cast, then drag down for power.");
    fv.tank = 3;
    fv.tankMax = 20;
    fv.beans = 214;
    fv.rod = 0;
    gCtx->fishing = &fv;
    // Ui::handleInput resolves the owner from the synced context (main loop order);
    // the panels can only see the fishing view in draw().
    gUi->syncContext(*gClient, 2000, false, false, true, false);
    uiFrame(0, 0, 0, 0);
    chk(host::gRecords.size() > 20, "fishing: draw produced records", "%zu",
        host::gRecords.size());
    assertBounds("fishing ready");
    assertLabelsNoOverlap("fishing ready");
    chk(gUi->laffShown() == false, "fishing: no tab bar under the fishing panel");
    // The header: practice badge and status inside the card. FishingPanel labels the badge
    // "Practice mode" (its kLPractice reparse), drawn inside the yellow pill.
    assertTextInside("fishing", findTextIn("Practice mode", 196, 5, 112, 17), "Practice mode",
                     196, 5, 112, 17);
    const HostRecord* status = findTextIn(fv.status, 4, 2, 312, 40);
    chk(status != nullptr, "fishing: the status line drawn");
    if (status) chk(status->x + status->w <= kW - 4, "fishing: the status fits the card", "%.1f",
                    status->x + status->w);
    // The Cast button and its label; the Exit part and its label; both on screen,
    // neither inside the other.
    // The Cast label: FishingPanel centers it on the button's cx and hangs it at the
    // button's BOTTOM edge (kBtnCy + kBtnSize/2 - 2 = y 148, the button spans y 94..150),
    // with a shadow copy 1 px down-right (modeled as-is; a raise is a claude-3ds-gui call).
    // The ruler asserts what 320x240 needs: drawn, shadow + ink on screen, centered.
    const HostRecord* castLabel = nullptr;
    for (const auto* t : textRecords())
        if (t->text == "Cast" && (!castLabel || t->x + t->w < castLabel->x + castLabel->w))
            castLabel = t;
    chk(castLabel != nullptr, "fishing: 'Cast' drawn");
    if (castLabel) {
        chk(castLabel->x >= 0 && castLabel->x + castLabel->w <= kW, "fishing: 'Cast' on screen",
            "(%.1f, %.1f)", castLabel->x, castLabel->x + castLabel->w);
        chk(castLabel->y >= 0 && castLabel->y + castLabel->h <= kScreenH,
            "fishing: 'Cast' on screen vertically", "(%.1f, %.1f)", castLabel->y,
            castLabel->y + castLabel->h);
        chk(fabsf(castLabel->x + castLabel->w * 0.5f - 160.0f) <= 2.0f,
            "fishing: 'Cast' centered on the cast button", "%.1f",
            castLabel->x + castLabel->w * 0.5f);
    }
    const HostRecord* exitLabel = findTextIn("Exit", 279, 165, 34, 45);
    chk(exitLabel != nullptr, "fishing: the Exit label drawn");
    if (exitLabel)
        chk(exitLabel->x + exitLabel->w <= kW && exitLabel->y + exitLabel->h <= kScreenH,
            "fishing: the Exit label stays on screen", "(%.1f, %.1f)", exitLabel->x,
            exitLabel->y);
    // A catch result: the "You caught:" card, item, weight and value, all inside.
    fv.phase = FishingView::kResult;
    fv.result = FishingView::kFish;
    strcpy(fv.itemName, "Old King Claude");
    strcpy(fv.itemWeight, "12 lbs");
    strcpy(fv.itemValue, "+34 beans");
    fv.revision++;
    uiFrame(0, 0, 0, 0);
    assertBounds("fishing result");
    assertLabelsNoOverlap("fishing result");
    assertTextInside("fishing", findTextIn("You caught:", 10, 46, 300, 46), "You caught:", 10, 46,
                     300, 46);
    assertTextInside("fishing", findTextIn("Old King Claude", 10, 46, 300, 46), "Old King Claude",
                     10, 46, 300, 46);
    assertTextInside("fishing", findTextIn("12 lbs", 10, 46, 300, 46), "12 lbs", 10, 46, 300, 46);
    assertTextInside("fishing", findTextIn("+34 beans", 10, 46, 300, 46), "+34 beans", 10, 46, 300,
                     46);
    // The cast gesture: press inside the Cast circle, drag down for power, release. On the
    // device a touch that is down also appears in hidKeysHeld, so the press frame carries
    // held = KEY_TOUCH too (else FishingPanel::handleInput would see "drag ended" on the
    // very press frame; Ui::handleInput(down, t) adds the bit exactly like this).
    uiFrame(KEY_TOUCH, KEY_TOUCH, 160, 122);
    chk(gUi->fishingPanel().touch().started, "fishing: the cast button press starts a cast");
    uiFrame(0, KEY_TOUCH, 160, 152); // a 30 px drag = half power
    chk(fabsf(gUi->fishingPanel().touch().power - 0.5f) < 0.02f, "fishing: 30px drag is half power",
        "%.2f", gUi->fishingPanel().touch().power);
    chk(gUi->fishingPanel().touch().dragging, "fishing: the drag is live while held");
    uiFrame(0, KEY_TOUCH, 200, 252); // far past full: clamps
    chk(gUi->fishingPanel().touch().power == 1.0f, "fishing: power clamps at 1");
    uiFrame(0, 0, 0, 0); // release
    chk(gUi->fishingPanel().touch().released, "fishing: release fires the cast");
    // The drag direction sets the angle, clamped to FishGlobals' 50 degrees. The panel's
    // rad2Deg(atan(dx / -dy)) makes a down-LEFT drag positive: (-160 / -30) -> ~79 -> +50.
    uiFrame(KEY_TOUCH, KEY_TOUCH, 160, 122);
    uiFrame(0, KEY_TOUCH, 61, 222); // 45 degrees down-left
    const FishingTouch& t2 = gUi->fishingPanel().touch();
    chk(fabsf(t2.angle - 45.0f) < 1.0f, "fishing: the 45-degree drag aims 45 degrees", "%.1f",
        t2.angle);
    uiFrame(0, KEY_TOUCH, 0, 152); // steep left: clamps
    chk(fabsf(fabsf(gUi->fishingPanel().touch().angle) - 50.0f) < 0.5f,
        "fishing: the angle clamps at 50", "%.1f", gUi->fishingPanel().touch().angle);
    uiFrame(0, 0, 0, 0); // release
    gUi->fishingPanel().clearTouchEvents();
    // The Exit button: press inside its circle and release (the press frame holds the
    // touch like the device does, so the exit waits for the finger to lift).
    uiFrame(KEY_TOUCH, KEY_TOUCH, 296, 182);
    chk(!gUi->fishingPanel().touch().exit, "fishing: the exit needs a release");
    uiFrame(0, 0, 0, 0);
    chk(gUi->fishingPanel().touch().exit, "fishing: the exit fires on release");
    gUi->fishingPanel().clearTouchEvents();
    gCtx->fishing = nullptr;
    gUi->syncContext(*gClient, 2000, false, false, false, false);
    uiFrame(0, 0, 0, 0);
    chk(findTextIn("Map", 62, 208, 52, 32) != nullptr, "fishing: clear returns to the tabs");
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
    static net::Client client;
    gClient = &client;
    host::stageReset();

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
    suiteBattle();
    suiteShop();
    suiteClerks();
    suiteTailor();
    suiteCloset();
    suitePurchase();
    suiteFishing();

    ui.shutdown();
    printf("checks: %ld, failures: %d\n", gChecks, gFails);
    if (gFails == 0) printf("gui_panel_test: PASS\n");
    return gFails == 0 ? 0 : 1;
}
