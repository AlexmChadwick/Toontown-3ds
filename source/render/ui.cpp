#include "render/ui_font.h"
#include "ui/panel_geometry.h" // qafix: bounded map content
#include "core/nearest.h"
#include "render/ui.h"

#include "ui/gag_data.h"
#include "ui/gag_table.h"
#include "ui/laff_meter.h"
#include "ui/theme.h"
#include "ui/map_view.h"
#include "quest/quest_text.h"
#include "world/battle_fx.h"
#include "world/place.h"
#include <cmath>
#include <cerrno>
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
// Map tab: the teleport row under the map, then Home and the place label beside the laff pocket.
constexpr float kHoodRowY = pad::hoods::kRowY, kHoodRowH = pad::hoods::kRowH, kHoodW = pad::hoods::kHoodW;
constexpr float kHomeX = pad::hoods::kTabHomeX, kHomeY = pad::hoods::kHomeY, kHomeW = pad::hoods::kHomeW,
                kHomeH = pad::hoods::kHomeH;
constexpr float kTabH = 32.0f;
constexpr float kTabY = 240.0f - kTabH;
// Our Laff meter sits in a pocket at the binder's left end, as upstream's sits at the screen's
// bottom left; the tabs share the rest of the bar. Panels keep the pocket's corner clear.
constexpr float kPocketW = pad::kLaffPocketW, kPocketTop = pad::kLaffPocketTop;
constexpr float kPocketLaff = 72.0f; // the meter's atlas cell (the head fills ~75% of it)
constexpr float kChatLogH = 34.0f; // Chat tab: two lines of recent chat above the tab bar
const char* const kTabNames[kTabCount] = {"Map", "Chat", "Toon", "Gags", "Book", "Debug"};
// Book tab buttons, in Book::PageIndex order.
const char* const kBookPageNames[pad::booktab::kCount] = {"Gags",       "ToonTasks", "Cog Gallery",
                                                          "Cog Suits",  "Fish",      "Trophies",
                                                          "SOS Toons",  "Options",   "Map"};

// The Debug panel and the plain info panel keep a dark, high-contrast look (developer data).
const u32 kBg = C2D_Color32(0x1E, 0x2A, 0x44, 0xFF);
const u32 kPanel = C2D_Color32(0x2C, 0x3C, 0x60, 0xFF);
const u32 kAccent = C2D_Color32(0xF5, 0xB8, 0x1A, 0xFF);
const u32 kWhite = C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF);
const u32 kDim = C2D_Color32(0xA8, 0xB4, 0xCC, 0xFF);

// Toon tab swatch grid
constexpr float kSwX = 8, kSwY = 44, kSwSize = 30, kSwGap = 4;
constexpr int kSwCols = 9;

// ---- padnav: the focus tables (ui/pad_tables.h) must match this layout ----
static_assert(pad::hoods::kCount == kNumTeleportHoods && pad::kTabBarY == kTabY,
              "Map tab focus table / tab bar");
static_assert(pad::toon::kNumColors == kNumToonColors && pad::toon::kSwCols == kSwCols && pad::toon::kSwX == kSwX &&
                  pad::toon::kSwY == kSwY && pad::toon::kSwSize == kSwSize && pad::toon::kSwGap == kSwGap,
              "Toon tab focus table");
static_assert(pad::booktab::kCount == Book::kPageCount, "Book tab: one button per page");
// ---- end padnav ----

bool inRect(const touchPosition& t, float x, float y, float w, float h) {
    return t.px >= x && t.px < x + w && t.py >= y && t.py < y + h;
}

u32 colorOf(int idx) {
    const float* c = kToonColors[idx];
    return C2D_Color32f(c[0], c[1], c[2], 1.0f);
}

void parseStatic(C2D_Text* t, C2D_TextBuf buf, const char* str) {
    tt::render::textParse(t, buf, str);
    C2D_TextOptimize(t);
}

} // namespace

bool Ui::init() {
    if (!serviceClerks_.init()) return false; // clerks

    textCache_.clear();
    textBuf_ = C2D_TextBufNew(1024);
    staticBuf_ = C2D_TextBufNew(256);
    debugBuf_ = C2D_TextBufNew(1024);
    if (!gagText_.init(768) || !chatLogText_.init(512) || !laff_.init() || !timer_.init()) return false;
    if (!textBuf_ || !staticBuf_ || !debugBuf_ || !chat_.init() || !shop_.init() || !tailor_.init() ||
        !closet_.init() ||
        !battle_.init() || !quest_.init() || !fishingPanel_.init() || !book_.init()) return false;
    for (int i = 0; i < kTabCount; i++) parseStatic(&tabText_[i], staticBuf_, kTabNames[i]);
    for (int i = 0; i < pad::booktab::kCount; i++) parseStatic(&bookText_[i], staticBuf_, kBookPageNames[i]);
    parseStatic(&bookTitle_, staticBuf_, "Shticker Book");
    parseStatic(&bookHint_, staticBuf_, "Tap a page, or press START anywhere.");
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
    gagText_.shutdown();
    chatLogText_.shutdown();
    laff_.shutdown();
    timer_.shutdown();
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
    tt::render::textParse(&l.text, debugBuf_, buf);
    C2D_TextOptimize(&l.text);
    // Every cached row stays inside the 320 px screen: shrink rather than run off the edge.
    float w = 0.0f;
    C2D_TextGetDimensions(&l.text, scale, scale, &w, nullptr);
    if (w > 0.0f && x + w > kW - 4.0f) scale *= (kW - 4.0f - x) / w;
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
    // The Debug tab leaves the bar when the book's Options page turns debug off.
    if (s.tab < 0 || s.tab >= numTabs(s)) s.tab = kTabMap;
    // SELECT shows / hides the Debug tab (AGENTS.md controls: "SELECT: debug overlay").
    if (down & KEY_SELECT) {
        s.debugOverlay = !s.debugOverlay;
        s.tab = s.debugOverlay ? kTabDebug : kTabMap;
        TT_LOG("ui: debug %s", s.debugOverlay ? "on" : "off");
    }
    // ---- padnav: D-Pad / A / B on a focused Map, Toon or Book tab ----
    if (const u32 padUsed = handleTabPad(down, t, s)) return padUsed;
    // ---- end padnav ----
    if (down & KEY_START) {
        book_.open();
        return KEY_START;
    }
    const int tabs = numTabs(s);
    if (down & KEY_DLEFT) s.tab = (s.tab + tabs - 1) % tabs;
    if (down & KEY_DRIGHT) s.tab = (s.tab + 1) % tabs;
    if (down & KEY_X) s.tab = kTabChat;
    if (down & KEY_Y) s.tab = kTabMap;
    if ((down & KEY_TOUCH) && t.px < kPocketW && t.py >= kPocketTop) {
        s.tab = kTabToon; // the laff pocket: our Toon
    } else if ((down & KEY_TOUCH) && t.py >= kTabY) {
        s.tab = static_cast<int>((t.px - kPocketW) / ((kW - kPocketW) / tabs));
        if (s.tab >= tabs) s.tab = tabs - 1;
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
        return chat_.handleInput(down, held, t, chatBottom());
    }
    if (!(down & KEY_TOUCH) || t.py >= kTabY) return 0;

    switch (s.tab) {
    case kTabMap:
        if (inRect(t, kHomeX, kHomeY, kHomeW, kHomeH)) {
            pressHoodControl(pad::hoods::kIdHome, s);
            break;
        }
        if (t.py >= kHoodRowY && t.py < kHoodRowY + kHoodRowH) {
            const int i = static_cast<int>(t.px / kHoodW);
            if (i >= 0 && i < kNumTeleportHoods) pressHoodControl(i, s);
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
    case kTabBook: {
        using namespace pad::booktab;
        for (int i = 0; i < kCount; i++) {
            if (inRect(t, kX0 + (i % kCols) * kStepX, kY0 + (i / kCols) * kStepY, kBtnW, kBtnH)) {
                book_.open(i);
                break;
            }
        }
        break;
    }
    default:
        break;
    }
    return 0;
}

void Ui::draw(C3D_RenderTarget* target, const State& s, const Context& c) {
    frame_++;
    chatNowMs_ = osGetTime();
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
        fishingPanel_.draw(*c.fishing, 240.0f); // owns the screen: no tab bar under it
        return;
    }
    const int tab = s.tab >= 0 && s.tab < numTabs(s) ? s.tab : kTabMap;
    if (tab != kTabDebug) theme::sky(kW, kTabY);
    switch (tab) {
    case kTabMap: drawMap(c); break;
    case kTabChat: {
        chat_.draw(chatBottom());
        theme::skyBand(chatBottom(), kTabY); // rows scrolled past the bottom stay hidden
        if (chatLogShown()) drawChatLog(kTabY - kChatLogH, kTabY);
        numNearby_ = c.numNearby < 8 ? c.numNearby : 8;
        const char* target = "Everyone";
        for (int i = 0; i < numNearby_; i++) {
            nearbyIds_[i] = c.nearbyIds[i];
            snprintf(nearbyNames_[i], sizeof(nearbyNames_[i]), "%s", c.nearbyNames[i] ? c.nearbyNames[i] : "");
            if (nearbyIds_[i] == s.whisperTo) target = nearbyNames_[i];
        }
        if (numNearby_ > 0) {
            // SpeedChat's whisper target: green while whispering, like a rolled-over SC button.
            theme::button(kW - 100, 1, 96, 26, s.whisperTo ? theme::kScRollover : theme::kScFrame,
                          s.whisperTo ? theme::kGreenDark : theme::kScFrameEdge);
            text(kW - 94, 5, 0.42f, theme::kScText, "To: %.12s", target);
        }
        break;
    }
    case kTabToon: drawToon(s, c); break;
    case kTabGags: drawGags(c); break;
    case kTabBook: drawBookTab(s); break;
    default: drawDebug(c); break;
    }
    // ---- padnav ----
    toonOnline_ = c.quests != nullptr;
    tabFocus_.drawHighlight();
    // ---- end padnav ----
    drawTabs(s, c);
}

// ---- padnav: button focus on the Map / Toon / Book tabs ----
// Nothing is focused until D-Up or D-Down is pressed on one of these tabs, so A still
// jumps, B still walks and D-Left / D-Right still change tabs. While a control is focused
// the D-Pad moves between controls, A presses the focused one and B lets go of the focus.
// Touch keeps its own code below; a touch on a control while focused moves the focus there.
u32 Ui::handleTabPad(u32 down, const touchPosition& t, State& s) {
    switch (s.tab) {
    case kTabMap:
        tabFocus_.setPanelName("tab_map");
        pad::buildHoodRow(tabFocus_, kHomeX);
        break;
    case kTabToon:
        tabFocus_.setPanelName("tab_toon");
        pad::buildToonTab(tabFocus_, toonOnline_);
        break;
    case kTabBook:
        tabFocus_.setPanelName("tab_book");
        pad::buildBookTab(tabFocus_);
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
        pressHoodControl(id, s);
    } else if (s.tab == kTabToon) {
        if (id == pad::toon::kIdBook) {
            book_.open();
        } else if (id >= pad::toon::kIdTarget0 && id < pad::toon::kIdTarget0 + 3) {
            s.colorTarget = id - pad::toon::kIdTarget0;
        } else if (id < kNumToonColors) {
            int& color = s.colorTarget == 0 ? s.bodyColor : s.colorTarget == 1 ? s.shirtColor : s.bottomColor;
            color = id;
        }
    } else if (s.tab == kTabBook) {
        if (id < pad::booktab::kCount) book_.open(id);
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

void Ui::logChat(const char* name, const char* text, bool whisper, bool you, u32 nowMs) {
    if (!text || !text[0]) return;
    if (chatLogCount_ == kChatLog) {
        for (int i = 1; i < kChatLog; i++) chatLog_[i - 1] = chatLog_[i];
        chatLogCount_--;
    }
    ChatLine& l = chatLog_[chatLogCount_++];
    snprintf(l.name, sizeof l.name, "%s", name && name[0] ? name : "Toon");
    snprintf(l.text, sizeof l.text, "%s", text);
    l.ms = nowMs;
    l.whisper = whisper;
    l.you = you;
}

bool Ui::chatLogShown() const {
    return chatLogCount_ > 0 && chatNowMs_ - chatLog_[chatLogCount_ - 1].ms < kChatLogMs;
}

// The SpeedChat menu stops above the recent-chat strip, or above the laff pocket.
float Ui::chatBottom() const { return chatLogShown() ? kTabY - kChatLogH : kPocketTop; }

// Newest line at the bottom, like a chat window: names in nametag blue (ours in green,
// whispers in the whisper panel's purple). It starts right of the laff pocket.
void Ui::drawChatLog(float y0, float y1) {
    const float x0 = kPocketW + 2;
    theme::paper(x0, y0 + 1, kW - 4 - x0, y1 - y0 - 3, 6.0f, theme::kScFrame, theme::kScFrameEdge);
    for (int i = 0; i < chatLogCount_; i++) {
        const ChatLine& l = chatLog_[i];
        const float y = y0 + (y1 - y0 - 3 - chatLogCount_ * 13.5f) * 0.5f + i * 13.5f;
        const u32 nameCol = l.you ? theme::kGreenDark : l.whisper ? theme::rgb(0x80, 0x30, 0xA0) : theme::kBlueDark;
        const float nw = chatLogText_.fit(x0 + 6, y, theme::kTextSmall, nameCol, 100, "%s%s:", l.name,
                                          l.whisper && !l.you ? " whispers" : "");
        chatLogText_.fit(x0 + 10 + nw, y, theme::kTextSmall, theme::kScText, kW - 14 - x0 - nw, "%s", l.text);
    }
}

// Shtiker-Book-style tabs on the binder: the open tab is yellow and stands taller.
void Ui::drawTabs(const State& s, const Context& c) {
    const int tabs = numTabs(s);
    const float tw = (kW - kPocketW) / tabs;
    C2D_DrawRectSolid(0, kTabY, 0.5f, kW, kTabH, theme::kBinderDark);
    C2D_DrawRectSolid(kPocketW, kTabY, 0.5f, kW - kPocketW, 2, theme::kPaperEdge);
    // The laff pocket: the binder rises at its left end and holds our meter.
    theme::roundRect(0, kPocketTop, kPocketW, 240 - kPocketTop + 8, 9, theme::kPaperEdge);
    theme::roundRect(0, kPocketTop + 2, kPocketW - 2, 240 - kPocketTop + 6, 8, theme::kBinderDark);
    theme::roundRect(3, kPocketTop + 5, kPocketW - 8, 240 - kPocketTop, 6, s.tab == kTabToon ? theme::kBinder
                                                                                           : theme::shade(theme::kBinder, 0.8f));
    LaffFace face;
    if (c.stats) {
        face.hp = c.stats->hp;
        face.maxHp = c.stats->maxHp;
    }
    for (int i = 0; i < 3; i++) face.rgb[i] = c.headRgb[i];
    face.species = c.species;
    laff_.draw(face, kPocketW * 0.5f - 1, kPocketTop + (240 - kPocketTop) * 0.5f + 1, kPocketLaff, 0.55f);
    for (int i = 0; i < tabs; i++) {
        const bool on = i == s.tab;
        const float x = kPocketW + i * tw + 2, y = on ? kTabY + 2 : kTabY + 5, h = kTabH - (on ? 4 : 7);
        const u32 face = on ? theme::kYellow : i == kTabDebug ? kPanel : theme::kBinder;
        theme::button(x, y, tw - 4, h, face, on ? theme::kYellowDark : theme::shade(face, 0.6f), false, 5.0f);
        theme::textCentered(tabText_[i], x + (tw - 4) * 0.5f, y + (on ? 4.0f : 3.0f), on ? 0.56f : 0.5f,
                            on ? theme::kInk : theme::kPaper, tw - 10);
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
    if (!f) {
        TT_LOG("map: no %s (errno %d): markers only", path, errno);
        return;
    }
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
    MapSource map;
    if (hasMap_) {
        map.image = &mapImage_;
        map.minX = mapMinX_;
        map.minY = mapMinY_;
        map.maxX = mapMaxX_;
        map.maxY = mapMaxY_;
        map.w = mapW_;
        map.h = mapH_;
    }
    drawMapFrame(c, map, 4, 4, kW - 8, kHoodRowY - 8);
    drawHoodControls(c, kHomeX);
}

void Ui::drawToon(const State& s, const Context& c) {
    theme::paper(4, 4, kW - 8, kTabY - 8, 8.0f);
    if (c.quests) { // online: the server's DNA wins, so the tab shows our ToonTasks instead
        // Our name, laff and jellybeans (the laff meter itself is in the tab bar's pocket).
        C2D_Text name;
        textCache_.parse(&name, textBuf_, c.toonName && c.toonName[0] ? c.toonName : "Toon", 1024);
        theme::textFit(name, 14, 9, 0.72f, theme::kInk, kW - 28);
        float top = 38;
        if (c.stats) {
            // A few jellybeans in upstream's bean colours, then the count.
            static constexpr u32 kBeans[3] = {theme::rgb(0xE8, 0x3A, 0x6A), theme::rgb(0x6C, 0xC6, 0x3C),
                                              theme::rgb(0xF6, 0xB0, 0x22)};
            for (int i = 0; i < 3; i++) theme::roundRect(14 + i * 7, top + 4 + (i & 1) * 3, 9, 6, 3, kBeans[i]);
            text(40, top, .48f, theme::kInk, "Jellybeans  %d / %d", c.stats->money, c.stats->maxMoney);
            text(196, top, .48f, theme::kInk, "Laff  %d / %d", c.stats->hp > 0 ? c.stats->hp : 0, c.stats->maxHp);
            top += 18;
        }
        // ---- clerks: show only the AI's current pet association ----
        if (c.clerkProfile) {
            const char* petName = c.clerkProfile->petId
                ? (c.petName ? c.petName : "Owned (at estate)") : "None";
            text(14, top, .42f, theme::kInkSoft, "Doodle: %s", petName);
            top += 16;
        }
        C2D_DrawRectSolid(12, top + 2, 0.5f, kW - 24, 1, theme::kPaperShade);
        quest_.drawTasks(*c.quests, top + 6, 172);
        // ---- end clerks ----
        theme::button(pad::toon::kBookX, pad::toon::kBookY, pad::toon::kBookW, pad::toon::kBookH + 2,
                      theme::kOrange, theme::shade(theme::kOrange, 0.65f));
        text(pad::toon::kBookX + 18, pad::toon::kBookY + 3, 0.48f, theme::kWhite, "Open Shticker Book (START)");
        return;
    }
    static const char* const kTargets[3] = {"Body", "Shirt", "Shorts"};
    for (int i = 0; i < 3; i++) {
        const bool on = i == s.colorTarget;
        theme::button(8 + i * 102, 8, 98, 30, on ? theme::kYellow : theme::kBlue,
                      on ? theme::kYellowDark : theme::kBlueDark);
        text(16 + i * 102, 12, 0.52f, on ? theme::kInk : theme::kWhite, "%s", kTargets[i]);
    }
    const int sel = s.colorTarget == 0 ? s.bodyColor : s.colorTarget == 1 ? s.shirtColor : s.bottomColor;
    for (int i = 0; i < kNumToonColors; i++) {
        const float x = kSwX + (i % kSwCols) * (kSwSize + kSwGap);
        const float y = kSwY + (i / kSwCols) * (kSwSize + kSwGap);
        if (i == sel) theme::roundRect(x - 3, y - 3, kSwSize + 6, kSwSize + 6, 7, theme::kRed);
        theme::roundRect(x, y, kSwSize, kSwSize, 6, theme::kPaperEdge);
        theme::roundRect(x + 1, y + 1, kSwSize - 2, kSwSize - 2, 5, colorOf(i));
    }
    text(10, 152, 0.46f, theme::kInk, "Tap a color. Head, arms and legs share the body color.");
    text(10, 172, 0.44f, theme::kInkSoft, "A: jump   B: walk   L/R: turn the camera");
}

// The Shtiker Book's cover: one button per page (upstream opens the book with its HUD button).
void Ui::drawBookTab(const State&) {
    using namespace pad::booktab;
    theme::roundRect(4, 4, kW - 8, kTabY - 8, 10, theme::kBinderDark);
    theme::roundRect(7, 7, kW - 14, kTabY - 14, 8, theme::kBinder);
    theme::textCentered(bookTitle_, kW * 0.5f, 10, 0.78f, theme::kYellow);
    for (int i = 0; i < kCount; i++) {
        const float x = kX0 + (i % kCols) * kStepX, y = kY0 + (i / kCols) * kStepY;
        theme::button(x, y, kBtnW, kBtnH, theme::kPaper, theme::kPaperEdge);
        theme::textCentered(bookText_[i], x + kBtnW * 0.5f, y + 8, 0.52f, theme::kInk, kBtnW - 10);
    }
    theme::textCentered(bookHint_, kW * 0.5f, 178, 0.44f, theme::kPaperShade, kW - 24);
}

void Ui::drawInfo(const Context &c) {
    // Minigame rules, loading notes and ride waits: a paper card on the sky, like upstream's
    // DirectGui frames.
    theme::sky();
    theme::paper(4, 4, kW - 8, 232, 8.0f);
    // ---- racing2 ----
    if (c.racing) {
        const auto &race = *c.racing;
        text(12, 8, .6f, theme::kRedDark, "%s", c.infoTitle);
        for (int i = 0; i < 3; ++i)
            text(12, 32 + i * 14, .38f, theme::kInk, "%s", c.infoLines[i] ? c.infoLines[i] : "");
        auto x = [&](float value) { return 14 + (value - race.minX) / race.spanX * 140; };
        auto y = [&](float value) { return 210 - (value - race.minY) / race.spanY * 112; };
        for (int i = 0; i < 64; ++i) {
            const int next = (i + 1) % 64;
            C2D_DrawLine(x(race.curve[i][0]), y(race.curve[i][1]), theme::kInkSoft, x(race.curve[next][0]),
                         y(race.curve[next][1]), theme::kInkSoft, 3, .5f);
        }
        for (int i = 0; i < race.count; ++i) {
            const u32 color = i == 0 ? theme::kRed : theme::kBlue;
            C2D_DrawRectSolid(x(race.racers[i][0]) - 3, y(race.racers[i][1]) - 3, .6f, 6, 6, color);
            text(170, 96 + i * 24, .48f, i == 0 ? theme::kRedDark : theme::kInk, "%s", race.names[i]);
        }
        return;
    }
    // ---- end racing2 ----
    C2D_Text title;
    textCache_.parse(&title, textBuf_, c.infoTitle && c.infoTitle[0] ? c.infoTitle : "Just a moment...", 1024);
    theme::textCentered(title, kW * 0.5f, 14, 0.72f, theme::kRedDark, kW - 28);
    C2D_DrawRectSolid(24, 44, 0.5f, kW - 48, 2, theme::kPaperShade);
    // A countdown in the lines ("Time: 54") shows as the ToontownTimer clock under them.
    int secondsLeft = -1;
    for (int i = 0; i < 3; i++) {
        if (!c.infoLines[i] || !c.infoLines[i][0]) continue;
        char shown[96];
        const int secs = splitTimeLeft(c.infoLines[i], shown, sizeof shown);
        if (secs >= 0) secondsLeft = secs;
        if (!shown[0]) continue;
        C2D_Text line;
        textCache_.parse(&line, textBuf_, shown, 1024);
        theme::textCentered(line, kW * 0.5f, 62 + i * 30, 0.54f, theme::kInk, kW - 28);
    }
    if (secondsLeft >= 0) timer_.draw(secondsLeft, kW * 0.5f, 186, 88);
}

void Ui::drawGags(const Context &c) {
    // The Shtiker Book's Gags page on the sky: the gag table, the carry count and jellybeans.
    theme::paper(4, 4, kW - 8, kTabY - 8, 8.0f);
    if (!c.gagCount) {
        gagText_.centered(kW * 0.5f, 84, theme::kTextBody, theme::kInk, kW - 32, "Your gags appear here");
        gagText_.centered(kW * 0.5f, 104, theme::kTextBody, theme::kInk, kW - 32, "once you are online.");
        return;
    }
    int total = 0;
    for (int t = 0; t < gags::kTracks; t++)
        for (int l = 0; l < gags::kLevels; l++) total += c.gagCount[t][l];
    gagText_.draw(12, 8, theme::kTextBody, theme::kInk, "Gags  %d / %d", total, c.maxCarry);
    if (c.stats) {
        static constexpr u32 kBeans[3] = {theme::rgb(0xE8, 0x3A, 0x6A), theme::rgb(0x6C, 0xC6, 0x3C),
                                          theme::rgb(0xF6, 0xB0, 0x22)};
        const float bw = gagText_.right(kW - 12, 8, theme::kTextBody, theme::rgb(0x80, 0x50, 0x00), 0, "%d",
                                        c.stats->money);
        for (int i = 0; i < 3; i++) theme::roundRect(kW - 46 - bw + i * 7, 12 + (i & 1) * 3, 9, 6, 3, kBeans[i]);
    }
    GagTableData d;
    d.count = c.gagCount;
    d.exp = c.gagExp;
    d.access = c.trackAccess;
    if (c.book) {
        d.trainingTrack = c.book->trackProgressId;
        d.trainingBits = c.book->trackProgress;
    }
    drawGagTable(gagText_, d, 9, 27, kW - 18, 21.0f); // seven rows end above the laff pocket
}

void Ui::drawDebug(const Context& c) {
    C2D_DrawRectSolid(0, 0, 0.5f, kW, kTabY, kBg);
    if (numDebugLines_ == 0 || frame_ % kDebugRefreshFrames == 0) {
        const u32 good = C2D_Color32(0x8C, 0xF0, 0x8C, 0xFF);
        const u32 bad = C2D_Color32(0xFF, 0x80, 0x80, 0xFF);
        C2D_TextBufClear(debugBuf_);
        numDebugLines_ = 0;
        debugLine(8, 4, 0.5f, kWhite, "Debug  (%s)   SELECT hides", c.isNew3ds ? "New 3DS" : "Old 3DS");
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
            debugLine(8, 114, 0.45f, kDim, "net offline");
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
        debugLine(8, 196, 0.32f, kDim, "%.68s", log::recent(0));
        // ---- end qafix ----
    }
    for (int i = 0; i < numDebugLines_; i++) {
        const CachedLine& l = debugLines_[i];
        C2D_DrawText(&l.text, C2D_WithColor, l.x, l.y, 0.5f, l.scale, l.scale, l.color);
    }
}

} // namespace tt::ui
// main() keeps its Ui on the 256 KB main-thread stack (AGENTS.md). Book pages once embedded a
// 28 KB TextCache each and overflowed it (PC 0 at boot); keep big caches on the heap.
static_assert(sizeof(tt::ui::Ui) <= 112 * 1024, "Ui grew: move large members to the heap");
