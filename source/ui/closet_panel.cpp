#include "render/ui_font.h"
#include "ui/closet_panel.h"
#include "ui/pad_tables.h"
#include "ui/theme.h"
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
    namespace th = theme;
    th::sky();
    th::paper(4, 4, 312, 186, 8.0f);
    C2D_DrawText(&title_, C2D_WithColor, 12, 10, 0.5f, 0.6f, 0.6f, th::kRedDark);
    C2D_DrawText(&body_, C2D_WithColor, 12, 70, 0.5f, 0.45f, 0.45f, th::kInk);
    th::button(160, 196, 76, 36, th::kGreen, th::kGreenDark, false, 7.0f);
    th::button(240, 196, 76, 36, th::kRed, th::kRedDark, false, 7.0f);
    th::textCentered(done_, 198, 204, 0.52f, th::kWhite, 70);
    th::textCentered(cancel_, 278, 204, 0.5f, th::kWhite, 70);
    focus_.drawHighlight();
}
}
