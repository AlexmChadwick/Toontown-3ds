#include "render/ui_font.h"
#include "ui/closet_panel.h"
#include "ui/pad_tables.h"
namespace tt::ui {
bool ClosetPanel::init() { textBuf_ = C2D_TextBufNew(256); return textBuf_ != nullptr; }
void ClosetPanel::shutdown() {
    if (textBuf_) C2D_TextBufDelete(textBuf_);
    textBuf_ = nullptr; active_ = false;
    focus_.clear();
}
void ClosetPanel::open(const net::Client& client, uint32_t) {
    const net::Closet* c = client.closet();
    if (!c) return;
    closet_ = c->doId; active_ = true; pending_ = 0;
    C2D_TextBufClear(textBuf_);
    tt::render::textParse(&title_, textBuf_, "Closet");
    tt::render::textParse(&body_, textBuf_, "Save keeps your current owned clothes.\nD-Pad picks a button, A presses it. B cancels.");
    tt::render::textParse(&done_, textBuf_, "Save");
    tt::render::textParse(&cancel_, textBuf_, "Cancel");
    C2D_TextOptimize(&title_); C2D_TextOptimize(&body_);
    C2D_TextOptimize(&done_); C2D_TextOptimize(&cancel_);
    // ---- padnav: Save is focused, so A saves as it always did ----
    pad::buildCloset(focus_);
    focus_.setFocusById(pad::closet::kIdDone);
}
void ClosetPanel::update(net::Client& client) {
    if (!active_) return;
    if (!client.closet() || client.closet()->doId != closet_) active_ = false;
    else if (pending_) {
        if (pending_ == 1) client.sendClosetDNA(closet_, client.avatarDna(), 2, 15);
        else client.sendClosetExit(closet_);
        pending_ = 0; active_ = false;
    }
}
u32 ClosetPanel::handleInput(u32 down, const touchPosition& touch) {
    if (!active_) return 0;
    // ---- padnav: the focus table holds the Save / Cancel touch rectangles ----
    int activated = -1;
    focus_.handleInput(down, touch, &activated);
    if (activated == pad::closet::kIdDone) {
        pending_ = 1;
    } else if (activated == pad::closet::kIdCancel || (down & KEY_B)) {
        pending_ = 2;
    }
    // ---- end padnav ----
    return 0xFFFFFFFFu;
}
void ClosetPanel::draw() {
    if (!active_) return;
    const u32 bg = C2D_Color32(0x1E, 0x2A, 0x44, 0xFF);
    const u32 panel = C2D_Color32(0x2C, 0x3C, 0x60, 0xFF);
    const u32 accent = C2D_Color32(0xF5, 0xB8, 0x1A, 0xFF);
    const u32 white = C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF);
    C2D_DrawRectSolid(4, 4, 0.5f, 312, 228, bg);
    C2D_DrawText(&title_, C2D_WithColor, 10, 12, 0.5f, 0.6f, 0.6f, accent);
    C2D_DrawText(&body_, C2D_WithColor, 10, 70, 0.5f, 0.45f, 0.45f, white);
    C2D_DrawRectSolid(160, 196, 0.5f, 76, 36, accent);
    C2D_DrawRectSolid(240, 196, 0.5f, 76, 36, panel);
    C2D_DrawText(&done_, C2D_WithColor, 180, 207, 0.5f, 0.52f, 0.52f, bg);
    C2D_DrawText(&cancel_, C2D_WithColor, 248, 207, 0.5f, 0.46f, 0.46f, white);
    focus_.drawHighlight();
}
}
