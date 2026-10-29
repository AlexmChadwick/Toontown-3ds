#include "ui/racing_view.h" // ---- racing2 ----
#include "ui/panel_owner.h" // qafix
#include "render/text_cache.h"
#include "ui/page_text.h"
// Bottom-screen UI (citro2d): tab bar + Map / Chat / Toon / Gags / Book panels, and the Debug
// panel (SELECT or the book's Options page; docs/design/ui_theme.md).
#pragma once

#include <3ds.h>
#include <citro2d.h>

#include "render/model.h"
#include "ui/chat_menu.h"
#include "ui/battle_panel.h"
#include "ui/quest_panel.h"
#include "ui/shop_panel.h"
#include "ui/service_clerk_panel.h" // clerks
#include "ui/tailor_panel.h"
#include "ui/fishing_panel.h"
#include "ui/closet_panel.h"
#include "ui/minigame_purchase_panel.h"
#include "ui/book.h"
#include "ui/laff_meter.h"
#include "ui/toontown_timer.h"

namespace tt::ui {

enum Tab : int { kTabMap = 0, kTabChat, kTabToon, kTabGags, kTabBook, kTabDebug, kTabCount };
// The Debug tab is in the bar only while State::debugOverlay is on.
constexpr int kNumPlayerTabs = kTabDebug;

// Persistent UI-controlled settings the game reads each frame.
struct State {
    int tab = kTabMap;
    bool stereo = false; // opt-in; AGENTS.md display rules
    int stressToons = 0; // extra toon instances for the M1.6 stress test
    int colorTarget = 0; // 0 body, 1 shirt, 2 shorts
    int bodyColor = 17;  // index into kToonColors (ToonDNA.allColorsList)
    int shirtColor = 2;
    int bottomColor = 18;
    // Map tab teleport: hood zone id the player tapped (main clears it), 0 = none.
    unsigned teleportRequest = 0;
    bool teleportFromMap = false; // the request came from the Map: send checkTeleportAccess first
    bool homeRequest = false;
    // Chat tab: SpeedChat goes to this toon as a whisper (0 = say it out loud).
    unsigned whisperTo = 0;
    bool debugOverlay = false;
    int lodBias = 0;
    int musicVol = 80;
    int sfxVol = 100;
    bool quitRequested = false;
};

struct MapMarker {
    float x, y;
    u32 color;
};

// Read-only per-frame data for the panels.
struct Context {
    float playerX, playerY, playerHeadingDeg;
    float zoneMinX, zoneMinY, zoneMaxX, zoneMaxY;
    const MapMarker* markers;
    int numMarkers;
    // Indoors the map shows the place outside (its markers, the arrow at the building's door).
    bool mapOutside = false;
    bool mapNoArrow = false; // ...when we did not come in from there, without our arrow
    // Teleport row on the Map tab: which hoods are installed (bit i = kTeleportHoods[i]).
    unsigned hoodsAvailable = 0;
    // setTeleportAccess / setHoodsVisited in the same bit order (offline: every hood).
    unsigned hoodsTeleport = ~0u;
    unsigned hoodsVisited = ~0u;
    bool homeAvailable = false; // the server has an EstateManager
    unsigned currentHood = 0;
    unsigned currentZone = 0;
    // Debug
    float fps, cpuMs, gpuMs, drawMs;
    unsigned drawCalls, triangles, modelsDrawn, modelsCulled;
    unsigned heapUsedKB, heapBudgetKB, linearFreeKB, linearBudgetKB, vramFreeKB;
    bool isNew3ds;
    // Network (Debug tab); netState null = offline.
    const char* netState = nullptr;
    unsigned zoneId = 0;
    unsigned netInBps = 0, netOutBps = 0;
    int remoteToons = 0;
    // Toons in view, for the Chat tab's whisper target (name pointers stay valid for the frame).
    int numNearby = 0;
    unsigned nearbyIds[8] = {};
    const char* nearbyNames[8] = {};
    // Toon tab: our name and laff meter face (online only).
    const char* toonName = nullptr;
    float headRgb[3] = {0.7f, 0.7f, 0.8f};
    int species = 0;
    // Our ToonTasks (Toon tab task list); null = offline.
    const net::OwnQuests* quests = nullptr;
    // A full-screen info panel (minigames): title plus up to 3 lines; null = none.
    const RacingView* racing = nullptr; // ---- racing2 ----
    const char* infoTitle = nullptr;
    const char* infoLines[3] = {};
    // Gags tab (owner view of our toon); null = offline.
    const uint16_t* gagExp = nullptr;            // [7]
    const uint8_t (*gagCount)[7] = nullptr;      // [7][7]
    const uint8_t* trackAccess = nullptr;        // [7]
    int maxCarry = 0;
    // Fishing: non-null while the toon is fishing; replaces the tab content (ui/fishing_panel.h).
    const FishingView* fishing = nullptr;
    // Shtiker Book data (null = offline)
    const net::ClerkProfile* clerkProfile = nullptr; // clerks
    const char* petName = nullptr; // clerks
    const net::OwnStats* stats = nullptr;
    const net::OwnBookStats* book = nullptr;
    const net::OwnFish* fish = nullptr;
    const net::District* districts = nullptr;
    int numDistricts = 0;
};

constexpr int kNumTeleportHoods = 8;
extern const unsigned kTeleportHoods[kNumTeleportHoods];

constexpr int kNumToonColors = 27;
// toontown/toon/ToonDNA.py allColorsList (RGB).
extern const float kToonColors[kNumToonColors][3];

class Ui {
public:
    bool init();
    void shutdown();
    // Tab switching (touch, D-pad left/right, X = chat, Y = map) and panel touches.
    // Returns the buttons the UI consumed this frame; clear them from the world's input
    // (`in.down &= ~ui.handleInput(...)`) so A/B used by the Chat tab do not also jump.
    // keysHeld is needed for touch drags/taps; the 3-argument form reads hidKeysHeld().
    u32 handleInput(u32 keysDown, u32 keysHeld, const touchPosition& touch, State& state);
    u32 handleInput(u32 keysDown, const touchPosition& touch, State& state);
    // Oldest SpeedChat phrase the player chose: send DistributedPlayer.setSC(uint16 msgIndex).
    void setEmoteAccess(const uint8_t* access) { chat_.setEmoteAccess(access); }
    bool popSpeech(int& msgIndex) { return chat_.popSpeech(msgIndex); }
    // SpeedChat TOONTASKS > MY TASKS: rebuilt when our quests change (null = offline).
    void setChatTasks(const net::OwnQuests* quests) { chat_.setTasks(quests); }
    // A popSpeech value >= ChatMenu::kTaskCode is a ToonTask phrase.
    const ChatMenu::TaskPhrase* chatTaskPhrase(int code) const { return chat_.taskPhrase(code); }
    // Gag shop panel (docs/design/shop.md): while active it owns the whole bottom screen.
    ServiceClerkPanel& serviceClerks() { return serviceClerks_; } // clerks
    ShopPanel& shop() { return shop_; }
    TailorPanel& tailor() { return tailor_; }
    ClosetPanel& closet() { return closet_; }
    Book& book() { return book_; }
    const Book& book() const { return book_; }
    void openBookPage(int page) { book_.open(page); }
    // Street battle (world/battle.*): while a view is set the battle panel owns the bottom
    // screen. Call every frame before handleInput (null when not fighting).
    void setBattle(const world::BattleView* view, const BattleGags& gags) {
        battleView_ = view;
        battleGags_ = gags;
    }
    BattlePanel& battle() { return battle_; }
    // NPC conversation (quest::Talk): while it is locked the quest panel owns the bottom screen.
    void setTalk(quest::Talk* talk) { talk_ = talk; }
    // ---- qafix: a single owner for both input and drawing ----
    void setRacingInput(bool racing) { contextRacing_ = racing; } // ---- racing2 ----
    void syncContext(net::Client& client, unsigned zone, bool lost, bool exclusive,
                     bool fishing, bool info);
    // ---- end qafix ----
    void draw(C3D_RenderTarget* target, const State& state, const Context& ctx);
    // Map tab image: call when the zone changes (or every frame, it is a no-op for the same
    // id) with the zone *file* id (street cells collapse to the branch, 2101 -> 2100; 0 or an
    // interior = none). Loads sdmc:/3ds/toontown3ds/data/zones/<id>.tmap (bake_minimap.py);
    // if absent the Map tab keeps its marker-only view. Main thread only (allocates linear).
    void setMapZone(unsigned fileZone);
    // The last drawn bottom screen showed our laff meter (the tab bar's pocket or the battle
    // panel's Toon cards), so the top HUD leaves it off the world.
    bool laffShown() const { return lastOwner_ == PanelOwner::Tabs || lastOwner_ == PanelOwner::Battle; }
    // The Chat tab's recent-chat strip: what was said nearby, by us, or whispered to us.
    // `you` marks our own lines; nothing is drawn after kChatLogMs without new chat.
    void logChat(const char* name, const char* text, bool whisper, bool you, u32 nowMs);

private:
    // ---- qafix ----
    PanelOwner resolveOwner(bool fishing, bool info);
public:
    // The fishing panel's touch gesture this frame (consumed by world::Fishing).
    FishingPanel& fishingPanel() { return fishingPanel_; }
private:
    PanelOwner lastOwner_ = PanelOwner::Tabs;
    bool drawnInfo_ = false; // the last drawn frame showed an info panel (ctx.infoTitle)
    unsigned contextZone_ = 0;
    bool contextFishing_ = false, contextInfo_ = false;
    bool contextRacing_ = false; // ---- racing2 ----
    // ---- end qafix ----
    struct CachedLine {
        C2D_Text text;
        float x, y, scale;
        u32 color;
    };
    static constexpr int kMaxDebugLines = 72; // the Gags tab caches one label per gag
    static constexpr int kDebugRefreshFrames = 10; // text parsing is ~ms of CPU; don't do it every frame

    void text(float x, float y, float scale, u32 color, const char* fmt, ...) __attribute__((format(printf, 6, 7)));
    void debugLine(float x, float y, float scale, u32 color, const char* fmt, ...)
        __attribute__((format(printf, 6, 7)));
    void drawTabs(const State& s, const Context& c);
    float chatBottom() const;
    void drawMap(const Context& c);
    void drawToon(const State& s, const Context& c);
    void drawBookTab(const State& s);
    void drawGags(const Context& c);
    void drawInfo(const Context& c);
    void drawDebug(const Context& c);

    void unloadMap();

    // Baked minimap (.tmap): one ETC1 texture + the world rectangle it covers.
    C3D_Tex mapTex_{};
    Tex3DS_SubTexture mapSub_{};
    C2D_Image mapImage_{};
    bool hasMap_ = false;
    unsigned mapZone_ = 0xFFFFFFFFu;
    float mapMinX_ = 0, mapMinY_ = 0, mapMaxX_ = 1, mapMaxY_ = 1;
    unsigned mapW_ = 0, mapH_ = 0;

    render::TextCache textCache_;
    PageText gagText_; // the Gags tab's table (cached per string)
    PageText chatLogText_;
    struct ChatLine {
        char name[24];
        char text[96];
        u32 ms;
        bool whisper, you;
    };
    static constexpr int kChatLog = 2;
    static constexpr u32 kChatLogMs = 60000;
    ChatLine chatLog_[kChatLog] = {};
    int chatLogCount_ = 0;
    u32 chatNowMs_ = 0;
    bool chatLogShown() const;
    void drawChatLog(float y0, float y1);
    C2D_TextBuf textBuf_ = nullptr;   // per-frame text
    C2D_TextBuf staticBuf_ = nullptr; // parsed once (tab labels)
    C2D_TextBuf debugBuf_ = nullptr;  // re-parsed every kDebugRefreshFrames
    C2D_Text tabText_[kTabCount];
    C2D_Text bookText_[9]; // Book tab page buttons
    C2D_Text bookTitle_, bookHint_;
    LaffDisplay laff_; // the tab bar's laff pocket
    ToontownTimer timer_; // minigame countdowns on the info card
    CachedLine debugLines_[kMaxDebugLines];
    int numDebugLines_ = 0;
    unsigned frame_ = 0;
    int lastTab_ = -1;
    ChatMenu chat_;
    ServiceClerkPanel serviceClerks_; // clerks
    ShopPanel shop_;
    TailorPanel tailor_;
    ClosetPanel closet_;
    BattlePanel battle_;
    QuestPanel quest_;
    // Whisper targets seen at the last draw (handleInput runs before draw each frame).
    int numNearby_ = 0;
    unsigned nearbyIds_[8] = {};
    char nearbyNames_[8][24] = {};
    quest::Talk* talk_ = nullptr;
    const world::BattleView* battleView_ = nullptr;
    BattleGags battleGags_{};
    FishingPanel fishingPanel_;
    Book book_;
    // ---- padnav: button focus on the Map / Toon / Setup tabs (docs/design/pad_navigation.md) ----
    u32 handleTabPad(u32 down, const touchPosition& touch, State& s);
    void activateTabControl(int id, State& s);
    void cycleWhisper(State& s);
    int numTabs(const State& s) const { return s.debugOverlay ? kTabCount : kNumPlayerTabs; }
    FocusGroup tabFocus_{"tab"};
    bool toonOnline_ = false; // the Toon tab's last drawn layout (ToonTasks + book, or colours)
    // ---- end padnav ----
};

} // namespace tt::ui
