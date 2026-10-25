// Gag shop panel (bottom screen): the DistributedNPCClerk purchase flow. Opened by the
// clerk's setMovie START for us; while it is open it replaces the tab UI and swallows all
// buttons (render/ui.cpp hooks it). Rules live in ui/shop_rules.* (docs/design/shop.md).
#pragma once

#include <3ds.h>
#include <citro2d.h>

#include "net/client.h"
#include "ui/shop_rules.h"

namespace tt::ui {

class ShopPanel {
public:
    bool init();
    void shutdown();

    void dismiss(); // zone/disconnect/exclusive-context cleanup
    bool active() const { return active_; }
    // EventType::ShopMovie handler: opens on START for us, closes on TIMEOUT, queues a
    // clerk-speech notice for NO_MONEY / TIMEOUT / COMPLETE (takeNotice).
    void onMovie(const net::Client& client, uint32_t nowMs);
    // Every frame (main thread): countdown, auto-submit when the AI is about to time out,
    // sends the purchase when Done/Cancel was pressed, closes if the session is lost.
    void update(net::Client& client, uint32_t nowMs);
    // Returns the buttons consumed (everything while active).
    u32 handleInput(u32 keysDown, const touchPosition& touch);
    void draw();
    // A short message for the top-screen toast (static string), once; null when none.
    const char* takeNotice() {
        const char* n = notice_;
        notice_ = nullptr;
        return n;
    }

    // Test/autotest hooks.
    const shop::Cart& cart() const { return cart_; }
    bool buy(int track, int level); // one more gag; false when refused
    void finish(bool cancel);       // Done (false) or Cancel (true); sent on the next update
    uint32_t clerkDoId() const { return clerk_; }

private:
    static constexpr int kCols = shop::kRegularLevels;
    void rebuildGrid();
    void rebuildInfo(uint32_t nowMs);
    void close();
    void setNote(const char* msg, uint32_t nowMs);

    bool active_ = false;
    uint32_t clerk_ = 0;
    uint32_t deadlineMs_ = 0;
    uint32_t nowMs_ = 0;
    shop::Profile profile_{};
    shop::Cart cart_{};
    int selTrack_ = 4, selLevel_ = 0;
    int pendingSend_ = 0; // 0 none, 1 Done, 2 Cancel
    bool dirty_ = true;
    int lastSecond_ = -1;
    char note_[64] = {};
    uint32_t noteUntilMs_ = 0;
    const char* notice_ = nullptr;

    C2D_TextBuf staticBuf_ = nullptr; // track names, button labels, "-" (parsed once)
    C2D_TextBuf gridBuf_ = nullptr;   // cell counts (re-parsed when the cart changes)
    C2D_TextBuf infoBuf_ = nullptr;   // header + info line (re-parsed on change / each second)
    C2D_Text trackText_[gags::kTracks];
    C2D_Text btnText_[4]; // -, +, Done, Cancel
    C2D_Text dashText_;
    C2D_Text cellText_[gags::kTracks][kCols];
    C2D_Text titleText_, beansText_, carryText_, timerText_, infoText_;
};

} // namespace tt::ui
