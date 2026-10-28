#include "ui/panel_geometry.h" // qafix: bounded map content
#include "core/nearest.h"
#include "render/ui.h"

#include "ui/gag_data.h"
#include "world/battle_fx.h"
#include "world/place.h"
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "core/log.h"
#include "ui/pad_tables.h" // padnav
#include "core/memory.h"
#include "core/profile.h"

namespace tt::ui {

const float kToonColors[kNumToonColors][3] = {
    {1.0f, 1.0f, 1.0f},         {0.96875f, 0.691406f, 0.699219f}, {0.933594f, 0.265625f, 0.28125f},
    {0.863281f, 0.40625f, 0.417969f}, {0.710938f, 0.234375f, 0.4375f}, {0.570312f, 0.449219f, 0.164062f},
    {0.640625f, 0.355469f, 0.269531f}, {0.996094f, 0.695312f, 0.511719f}, {0.832031f, 0.5f, 0.296875f},
    {0.992188f, 0.480469f, 0.167969f}, {0.996094f, 0.898438f, 0.320312f}, {0.996094f, 0.957031f, 0.597656f},
    {0.855469f, 0.933594f, 0.492188f}, {0.550781f, 0.824219f, 0.324219f}, {0.242188f, 0.742188f, 0.515625f},
    {0.304688f, 0.96875f, 0.402344f},  {0.433594f, 0.90625f, 0.835938f}, {0.347656f, 0.820312f, 0.953125f},
    {0.191406f, 0.5625f, 0.773438f},   {0.558594f, 0.589844f, 0.875f},   {0.285156f, 0.328125f, 0.726562f},
    {0.460938f, 0.378906f, 0.824219f}, {0.546875f, 0.28125f, 0.75f},     {0.726562f, 0.472656f, 0.859375f},
    {0.898438f, 0.617188f, 0.90625f},  {0.7f, 0.7f, 0.8f},               {0.3f, 0.3f, 0.35f},
};

const unsigned kTeleportHoods[kNumTeleportHoods] = {2000, 1000, 5000, 4000, 3000, 9000, 6000, 8000};

namespace {

constexpr float kW = 320.0f;
// Map tab teleport row (just above the tab bar).
constexpr float kHoodRowY = 178.0f, kHoodRowH = 26.0f, kHoodW = 320.0f / kNumTeleportHoods;
constexpr float kHomeY = 148.0f, kHomeH = 24.0f;
const char* const kHoodShort[kNumTeleportHoods] = {"TTC", "DD", "DG", "MM", "BR", "DL", "AA", "GS"};
constexpr float kTabH = 32.0f;
constexpr float kTabY = 240.0f - kTabH;
constexpr float kTabW = kW / kVisibleTabCount;
const char* const kTabNames[kTabCount] = {"Map", "Chat", "Toon", "Gags", "Setup", "Debug"};

// The world tabs are the closed Shticker Book's quick pages.  Keep the same
// parchment, red-leather and gold language instead of the old renderer-debug blue.
const u32 kBg = C2D_Color32(0xFA, 0xF6, 0xEE, 0xFF);
const u32 kPanel = C2D_Color32(0xE8, 0xDE, 0xCA, 0xFF);
const u32 kPanelAlt = C2D_Color32(0xF2, 0xEC, 0xDF, 0xFF);
const u32 kLeather = C2D_Color32(0x6E, 0x12, 0x12, 0xFF);
const u32 kLeatherLight = C2D_Color32(0x91, 0x25, 0x20, 0xFF);
const u32 kDebugBg = C2D_Color32(0x1E, 0x2A, 0x44, 0xFF);
const u32 kAccent = C2D_Color32(0xF5, 0xB8, 0x1A, 0xFF);
const u32 kWhite = C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF);
const u32 kInk = C2D_Color32(0x20, 0x18, 0x10, 0xFF);
const u32 kDim = C2D_Color32(0x78, 0x68, 0x58, 0xFF);

// Toon tab swatch grid
constexpr float kSwX = 8, kSwY = 44, kSwSize = 30, kSwGap = 4;
constexpr int kSwCols = 9;
// Settings buttons
constexpr float kBtnX = 12, kBtnW = 296, kBtnH = 36;
constexpr float kStereoY = 30, kBookY = 82;

// ---- padnav: the focus tables (ui/pad_tables.h) must match this layout ----
static_assert(pad::hoods::kCount == kNumTeleportHoods && pad::hoods::kRowY == kHoodRowY &&
                  pad::hoods::kRowH == kHoodRowH && pad::hoods::kHomeY == kHomeY && pad::hoods::kHomeH == kHomeH,
              "Map tab focus table");
static_assert(pad::toon::kNumColors == kNumToonColors && pad::toon::kSwCols == kSwCols && pad::toon::kSwX == kSwX &&
                  pad::toon::kSwY == kSwY && pad::toon::kSwSize == kSwSize && pad::toon::kSwGap == kSwGap,
              "Toon tab focus table");
static_assert(pad::setup::kBtnX == kBtnX && pad::setup::kBtnW == kBtnW && pad::setup::kBtnH == kBtnH &&
                  pad::setup::kStereoY == kStereoY && pad::setup::kBookY == kBookY,
              "Setup tab focus table");
// ---- end padnav ----

bool inRect(const touchPosition& t, float x, float y, float w, float h) {
    return t.px >= x && t.px < x + w && t.py >= y && t.py < y + h;
}

u32 colorOf(int idx) {
    const float* c = kToonColors[idx];
    return C2D_Color32f(c[0], c[1], c[2], 1.0f);
}

} // namespace

bool Ui::init() {
    if (!serviceClerks_.init()) return false; // clerks

    textCache_.clear();
    textBuf_ = C2D_TextBufNew(1024);
    staticBuf_ = C2D_TextBufNew(64);
    debugBuf_ = C2D_TextBufNew(1024);
    if (!textBuf_ || !staticBuf_ || !debugBuf_ || !chat_.init() || !shop_.init() || !tailor_.init() ||
        !closet_.init() ||
        !battle_.init() || !quest_.init() || !fishingPanel_.init() || !book_.init()) return false;
    for (int i = 0; i < kTabCount; i++) {
        C2D_TextParse(&tabText_[i], staticBuf_, kTabNames[i]);
        C2D_TextOptimize(&tabText_[i]);
    }
    return true;
}

void Ui::shutdown() {
    serviceClerks_.shutdown(); // clerks

    unloadMap();
    chat_.shutdown();
    shop_.shutdown();
    tailor_.shutdown();
    closet_.shutdown();
    battle_.shutdown();
    quest_.shutdown();
    fishingPanel_.shutdown();
    book_.shutdown();
    C2D_TextBuf* bufs[] = {&textBuf_, &staticBuf_, &debugBuf_};
    for (C2D_TextBuf* b : bufs) {
        if (*b) C2D_TextBufDelete(*b);
        *b = nullptr;
    }
}

void Ui::debugLine(float x, float y, float scale, u32 color, const char* fmt, ...) {
    if (numDebugLines_ >= kMaxDebugLines) return;
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    CachedLine& l = debugLines_[numDebugLines_++];
    C2D_TextParse(&l.text, debugBuf_, buf);
    C2D_TextOptimize(&l.text);
    l.x = x;
    l.y = y;
    l.scale = scale;
    l.color = color;
}

void Ui::text(float x, float y, float scale, u32 color, const char* fmt, ...) {
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    C2D_Text t;
    textCache_.parse(&t, textBuf_, buf, 1024);
    C2D_DrawText(&t, C2D_WithColor, x, y, 0.5f, scale, scale, color);
}

// ---- qafix: every full-screen panel uses the same ownership policy ----
PanelOwner Ui::resolveOwner(bool fishing, bool info) {
    const auto* purchase = MinigamePurchasePanel::activeInstance();
    PanelRequests requests;
    requests.battle = battleView_ != nullptr;
    requests.purchase = purchase && purchase->active();
    requests.info = info;
    requests.talk = talk_ && talk_->locked();
    requests.clerk = serviceClerks_.active();
    requests.shop = shop_.active();
    requests.tailor = tailor_.active();
    requests.closet = closet_.active();
    requests.fishing = fishing;
    requests.book = book_.isOpen();
    const PanelOwner owner = choosePanel(requests);
    if (owner != lastOwner_) {
        TT_LOG("panel: close owner=%s", panelName(lastOwner_));
        TT_LOG("panel: open owner=%s", panelName(owner));
        lastOwner_ = owner;
    }
    return owner;
}

void Ui::syncContext(net::Client& client, unsigned zone, bool lost, bool exclusive,
                     bool fishing, bool info) {
    const bool changed = contextZone_ && contextZone_ != zone;
    contextZone_ = zone;
    contextFishing_ = fishing;
    contextInfo_ = info;
    if (changed || lost || exclusive) {
        // Cancel transactions before relinquishing them when the object still exists.
        if (shop_.active()) { shop_.finish(true); shop_.update(client, 0); }
        if (tailor_.active()) { tailor_.cancel(); tailor_.update(client, 0); }
        shop_.dismiss();
        tailor_.dismiss();
        closet_.dismiss();
        serviceClerks_.dismiss();
        if (talk_) talk_->reset();
        book_.close();
    }
    if (changed || lost) {
        if (auto* purchase = MinigamePurchasePanel::activeInstance()) purchase->close();
    }
}
// ---- end qafix ----

u32 Ui::handleInput(u32 down, const touchPosition& t, State& s) {
    return handleInput(down, hidKeysHeld() | (down & KEY_TOUCH), t, s);
}

u32 Ui::handleInput(u32 down, u32 held, const touchPosition& t, State& s) {
    // ---- racing2 ----
    if (contextRacing_) return down & ~(KEY_A | KEY_B | KEY_R);
    // ---- end racing2 ----
    // ---- qafix: input and drawing share resolveOwner ----
    // An info panel drawn last frame (loading, streaming Cogs) owns input too: both calls must agree.
    const PanelOwner owner = resolveOwner(contextFishing_, contextInfo_ || drawnInfo_);
    // ---- padnav: a panel that owns the screen drops the tab focus, so A jumps afterwards ----
    if (owner != PanelOwner::Tabs) tabFocus_.blur();
    // ---- end padnav ----
    if (owner == PanelOwner::Clerk) return serviceClerks_.handleInput(down, t);
    if (owner == PanelOwner::Book) {
        BookDrawContext bdc;
        bdc.state = &s;
        bdc.book = inputBook_;
        return book_.handleInput(down, t, bdc);
    }
    if (owner == PanelOwner::Shop) return shop_.handleInput(down, t);
    if (owner == PanelOwner::Tailor) return tailor_.handleInput(down, t);
    if (owner == PanelOwner::Closet) return closet_.handleInput(down, t);
    if (owner == PanelOwner::Battle) return battle_.handleInput(down, t, *battleView_, battleGags_);
    if (owner == PanelOwner::Talk) return quest_.handleInput(down, t, *talk_);
    if (owner == PanelOwner::Purchase || owner == PanelOwner::Info) return down & ~KEY_B;
    if (owner == PanelOwner::Fishing) return down & ~(KEY_A | KEY_B);
    // ---- end qafix ----
    // Debug is a developer view, not a sixth player-facing tab. Keep it reachable
    // without letting it become the first thing a player sees on the bottom screen.
    if (down & KEY_SELECT) {
        if (s.tab == kTabDebug) {
            s.tab = s.debugReturnTab >= 0 && s.debugReturnTab < kVisibleTabCount ? s.debugReturnTab : kTabMap;
        } else {
            if (s.tab >= 0 && s.tab < kVisibleTabCount) s.debugReturnTab = s.tab;
            s.tab = kTabDebug;
        }
        chat_.blur();
        tabFocus_.blur();
        lastTab_ = s.tab;
        TT_LOG("ui: debug %s", s.tab == kTabDebug ? "open" : "close");
        return KEY_SELECT;
    }
    // ---- padnav: D-Pad / A / B on a focused Map, Toon or Setup tab ----
    if (const u32 padUsed = handleTabPad(down, t, s)) return padUsed;
    // ---- end padnav ----
    if (down & KEY_START) {
        book_.open();
        return KEY_START;
    }
    if (down & KEY_DLEFT)
        s.tab = s.tab == kTabDebug ? kTabSettings : (s.tab + kVisibleTabCount - 1) % kVisibleTabCount;
    if (down & KEY_DRIGHT)
        s.tab = s.tab == kTabDebug ? kTabMap : (s.tab + 1) % kVisibleTabCount;
    if (down & KEY_X) s.tab = kTabChat;
    if (down & KEY_Y) s.tab = kTabMap;
    if ((down & KEY_TOUCH) && t.py >= kTabY) {
        s.tab = static_cast<int>(t.px / kTabW);
        if (s.tab >= kVisibleTabCount) s.tab = kVisibleTabCount - 1;
    }
    if (s.tab != lastTab_) {
        chat_.blur(); // the highlight (and A/B capture) never survives a tab change
        tabFocus_.blur(); // padnav
        lastTab_ = s.tab;
        return 0;
    }
    if (s.tab == kTabChat) {
        // Header button: cycle the whisper target through the toons in view.
        if ((down & KEY_TOUCH) && numNearby_ > 0 && inRect(t, kW - 100, 2, 96, 24)) {
            cycleWhisper(s); // padnav: shared with X
            return down;
        }
        // ---- padnav: X (already on this tab) presses the "To:" header button ----
        if ((down & KEY_X) && numNearby_ > 0) {
            TT_LOG("padnav: chat activate whisper");
            cycleWhisper(s);
            return KEY_X;
        }
        // ---- end padnav ----
        bool stillHere = s.whisperTo == 0;
        for (int i = 0; i < numNearby_; i++) stillHere |= nearbyIds_[i] == s.whisperTo;
        if (!stillHere) s.whisperTo = 0;
        return chat_.handleInput(down, held, t, kTabY);
    }
    if (!(down & KEY_TOUCH) || t.py >= kTabY) return 0;

    switch (s.tab) {
    case kTabMap:
        if (inRect(t, 4, kHomeY, 76, kHomeH)) {
            s.homeRequest = true;
            break;
        }
        if (t.py >= kHoodRowY && t.py < kHoodRowY + kHoodRowH) {
            const int i = static_cast<int>(t.px / kHoodW);
            if (i >= 0 && i < kNumTeleportHoods) s.teleportRequest = kTeleportHoods[i];
        }
        break;
    case kTabToon:
        if (inRect(t, 60, 175, 200, 24)) {
            book_.open();
            break;
        }
        for (int i = 0; i < 3; i++) {
            if (inRect(t, 8 + i * 102, 8, 98, 28)) s.colorTarget = i;
        }
        for (int i = 0; i < kNumToonColors; i++) {
            const float x = kSwX + (i % kSwCols) * (kSwSize + kSwGap);
            const float y = kSwY + (i / kSwCols) * (kSwSize + kSwGap);
            if (inRect(t, x, y, kSwSize, kSwSize)) {
                (s.colorTarget == 0 ? s.bodyColor : s.colorTarget == 1 ? s.shirtColor : s.bottomColor) = i;
            }
        }
        break;
    case kTabSettings:
        if (inRect(t, kBtnX, kStereoY, kBtnW, kBtnH)) s.stereo = !s.stereo;
        if (inRect(t, kBtnX, kBookY, kBtnW, kBtnH)) book_.open();
        break;
    default:
        break;
    }
    return 0;
}

void Ui::draw(C3D_RenderTarget* target, const State& s, const Context& c) {
    frame_++;
    // Persistent glyph cache: unchanged strings require no layout work.
    C2D_TargetClear(target, kBg);
    C2D_SceneBegin(target);
    drawnInfo_ = c.infoTitle != nullptr;
    const PanelOwner owner = resolveOwner(c.fishing != nullptr, contextInfo_ || drawnInfo_); // qafix
    if (owner == PanelOwner::Clerk) { // clerks
        serviceClerks_.draw();
        return;
    }
    if (owner == PanelOwner::Book) {
        BookDrawContext bdc;
        bdc.stats = c.stats;
        bdc.quests = c.quests;
        bdc.book = c.book;
        bdc.fish = c.fish;
        bdc.districts = c.districts;
        bdc.numDistricts = c.numDistricts;
        bdc.state = const_cast<State*>(&s);
        bdc.ctx = &c;
        bdc.mapImage = hasMap_ ? &mapImage_ : nullptr;
        bdc.hasMap = hasMap_;
        bdc.mapMinX = mapMinX_;
        bdc.mapMinY = mapMinY_;
        bdc.mapMaxX = mapMaxX_;
        bdc.mapMaxY = mapMaxY_;
        bdc.mapW = mapW_;
        bdc.mapH = mapH_;
        book_.draw(bdc);
        return;
    }
    if (owner == PanelOwner::Shop) {
        shop_.draw();
        return;
    }
    if (owner == PanelOwner::Tailor) {
        tailor_.draw();
        return;
    }
    if (owner == PanelOwner::Closet) {
        closet_.draw();
        return;
    }
    if (owner == PanelOwner::Battle) {
        battle_.draw(*battleView_, battleGags_);
        return;
    }
    if (owner == PanelOwner::Purchase) {
        MinigamePurchasePanel::activeInstance()->draw();
        return;
    }
    if (owner == PanelOwner::Info) {
        drawInfo(c);
        return;
    }
    if (owner == PanelOwner::Talk) {
        quest_.drawTalk(*talk_);
        return;
    }
    if (owner == PanelOwner::Fishing) {
        fishingPanel_.draw(*c.fishing, kTabY);
        return;
    }
    switch (s.tab) {
    case kTabMap: drawMap(c); break;
    case kTabChat: {
        chat_.draw(kTabY);
        numNearby_ = c.numNearby < 8 ? c.numNearby : 8;
        const char* target = "Everyone";
        for (int i = 0; i < numNearby_; i++) {
            nearbyIds_[i] = c.nearbyIds[i];
            snprintf(nearbyNames_[i], sizeof(nearbyNames_[i]), "%s", c.nearbyNames[i] ? c.nearbyNames[i] : "");
            if (nearbyIds_[i] == s.whisperTo) target = nearbyNames_[i];
        }
        if (numNearby_ > 0) {
            C2D_DrawRectSolid(kW - 100, 2, 0.5f, 96, 24, s.whisperTo ? kAccent : kLeatherLight);
            text(kW - 96, 7, 0.4f, s.whisperTo ? kInk : kWhite, "To: %.12s", target);
        }
        break;
    }
    case kTabToon: drawToon(s, c); break;
    case kTabGags: drawGags(c); break;
    case kTabSettings: drawSettings(s); break;
    default: drawDebug(c); break;
    }
    // ---- padnav ----
    toonOnline_ = c.quests != nullptr;
    tabFocus_.drawHighlight();
    // ---- end padnav ----
    drawTabs(s);
}

// ---- padnav: button focus on the Map / Toon / Setup tabs ----
// Nothing is focused until D-Up or D-Down is pressed on one of these tabs, so A still
// jumps, B still walks and D-Left / D-Right still change tabs. While a control is focused
// the D-Pad moves between controls, A presses the focused one and B lets go of the focus.
// Touch keeps its own code below; a touch on a control while focused moves the focus there.
u32 Ui::handleTabPad(u32 down, const touchPosition& t, State& s) {
    switch (s.tab) {
    case kTabMap:
        tabFocus_.setPanelName("tab_map");
        pad::buildHoodRow(tabFocus_);
        break;
    case kTabToon:
        tabFocus_.setPanelName("tab_toon");
        pad::buildToonTab(tabFocus_, toonOnline_);
        break;
    case kTabSettings:
        tabFocus_.setPanelName("tab_setup");
        pad::buildSetupTab(tabFocus_);
        break;
    default: // Chat has its own menu; Gags and Debug only show information
        tabFocus_.clear();
        return 0;
    }
    if (!tabFocus_.hasFocus()) {
        if (!(down & (KEY_DUP | KEY_DDOWN))) return 0;
        tabFocus_.move(0, (down & KEY_DUP) ? -1 : 1); // focuses the first control
        return down & (KEY_DUP | KEY_DDOWN);
    }
    if (down & KEY_TOUCH) {
        const int hit = tabFocus_.itemAt(t.px, t.py);
        if (hit >= 0) tabFocus_.setFocus(hit);
        return 0; // the touch code in handleInput performs the press
    }
    if (down & KEY_B) {
        tabFocus_.blur();
        return KEY_B;
    }
    int activated = -1;
    tabFocus_.handleInput(down & (kDpadKeys | KEY_A), t, &activated);
    activateTabControl(activated, s);
    return down & (kDpadKeys | KEY_A);
}

// The same actions as the touch code in handleInput.
void Ui::activateTabControl(int id, State& s) {
    if (id < 0) return;
    if (s.tab == kTabMap) {
        if (id == pad::hoods::kIdHome) {
            s.homeRequest = true;
        } else if (id < kNumTeleportHoods) {
            s.teleportRequest = kTeleportHoods[id];
        }
    } else if (s.tab == kTabToon) {
        if (id == pad::toon::kIdBook) {
            book_.open();
        } else if (id >= pad::toon::kIdTarget0 && id < pad::toon::kIdTarget0 + 3) {
            s.colorTarget = id - pad::toon::kIdTarget0;
        } else if (id < kNumToonColors) {
            int& color = s.colorTarget == 0 ? s.bodyColor : s.colorTarget == 1 ? s.shirtColor : s.bottomColor;
            color = id;
        }
    } else if (s.tab == kTabSettings) {
        if (id == pad::setup::kIdStereo) {
            s.stereo = !s.stereo;
        } else if (id == pad::setup::kIdBook) {
            book_.open();
        }
    }
}

// The Chat tab's "To:" button: the next toon in view, then everyone.
void Ui::cycleWhisper(State& s) {
    int next = 0;
    for (int i = 0; i < numNearby_; i++) {
        if (nearbyIds_[i] == s.whisperTo) next = i + 1;
    }
    s.whisperTo = next < numNearby_ ? nearbyIds_[next] : 0;
}
// ---- end padnav ----

void Ui::drawTabs(const State& s) {
    C2D_DrawRectSolid(0, kTabY, 0.5f, kW, kTabH, kLeather);
    for (int i = 0; i < kVisibleTabCount; i++) {
        const float x = i * kTabW;
        // The selected bookmark rises out of the leather strip, like the full book tabs.
        C2D_DrawRectSolid(x + 2, kTabY + (i == s.tab ? 1 : 5), 0.51f, kTabW - 4,
                          i == s.tab ? kTabH - 1 : kTabH - 5, i == s.tab ? kAccent : kLeatherLight);
        float tw = 0.0f;
        C2D_TextGetDimensions(&tabText_[i], 0.5f, 0.5f, &tw, nullptr);
        C2D_DrawText(&tabText_[i], C2D_WithColor, x + (kTabW - tw) * 0.5f, kTabY + 9, 0.52f, 0.5f, 0.5f,
                     i == s.tab ? kInk : kWhite);
    }
}

void Ui::unloadMap() {
    if (hasMap_) {
        mem::LinearGuard g;
        C3D_TexDelete(&mapTex_);
    }
    mapTex_ = C3D_Tex{};
    mapImage_ = C2D_Image{};
    hasMap_ = false;
}

// .tmap v1, see tools/pipeline/bake_minimap.py.
void Ui::setMapZone(unsigned fileZone) {
    if (fileZone == mapZone_) return;
    mapZone_ = fileZone;
    unloadMap();
    if (!fileZone) return;
    char path[80];
    snprintf(path, sizeof path, "sdmc:/3ds/toontown3ds/data/zones/%u.tmap", fileZone);
    FILE* f = fopen(path, "rb");
    if (!f) return;
    unsigned char h[48];
    bool ok = fread(h, 1, sizeof h, f) == sizeof h && memcmp(h, "TMAP", 4) == 0;
    u16 version = 0, w = 0, ht = 0;
    float rect[4] = {};
    u32 dataOffset = 0, dataSize = 0;
    if (ok) {
        memcpy(&version, h + 4, 2);
        memcpy(&w, h + 8, 2);
        memcpy(&ht, h + 10, 2);
        memcpy(rect, h + 16, sizeof rect);
        memcpy(&dataOffset, h + 32, 4);
        memcpy(&dataSize, h + 36, 4);
        ok = version == 1 && h[12] == GPU_ETC1 && w >= 8 && ht >= 8 && w <= 256 && ht <= 256 &&
             rect[2] > rect[0] && rect[3] > rect[1];
    }
    if (ok) {
        render::TexEntry e{};
        e.width = w;
        e.height = ht;
        e.format = GPU_ETC1;
        e.mipLevels = 1;
        e.wrapS = e.wrapT = GPU_CLAMP_TO_EDGE;
        e.dataOffset = dataOffset;
        e.dataSize = dataSize;
        snprintf(e.name, sizeof e.name, "minimap %u", fileZone);
        ok = render::loadTexture(f, e, &mapTex_);
    }
    fclose(f);
    if (!ok) {
        TT_LOG("map: bad or unreadable %s", path);
        mapTex_ = C3D_Tex{};
        return;
    }
    mapSub_.width = w;
    mapSub_.height = ht;
    mapSub_.left = 0.0f;
    mapSub_.top = 1.0f;
    mapSub_.right = 1.0f;
    mapSub_.bottom = 0.0f;
    mapImage_.tex = &mapTex_;
    mapImage_.subtex = &mapSub_;
    mapW_ = w;
    mapH_ = ht;
    mapMinX_ = rect[0];
    mapMinY_ = rect[1];
    mapMaxX_ = rect[2];
    mapMaxY_ = rect[3];
    hasMap_ = true;
    TT_LOG("map: %s %ux%u", path, w, ht);
}

void Ui::drawMap(const Context& c) {
    // Fit the map (baked image rectangle, else zone bounds) into the content area,
    // preserving aspect. Map +Y is up.
    const float areaX = 4, areaY = 4, areaW = kW - 8, areaH = kHomeY - 8;
    const float minX = hasMap_ ? mapMinX_ : c.zoneMinX, maxX = hasMap_ ? mapMaxX_ : c.zoneMaxX;
    const float minY = hasMap_ ? mapMinY_ : c.zoneMinY, maxY = hasMap_ ? mapMaxY_ : c.zoneMaxY;
    const float zw = fmaxf(1.0f, maxX - minX), zh = fmaxf(1.0f, maxY - minY);
    const float scale = hasMap_ ? fminf(areaW / mapW_, areaH / mapH_) * mapW_ / zw
                                : fminf(areaW / zw, areaH / zh);
    const float ox = areaX + (areaW - zw * scale) * 0.5f;
    const float oy = areaY + (areaH - zh * scale) * 0.5f;
    auto mx = [&](float x) { return ox + (x - minX) * scale; };
    auto my = [&](float y) { return oy + (maxY - y) * scale; };

    if (hasMap_) {
        C2D_DrawImageAt(mapImage_, ox, oy, 0.45f, nullptr, zw * scale / mapW_, zh * scale / mapH_);
    } else if (world::placeKind(c.currentZone) == world::PlaceKind::Interior) {
        // No baked map indoors (upstream has none either): a plain floor plan of the room,
        // so the markers still read as "who is where" instead of sitting on grass.
        C2D_DrawRectSolid(ox, oy, 0.5f, zw * scale, zh * scale, C2D_Color32(0x8A, 0x63, 0x3E, 0xFF));
        C2D_DrawRectSolid(ox + 2, oy + 2, 0.5f, zw * scale - 4, zh * scale - 4, C2D_Color32(0xB9, 0x8A, 0x5A, 0xFF));
    } else {
        C2D_DrawRectSolid(ox, oy, 0.5f, zw * scale, zh * scale, C2D_Color32(0x4E, 0x9A, 0x3C, 0xFF));
    }
    // ---- qafix: all marker geometry stays inside the map, above Home ----
    const MapRect rect{ox, oy, zw * scale, zh * scale};
    // Squares, not C2D_DrawCircleSolid: citro2d circles use the procedural-texture
    // unit and did not render after the 3D pass in Azahar (unverified on hardware).
    for (int i = 0; i < c.numMarkers; i++) {
        float x = mx(c.markers[i].x), y = my(c.markers[i].y);
        if (rect.marker(&x, &y, 3))
            C2D_DrawRectSolid(x - 3, y - 3, 0.55f, 6, 6, c.markers[i].color);
    }
    // Player arrow (heading 0 = +Y = up on the map; positive heading turns left).
    const float h = c.playerHeadingDeg * 3.14159265f / 180.0f;
    float px = mx(c.playerX), py = my(c.playerY);
    const bool playerVisible = rect.marker(&px, &py, 11);
    const float fx = -sinf(h), fy = -cosf(h); // screen-space forward
    const float sx = -fy, sy = fx;
    // Long, narrow arrow so the tip is unambiguous at this size.
    if (playerVisible) C2D_DrawTriangle(px + fx * 11, py + fy * 11, kAccent, px - fx * 5 + sx * 4.5f, py - fy * 5 + sy * 4.5f, kAccent,
                     px - fx * 5 - sx * 4.5f, py - fy * 5 - sy * 4.5f, kAccent, 0.6f);
    // ---- end qafix ----
    // A small paper label remains legible over every baked map without exposing
    // implementation zone IDs or coordinates to players.
    C2D_DrawRectSolid(5, 5, 0.61f, 100, 20, kPanelAlt);
    if (!hasMap_ && world::placeKind(c.currentZone) == world::PlaceKind::Interior)
        text(8, 8, 0.45f, kInk, "You are indoors");
    else
        text(8, 8, 0.45f, kInk, "You are here");
    C2D_DrawRectSolid(4, kHomeY, 0.5f, 76, kHomeH, kAccent);
    text(18, kHomeY + 6, 0.42f, kInk, "Home");
    // Teleport row: tap a playground to go there (installed ones only).
    for (int i = 0; i < kNumTeleportHoods; i++) {
        const bool have = c.hoodsAvailable & (1u << i);
        const bool here = kTeleportHoods[i] == c.currentHood;
        const float x = i * kHoodW + 1;
        C2D_DrawRectSolid(x, kHoodRowY, 0.5f, kHoodW - 2, kHoodRowH,
                          here ? kAccent : have ? kLeatherLight : kPanel);
        text(x + 6, kHoodRowY + 6, 0.42f, here ? kInk : have ? kWhite : kDim, "%s", kHoodShort[i]);
    }
}

void Ui::drawToon(const State& s, const Context& c) {
    if (c.quests) { // online: the server's DNA wins, so the tab shows our ToonTasks instead
        // Laff and jellybeans (the HUD shows only the laff meter, as upstream).
        float top = 6;
        if (c.stats) {
            text(6, top, .42f, kInk, "Laff %d/%d", c.stats->hp > 0 ? c.stats->hp : 0, c.stats->maxHp);
            text(130, top, .42f, kAccent, "Jellybeans %d/%d", c.stats->money, c.stats->maxMoney);
            top += 16;
        }
        // ---- clerks: show only the AI's current pet association ----
        if (c.clerkProfile) {
            const char* petName = c.clerkProfile->petId
                ? (c.petName ? c.petName : "Owned (at estate)") : "None";
            text(6, top, .38f, kInk, "Doodle: %s", petName);
            top += 16;
        }
        quest_.drawTasks(*c.quests, top + 6, 170);
        // ---- end clerks ----
        C2D_DrawRectSolid(60, 175, 0.5f, 200, 24, kAccent);
        text(72, 180, 0.42f, kInk, "Open Shticker Book (START)");
        return;
    }
    static const char* const kTargets[3] = {"Body", "Shirt", "Shorts"};
    for (int i = 0; i < 3; i++) {
        C2D_DrawRectSolid(8 + i * 102, 8, 0.5f, 98, 28, i == s.colorTarget ? kAccent : kPanel);
        text(16 + i * 102, 14, 0.5f, i == s.colorTarget ? kInk : kInk, "%s", kTargets[i]);
    }
    const int sel = s.colorTarget == 0 ? s.bodyColor : s.colorTarget == 1 ? s.shirtColor : s.bottomColor;
    for (int i = 0; i < kNumToonColors; i++) {
        const float x = kSwX + (i % kSwCols) * (kSwSize + kSwGap);
        const float y = kSwY + (i / kSwCols) * (kSwSize + kSwGap);
        if (i == sel) C2D_DrawRectSolid(x - 2, y - 2, 0.5f, kSwSize + 4, kSwSize + 4, kWhite);
        C2D_DrawRectSolid(x, y, 0.5f, kSwSize, kSwSize, colorOf(i));
    }
    text(8, 154, 0.45f, kDim, "Tap a color. Head/arms/legs share the body color.");
    text(8, 172, 0.45f, kDim, "A: jump  B: walk  L/R: orbit camera");
}

void Ui::drawSettings(const State& s) {
    text(12, 8, 0.5f, kInk, "Settings");
    C2D_DrawRectSolid(kBtnX, kStereoY, 0.5f, kBtnW, kBtnH, s.stereo ? kAccent : kPanel);
    text(kBtnX + 8, kStereoY + 10, 0.5f, kInk, "Stereo 3D: %s", s.stereo ? "On" : "Off");
    text(186, kStereoY + 11, 0.4f, kDim, "3D slider controls depth");

    C2D_DrawRectSolid(kBtnX, kBookY, 0.5f, kBtnW, kBtnH, kAccent);
    text(kBtnX + 46, kBookY + 9, 0.52f, kInk, "Open Shticker Book");
    text(12, 134, 0.45f, kDim, "Music, sound, graphics and game options");
    text(12, 151, 0.45f, kDim, "are tucked inside your Shticker Book.");
    text(12, 184, 0.4f, kDim, "SELECT opens the hidden performance view.");
}

void Ui::drawInfo(const Context &c) {
    // ---- racing2 ----
    if (c.racing) {
        const auto &race = *c.racing;
        text(8, 4, .6f, kAccent, "%s", c.infoTitle);
        for (int i = 0; i < 3; ++i)
            text(8, 28 + i * 14, .36f, kInk, "%s", c.infoLines[i] ? c.infoLines[i] : "");
        auto x = [&](float value) { return 12 + (value - race.minX) / race.spanX * 140; };
        auto y = [&](float value) { return 194 - (value - race.minY) / race.spanY * 112; };
        for (int i = 0; i < 64; ++i) {
            const int next = (i + 1) % 64;
            C2D_DrawLine(x(race.curve[i][0]), y(race.curve[i][1]), kDim, x(race.curve[next][0]),
                         y(race.curve[next][1]), kDim, 2, .5f);
        }
        for (int i = 0; i < race.count; ++i) {
            const u32 color = i == 0 ? kAccent : kLeatherLight;
            C2D_DrawRectSolid(x(race.racers[i][0]) - 2, y(race.racers[i][1]) - 2, .6f, 4, 4, color);
            text(168, 88 + i * 24, .45f, color, "%s", race.names[i]);
        }
        return;
    }
    // ---- end racing2 ----
    text(12, 12, 0.7f, kAccent, "%s", c.infoTitle);
    for (int i = 0; i < 3; i++) {
        if (c.infoLines[i] && c.infoLines[i][0])
            text(12, 60 + i * 30, 0.55f, kInk, "%s", c.infoLines[i]);
    }
}

void Ui::drawGags(const Context &c) {
    // Like the Shticker Book inventory page: one row per track, counts per gag level.
    // Text is re-parsed every kDebugRefreshFrames (shares the debug line cache).
    if (cachedLinesTab_ != kTabGags || numDebugLines_ == 0 || frame_ % kDebugRefreshFrames == 0) {
        C2D_TextBufClear(debugBuf_);
        numDebugLines_ = 0;
        cachedLinesTab_ = kTabGags;
        if (!c.gagCount) {
            debugLine(8, 8, 0.5f, kDim, "Gags appear once you are online.");
        } else {
            int total = 0;
            for (int t = 0; t < gags::kTracks; t++)
                for (int l = 0; l < gags::kLevels; l++) total += c.gagCount[t][l];
            debugLine(8, 4, 0.5f, kInk, "Gags  %d / %d", total, c.maxCarry);
            for (int t = 0; t < gags::kTracks; t++) {
                const bool have = c.trackAccess[t];
                const int levels = have ? gags::levelsUnlocked(t, c.gagExp[t]) : 0;
                const float y = 26 + t * 24;
                debugLine(8, y, 0.5f, have ? kInk : kDim, "%s", gags::kTrackNames[t]);
                for (int l = 0; l < gags::kLevels; l++) {
                    if (l < levels) {
                        debugLine(80 + l * 22, y, 0.5f, c.gagCount[t][l] ? kInk : kDim, "%u",
                                  unsigned(c.gagCount[t][l]));
                    } else {
                        debugLine(80 + l * 22, y, 0.5f, kDim, "-");
                    }
                }
                const uint16_t next = levels < gags::kLevels ? gags::kLevelExp[t][levels] : 0;
                if (have && next) {
                    debugLine(236, 28 + t * 24, 0.4f, kDim, "%u/%u", unsigned(c.gagExp[t]), unsigned(next));
                } else if (have) {
                    debugLine(236, 28 + t * 24, 0.4f, kDim, "%u max", unsigned(c.gagExp[t]));
                }
            }
        }
    }
    if (c.gagCount) {
        for (int t = 0; t < gags::kTracks; t++)
            C2D_DrawRectSolid(4, 23 + t * 24, 0.46f, 312, 22, (t & 1) ? kPanelAlt : kPanel);
    }
    for (int i = 0; i < numDebugLines_; i++) {
        const CachedLine& l = debugLines_[i];
        C2D_DrawText(&l.text, C2D_WithColor, l.x, l.y, 0.5f, l.scale, l.scale, l.color);
    }
    if (c.gagCount) {
        for (int t = 0; t < gags::kTracks; t++) {
            if (!c.trackAccess[t]) continue;
            const int levels = gags::levelsUnlocked(t, c.gagExp[t]);
            const float y = 26 + t * 24;
            for (int l = 0; l < levels; l++) {
                world::GagAtlas::instance().drawIcon(t, l, 68 + l * 22, y + 2, 14.0f, 0.52f,
                                                     c.gagCount[t][l] ? 1.0f : 0.35f);
            }
        }
    }
}

void Ui::drawDebug(const Context& c) {
    // Diagnostics deliberately retain the compact high-contrast engineering view.
    C2D_DrawRectSolid(0, 0, 0.44f, kW, kTabY, kDebugBg);
    if (cachedLinesTab_ != kTabDebug || numDebugLines_ == 0 || frame_ % kDebugRefreshFrames == 0) {
        const u32 good = C2D_Color32(0x8C, 0xF0, 0x8C, 0xFF);
        const u32 bad = C2D_Color32(0xFF, 0x80, 0x80, 0xFF);
        const u32 debugDim = C2D_Color32(0xA8, 0xB4, 0xCC, 0xFF);
        C2D_TextBufClear(debugBuf_);
        numDebugLines_ = 0;
        cachedLinesTab_ = kTabDebug;
        debugLine(8, 4, 0.5f, kWhite, "Debug  (%s)", c.isNew3ds ? "New 3DS" : "Old 3DS");
        debugLine(248, 4, 0.36f, kWhite, "SELECT: back");
        debugLine(8, 24, 0.5f, c.fps >= 29.5f ? good : bad, "%.1f fps   cpu %.1f ms  gpu %.1f ms", c.fps, c.cpuMs,
                  c.gpuMs);
        debugLine(8, 42, 0.5f, kWhite, "draws %u  tris %u  models %u (+%u culled)", c.drawCalls, c.triangles,
                  c.modelsDrawn, c.modelsCulled);
        debugLine(8, 60, 0.5f, c.heapUsedKB < c.heapBudgetKB ? kWhite : bad, "heap  %u / %u KB used", c.heapUsedKB,
                  c.heapBudgetKB);
        debugLine(8, 78, 0.5f, kWhite, "linear %u / %u KB free", c.linearFreeKB, c.linearBudgetKB);
        // Rows stay within the 320 px screen: zone and toons share the vram row.
        debugLine(8, 96, 0.5f, kWhite, "vram  %u KB free   zone %u  toons %d", c.vramFreeKB, c.zoneId,
                  c.remoteToons);
        if (c.netState) {
            debugLine(8, 114, 0.45f, kWhite, "net %s   in %u  out %u B/s", c.netState, c.netInBps, c.netOutBps);
        } else {
            debugLine(8, 114, 0.45f, debugDim, "net offline");
        }
        // Main-thread CPU breakdown (core/profile.h): mean ms per frame over the last second.
        // "wait" is the vblank/GPU sync, not CPU work; busy = total - wait.
        debugLine(8, 130, 0.4f, prof::busyAvgMs() > 25.0f ? bad : good, "busy %.1f ms  (frame %.1f, wait %.1f)",
                  static_cast<double>(prof::busyAvgMs()), static_cast<double>(prof::totalAvgMs()),
                  static_cast<double>(prof::avgMs(prof::kWait)));
        // ---- qafix: reserve a separate footer row before the tabs ----
        constexpr int kProfRows = 5;
        constexpr int kProfSlots = 15; // three columns within the 320px screen
        int selected[kProfSlots];
        nearestIndices(prof::kCount,
            [](int index) { return index != prof::kWait; },
            [](int index) { return -prof::avgMs(static_cast<prof::Section>(index)); },
            0.0f, selected);
        for (int slot=0;slot<kProfSlots;++slot) {
            const int index=selected[slot];
            if (index<0) continue;
            const float ms=prof::avgMs(static_cast<prof::Section>(index));
            debugLine(8+(slot/kProfRows)*106,143+(slot%kProfRows)*10,.34f,ms>4?bad:kWhite,"%-6s %5.2f",
                      prof::name(static_cast<prof::Section>(index)),static_cast<double>(ms));
        }
        debugLine(8, 196, 0.32f, debugDim, "%.68s", log::recent(0));
        // ---- end qafix ----
    }
    for (int i = 0; i < numDebugLines_; i++) {
        const CachedLine& l = debugLines_[i];
        C2D_DrawText(&l.text, C2D_WithColor, l.x, l.y, 0.5f, l.scale, l.scale, l.color);
    }
}

} // namespace tt::ui
