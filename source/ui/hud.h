// Top-screen HUD (citro2d, drawn after the 3D pass): laff meter and jellybean jar.
#pragma once

#include "render/model.h"
#include "ui/laff_meter.h"

#include <3ds.h>
#include <citro2d.h>

namespace tt::ui {

struct HudValues {
    int hp = 15, maxHp = 15;
    int money = 0, maxMoney = 40;
    float headRgb[3] = {0.7f, 0.7f, 0.8f}; // laff meter face colour (toon head colour)
    int species = 0;                         // world::Species: which laff meter head
    bool online = false;
    bool laffOnBottom = false; // the bottom screen shows the meter: leave it off the world
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
    C2D_TextBuf raceBuf_ = nullptr;
    C2D_Text toastText_{};
    C2D_Text raceText_{};
    LaffDisplay laff_;
    const char* lastToast_ = nullptr;
    C2D_TextBuf toastBuf_ = nullptr;
};

} // namespace tt::ui
