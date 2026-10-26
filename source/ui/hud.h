// Top-screen HUD (citro2d, drawn after the 3D pass): laff meter and jellybean jar.
#pragma once

#include "render/model.h"

#include <3ds.h>
#include <citro2d.h>

namespace tt::ui {

struct HudValues {
    int hp = 15, maxHp = 15;
    int money = 0, maxMoney = 40;
    float headRgb[3] = {0.7f, 0.7f, 0.8f}; // laff meter face colour (toon head colour)
    int species = 0;                         // world::Species: which laff meter head
    bool online = false;
    const char* toast = nullptr; // short centred message ("Coming soon...", "Loading...")
    bool racing = false;
    int raceSpeed = 0, raceLap = 0, raceLaps = 0, racePlace = 0;
    bool photoViewfinder = false; // Photo Fun: the tripod camera's viewfinder frame and sight
};

class Hud {
public:
    bool init();
    void shutdown();
    void draw(C3D_RenderTarget* top, const HudValues& v);

private:
    C2D_TextBuf buf_ = nullptr;
    C2D_TextBuf raceBuf_ = nullptr;
    int lastHp_ = -1, lastMaxHp_ = -1;
    C2D_Text hpText_{}, toastText_{};
    C2D_Text raceText_{};
    C2D_Text maxText_{};
    // romfs:/baked/laff_meter.ttm (tools/pipeline/bake_laff_meter.py), loaded on first use.
    render::Model laffAtlas_;
    bool laffTried_ = false;
    // LaffMeter.animatedEffect: pulse larger on a heal, smaller on damage.
    uint64_t pulseStartMs_ = 0;
    float pulseChange_ = 1.0f;
    void drawLaffMeter(const HudValues& v);
    const char* lastToast_ = nullptr;
    C2D_TextBuf toastBuf_ = nullptr;
};

} // namespace tt::ui
