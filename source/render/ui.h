// Bottom-screen UI (citro2d): tab bar + Map / Chat / Toon / Gags / Settings / Debug panels.
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

namespace tt::ui {

enum Tab : int { kTabMap = 0, kTabChat, kTabToon, kTabGags, kTabSettings, kTabDebug, kTabCount };

// Persistent UI-controlled settings the game reads each frame.
struct State {
    int tab = kTabDebug;
    bool stereo = false; // opt-in; AGENTS.md display rules
    int stressToons = 0; // extra toon instances for the M1.6 stress test
    int colorTarget = 0; // 0 body, 1 shirt, 2 shorts
    int bodyColor = 17;  // index into kToonColors (ToonDNA.allColorsList)
    int shirtColor = 2;
    int bottomColor = 18;
    // Map tab teleport: hood zone id the player tapped (main clears it), 0 = none.
    unsigned teleportRequest = 0;
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
    // Teleport row on the Map tab: which hoods are installed (bit i = kTeleportHoods[i]).
    unsigned hoodsAvailable = 0;
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
    // Our ToonTasks (Toon tab task list); null = offline.
    const net::OwnQuests* quests = nullptr;
    // A full-screen info panel (minigames): title plus up to 3 lines; null = none.
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
    void draw(C3D_RenderTarget* target, const State& state, const Context& ctx);
    // Map tab image: call when the zone changes (or every frame, it is a no-op for the same
    // id) with the zone *file* id (street cells collapse to the branch, 2101 -> 2100; 0 or an
    // interior = none). Loads sdmc:/3ds/toontown3ds/data/zones/<id>.tmap (bake_minimap.py);
    // if absent the Map tab keeps its marker-only view. Main thread only (allocates linear).
    void setMapZone(unsigned fileZone);

private:
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
    void drawTabs(const State& s);
    void drawMap(const Context& c);
    void drawToon(const State& s, const Context& c);
    void drawSettings(const State& s);
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

    C2D_TextBuf textBuf_ = nullptr;   // per-frame text
    C2D_TextBuf staticBuf_ = nullptr; // parsed once (tab labels)
    C2D_TextBuf debugBuf_ = nullptr;  // re-parsed every kDebugRefreshFrames
    C2D_Text tabText_[kTabCount];
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
};

} // namespace tt::ui
