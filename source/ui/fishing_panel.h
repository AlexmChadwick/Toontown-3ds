// Bottom-screen fishing panel (citro2d): replaces the tab content while the toon is fishing.
// Upstream shows this on the 3D screen as the fishingGui overlay (cast button, power arrow,
// bucket/jar counters, "You caught:" frame); here it uses the touch screen
// (docs/design/fishing.md section 8). source/world/fishing.* fills a FishingView each frame.
#pragma once

#include <3ds.h>
#include <citro2d.h>

namespace tt::ui {

struct FishingView {
    enum Phase : u8 { kEntering, kReady, kCharging, kCasting, kWaiting, kReeling, kResult, kLeaving };
    enum Result : u8 { kNone, kFish, kBeans, kBoot, kNothing, kTankFull, kBroke };

    Phase phase = kEntering;
    Result result = kNone;
    bool practice = false;   // local rules: the server offers no fishing spots
    float power = 0.0f;      // 0..1 while charging (and the cast just made)
    float aim = 0.0f;        // degrees, positive = left, within +-FishGlobals.FishingAngleMax
    float aimMax = 50.0f;
    int rod = 0;
    int tank = 0, tankMax = 20;
    int beans = -1;          // jellybean jar; -1 = unknown (offline)
    bool newEntry = false;   // FishItemNewEntry / NewRecord
    float timeoutFrac = 0.0f; // fraction of the cast timeout used while waiting for a bite (0..1)
    // Text. Bump `revision` whenever any of it changes (the panel re-parses only then).
    unsigned revision = 0;
    char status[72] = "";
    char itemName[56] = "";
    char itemWeight[40] = "";
    char itemValue[40] = "";
};

class FishingPanel {
public:
    bool init();
    void shutdown();
    // Draws into the current citro2d scene; y range [0, contentHeight).
    void draw(const FishingView& v, float contentHeight);

private:
    static constexpr int kMaxLines = 12;
    void reparse(const FishingView& v);

    C2D_TextBuf buf_ = nullptr;
    C2D_Text lines_[kMaxLines];
    int numLines_ = 0;
    unsigned revision_ = 0xFFFFFFFFu;
    int lastTank_ = -1, lastTankMax_ = -1, lastBeans_ = -2, lastRod_ = -1;
    bool lastPractice_ = false;
    FishingView::Result lastResult_ = FishingView::kNone;
};

} // namespace tt::ui
