// Top-screen HUD (citro2d, drawn after the 3D pass): laff meter and jellybean jar.
#pragma once

#include "render/model.h"
#include "ui/laff_meter.h"
#include "ui/toontown_timer.h"

#include <3ds.h>
#include <citro2d.h>

namespace tt::ui {

struct FishingView; // ui/fishing_panel.h

// RaceGUI.initRaceMode and DistributedVehicle's speedometer, filled by world::Racing::fillHud.
struct RaceHud {
    static constexpr int kMaxRacers = 4, kMaxLaps = 8;
    bool active = false;      // the race is running (the HUD shows; the laff meter does not)
    bool started = false;     // the timer runs (RaceGUI.setTimerEnabled)
    int speed = 0;            // speedometer text: int(curSpeed / 3)
    float needle = 0;         // speedGauge.setR, degrees 0..110
    int lap = 1, laps = 3;    // lapLabel "<lap>/<laps>"
    int place = 1;            // placeLabelNum + suffix
    bool wrongWay = false;
    bool photoFinish = false; // "Photo Finish!" replaces the place label
    int lapNotice = 0;        // centred for 2 s: n = "Lap n", -1 = "Final Lap!"
    int gag = 0;              // RaceGUI.gagTextures index (0 = none)
    int lapsShown = 1;        // time labels shown (maxLapHit + 1)
    float lapSeconds[kMaxLaps] = {};
    float body[3] = {1, 1, 1}, accessory[3] = {1, 1, 1}; // local kart colours (KartDNA)
    float mapScale = 1;       // RaceGlobals.TrackDict[track][6]
    int curveCount = 0;
    float curve[101][2] = {}; // the map: the curve at 101 even t, world x / y
    struct Racer {
        float progress = 0;   // curvetime / lapCount, 0..1 along the progress line
        float x = 0, y = 0;   // map spot (world)
        float body[3] = {1, 1, 1};
        float head[3] = {1, 1, 1};
        int species = 0;
        bool local = false;
    } racers[kMaxRacers];
    int racerCount = 0;
};

struct HudValues {
    int hp = 15, maxHp = 15;
    int money = 0, maxMoney = 40;
    float headRgb[3] = {0.7f, 0.7f, 0.8f}; // laff meter face colour (toon head colour)
    int species = 0;                         // world::Species: which laff meter head
    bool online = false;
    bool laffOnBottom = false; // the bottom screen shows the meter: leave it off the world
    bool laffHidden = false;   // the Toontorial obscures the meter until Tom's lesson shows it
    float laffGrow = 0.0f;     // Tom's Laff meter lesson: 0 in its corner .. 1 big in the middle
    const char* toast = nullptr; // short centred message ("Coming soon...", "Loading...")
    bool racing = false;
    RaceHud race; // RaceGUI's race mode, while race.active
    bool photoViewfinder = false; // Photo Fun: the tripod camera's viewfinder frame and sight
    int timerSeconds = -1;       // TownBattle's and Purchase's ToontownTimer, top right, -1 = hidden
    const FishingView* fishing = nullptr; // the fishing spot's catch (FishPanel / itemGui over the world)
    // A facility's showInfoText (DistributedCountryClub: sign font, white, black shadow, at
    // aspect2d (0, -0.5)) and its fade, 1 = opaque.
    const char* infoText = nullptr;
    float infoAlpha = 1.0f;
    const char* gameLabel = nullptr; // a room game's frame2D label (Moles Left: n, Maze Race!)
    int timerBelowSeconds = -1; // ToontownTimer.posBelowTopRightCorner (mole field, maze), -1 = hidden
    // Bust-a-Cog's frame2D quit button (Exit for Now, B). Its timerLabel stays hidden in stock.
    bool golfExit = false;
};

class Hud {
public:
    bool init();
    void shutdown();
    void draw(C3D_RenderTarget* top, const HudValues& v);

private:
    void drawRace(const RaceHud& r);
    void drawFishCatch(const FishingView& f);
    C2D_TextBuf raceBuf_ = nullptr;
    C2D_Text toastText_{};
    LaffDisplay laff_;
    ToontownTimer battleTimer_;
    const char* lastToast_ = nullptr;
    char lastToastText_[160] = {};
    C2D_TextBuf toastBuf_ = nullptr;
    C2D_TextBuf fishBuf_ = nullptr;
    C2D_TextBuf infoBuf_ = nullptr; // info line and Moles Left, re-parsed when the text changes
    C2D_Text infoTextC2d_{}, molesTextC2d_{};
    char infoLast_[64] = {}, labelLast_[32] = {};
    C2D_TextBuf golfBuf_ = nullptr;
    C2D_Text golfExit_{};
    C2D_Text fishText_[5]{};
    unsigned fishRevision_ = ~0u;
};

} // namespace tt::ui
