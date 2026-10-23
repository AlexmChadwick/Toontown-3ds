// Bottom-screen UI (citro2d): tab bar + Map / Chat / Toon / Settings / Debug panels.
#pragma once

#include <3ds.h>
#include <citro2d.h>

namespace tt::ui {

enum Tab : int { kTabMap = 0, kTabChat, kTabToon, kTabSettings, kTabDebug, kTabCount };

// Persistent UI-controlled settings the game reads each frame.
struct State {
    int tab = kTabDebug;
    bool stereo = false; // opt-in; AGENTS.md display rules
    int stressToons = 0; // extra toon instances for the M1.6 stress test
    int colorTarget = 0; // 0 body, 1 shirt, 2 shorts
    int bodyColor = 17;  // index into kToonColors (ToonDNA.allColorsList)
    int shirtColor = 2;
    int bottomColor = 18;
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
    // Debug
    float fps, cpuMs, gpuMs, drawMs;
    unsigned drawCalls, triangles, modelsDrawn, modelsCulled;
    unsigned heapUsedKB, heapBudgetKB, linearFreeKB, linearBudgetKB, vramFreeKB;
    bool isNew3ds;
};

constexpr int kNumToonColors = 27;
// toontown/toon/ToonDNA.py allColorsList (RGB).
extern const float kToonColors[kNumToonColors][3];

class Ui {
public:
    bool init();
    void shutdown();
    // Tab switching (touch, D-pad left/right, X = chat, Y = map) and panel touches.
    void handleInput(u32 keysDown, const touchPosition& touch, State& state);
    void draw(C3D_RenderTarget* target, const State& state, const Context& ctx);

private:
    struct CachedLine {
        C2D_Text text;
        float x, y, scale;
        u32 color;
    };
    static constexpr int kMaxDebugLines = 16;
    static constexpr int kDebugRefreshFrames = 10; // text parsing is ~ms of CPU; don't do it every frame

    void text(float x, float y, float scale, u32 color, const char* fmt, ...) __attribute__((format(printf, 6, 7)));
    void debugLine(float x, float y, float scale, u32 color, const char* fmt, ...)
        __attribute__((format(printf, 6, 7)));
    void drawTabs(const State& s);
    void drawMap(const Context& c);
    void drawChat();
    void drawToon(const State& s);
    void drawSettings(const State& s);
    void drawDebug(const Context& c);

    C2D_TextBuf textBuf_ = nullptr;   // per-frame text
    C2D_TextBuf staticBuf_ = nullptr; // parsed once (tab labels)
    C2D_TextBuf debugBuf_ = nullptr;  // re-parsed every kDebugRefreshFrames
    C2D_Text tabText_[kTabCount];
    CachedLine debugLines_[kMaxDebugLines];
    int numDebugLines_ = 0;
    unsigned frame_ = 0;
};

} // namespace tt::ui
