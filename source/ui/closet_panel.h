#pragma once
#include <3ds.h>
#include <citro2d.h>
#include "net/client.h"
namespace tt::ui {
class ClosetPanel {
public:
    bool init();
    void shutdown();
    bool active() const { return active_; }
    void open(const net::Client& client, uint32_t nowMs);
    void update(net::Client& client);
    u32 handleInput(u32 down, const touchPosition& touch);
    void draw();
private:
    bool active_ = false;
    uint32_t closet_ = 0;
    uint8_t pending_ = 0;
    C2D_TextBuf textBuf_ = nullptr;
    C2D_Text title_{}, body_{}, done_{}, cancel_{};
};
}
