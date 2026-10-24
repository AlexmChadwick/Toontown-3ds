// Top-screen HUD (citro2d, drawn after the 3D pass): laff meter and jellybean jar.
#pragma once

#include <3ds.h>
#include <citro2d.h>

namespace tt::ui {

struct HudValues {
    int hp = 15, maxHp = 15;
    int money = 0, maxMoney = 40;
    float headRgb[3] = {0.7f, 0.7f, 0.8f}; // laff meter face colour (toon head colour)
    bool online = false;
    const char* toast = nullptr; // short centred message ("Coming soon...", "Loading...")
    bool racing = false;
    int raceSpeed = 0, raceLap = 0, raceLaps = 0, racePlace = 0;
};

class Hud {
public:
    bool init();
    void shutdown();
    void draw(C3D_RenderTarget* top, const HudValues& v);

private:
    C2D_TextBuf buf_ = nullptr;
    C2D_TextBuf raceBuf_ = nullptr;
    int lastHp_ = -1, lastMaxHp_ = -1, lastMoney_ = -1;
    C2D_Text hpText_{}, moneyText_{}, toastText_{};
    C2D_Text raceText_{};
    const char* lastToast_ = nullptr;
    C2D_TextBuf toastBuf_ = nullptr;
};

} // namespace tt::ui
