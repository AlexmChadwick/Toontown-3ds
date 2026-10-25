// Minigame gag purchase panel (bottom screen): the PurchaseManager flow after a minigame.
// Allows buying gags with jellybeans won before returning to the playground.
// Modeled after ui/shop_panel.* and using ui/shop_rules.*.
#pragma once

#include <3ds.h>
#include <citro2d.h>

#include "net/client.h"
#include "ui/shop_rules.h"
#include "ui/focus.h"

namespace tt::ui {

class MinigamePurchasePanel {
public:
    static MinigamePurchasePanel* activeInstance() { return activeInstance_; }

    bool init();
    void shutdown();

    bool active() const { return active_; }
    void open(const net::Client& client, uint32_t beansWon, uint32_t nowMs);
    void close();

    void update(net::Client& client, uint32_t nowMs);
    u32 handleInput(u32 keysDown, const touchPosition& touch);
    void draw();

    const shop::Cart& cart() const { return cart_; }
    bool buy(int track, int level);
    void finish(bool cancel);
    bool isDone() const { return done_; }

private:
    static constexpr int kCols = shop::kRegularLevels;
    void rebuildGrid();
    void rebuildInfo(uint32_t nowMs);
    void setNote(const char* msg, uint32_t nowMs);

    static MinigamePurchasePanel* activeInstance_;

    bool active_ = false;
    bool done_ = false;
    uint32_t beansWon_ = 0;
    uint32_t deadlineMs_ = 0;
    uint32_t nowMs_ = 0;
    shop::Profile profile_{};
    shop::Cart cart_{};
    int selTrack_ = 4, selLevel_ = 0;
    // ---- padnav: the button row's focus (none while the gag grid has the D-Pad) ----
    FocusGroup buttons_{"purchase"};
    int gridId() const;        // track * 10 + level, as logged
    void logGridFocus() const; // "padnav: purchase_grid focus <id>"
    void pressButton(int id);  // pad::counter ids
    // ---- end padnav ----
    int pendingSend_ = 0; // 0 none, 1 Done, 2 Cancel
    bool dirty_ = true;
    int lastSecond_ = -1;
    char note_[64] = {};
    uint32_t noteUntilMs_ = 0;

    C2D_TextBuf staticBuf_ = nullptr;
    C2D_TextBuf gridBuf_ = nullptr;
    C2D_TextBuf infoBuf_ = nullptr;
    C2D_Text trackText_[gags::kTracks];
    C2D_Text btnText_[4]; // -, +, Done, Cancel
    C2D_Text dashText_;
    C2D_Text cellText_[gags::kTracks][kCols];
    C2D_Text titleText_, beansText_, carryText_, timerText_, infoText_;
};

} // namespace tt::ui
