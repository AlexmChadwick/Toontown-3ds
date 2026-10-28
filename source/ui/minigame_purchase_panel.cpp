#include "render/ui_font.h"
#include "ui/minigame_purchase_panel.h"

#include <cstdio>
#include <cstring>

#include "core/log.h"
#include "ui/pad_tables.h"

namespace tt::ui {

MinigamePurchasePanel* MinigamePurchasePanel::activeInstance_ = nullptr;

namespace {

constexpr float kNameX = 4, kGridX = 64, kGridY = 26, kCellW = 42, kCellH = 22;
constexpr float kInfoY = 184;
// The button row is shared with its focus table (ui/pad_tables.h).
constexpr float kBtnY = pad::counter::kBtnY, kBtnH = pad::counter::kBtnH;
constexpr float kMinusX = pad::counter::kMinusX, kPlusX = pad::counter::kPlusX;
constexpr float kBtnSmallW = pad::counter::kBtnSmallW;
constexpr float kDoneX = pad::counter::kDoneX, kDoneW = pad::counter::kDoneW;
constexpr float kCancelX = pad::counter::kCancelX, kCancelW = pad::counter::kCancelW;
constexpr uint32_t kAutoSubmitMarginMs = 2000;
constexpr uint32_t kNoteMs = 2500;

const u32 kBg = C2D_Color32(0x1E, 0x2A, 0x44, 0xFF);
const u32 kPanel = C2D_Color32(0x2C, 0x3C, 0x60, 0xFF);
const u32 kLocked = C2D_Color32(0x16, 0x1E, 0x32, 0xFF);
const u32 kBought = C2D_Color32(0x6B, 0x52, 0x10, 0xFF);
const u32 kAccent = C2D_Color32(0xF5, 0xB8, 0x1A, 0xFF);
const u32 kWhite = C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF);
const u32 kDim = C2D_Color32(0x7C, 0x88, 0xA4, 0xFF);
const u32 kGood = C2D_Color32(0x8C, 0xF0, 0x8C, 0xFF);
const u32 kBad = C2D_Color32(0xFF, 0x80, 0x80, 0xFF);

bool inRect(const touchPosition& t, float x, float y, float w, float h) {
    return t.px >= x && t.px < x + w && t.py >= y && t.py < y + h;
}

} // namespace

bool MinigamePurchasePanel::init() {
    if (staticBuf_) return true; // reuse across trolley rides
    staticBuf_ = C2D_TextBufNew(256);
    gridBuf_ = C2D_TextBufNew(512);
    infoBuf_ = C2D_TextBufNew(512);
    if (!staticBuf_ || !gridBuf_ || !infoBuf_) return false;
    for (int t = 0; t < gags::kTracks; t++) {
        tt::render::textParse(&trackText_[t], staticBuf_, gags::kTrackNames[t]);
        C2D_TextOptimize(&trackText_[t]);
    }
    static const char* const kBtn[4] = {"-", "+", "Done", "Cancel"};
    for (int i = 0; i < 4; i++) {
        tt::render::textParse(&btnText_[i], staticBuf_, kBtn[i]);
        C2D_TextOptimize(&btnText_[i]);
    }
    tt::render::textParse(&dashText_, staticBuf_, "-");
    C2D_TextOptimize(&dashText_);
    return true;
}

void MinigamePurchasePanel::shutdown() {
    close();
    C2D_TextBuf* bufs[] = {&staticBuf_, &gridBuf_, &infoBuf_};
    for (C2D_TextBuf* b : bufs) {
        if (*b) C2D_TextBufDelete(*b);
        *b = nullptr;
    }
}

void MinigamePurchasePanel::close() {
    active_ = false;
    pendingSend_ = 0;
    if (activeInstance_ == this) activeInstance_ = nullptr;
}

void MinigamePurchasePanel::setNote(const char* msg, uint32_t nowMs) {
    snprintf(note_, sizeof(note_), "%s", msg);
    noteUntilMs_ = nowMs + kNoteMs;
    dirty_ = true;
}

void MinigamePurchasePanel::open(const net::Client& client, uint32_t beansWon, uint32_t deadlineMs, uint32_t nowMs) {
    const net::OwnStats& st = client.ownStats();
    for (int t = 0; t < gags::kTracks; t++) {
        profile_.experience[t] = st.experience[t];
        profile_.trackAccess[t] = st.trackAccess[t];
    }
    profile_.maxCarry = st.maxCarry;
    cart_.reset(st.inventory, st.money);

    beansWon_ = beansWon;
    // PurchaseManager.setCountdown is a network timestamp.  The client reconstructs
    // this local deadline when it decodes the required field; do not restart the
    // upstream PurchaseManagerConstants.PURCHASE_COUNTDOWN_TIME window on generate.
    deadlineMs_ = deadlineMs;
    nowMs_ = nowMs;
    pendingSend_ = 0;
    done_ = false;
    note_[0] = 0;
    selTrack_ = 4;
    selLevel_ = 0;
    pad::buildCounterButtons(buttons_); // padnav: the grid has the focus
    dirty_ = true;
    lastSecond_ = -1;
    active_ = true;
    activeInstance_ = this;
    TT_LOG("minigame purchase: opened with %u won, %d total money", unsigned(beansWon), cart_.money);
}

bool MinigamePurchasePanel::buy(int track, int level) {
    if (!active_ || pendingSend_) return false;
    shop::Buy why;
    if (cart_.add(profile_, track, level, &why)) {
        dirty_ = true;
        return true;
    }
    const char* msg = "Cannot buy";
    switch (why) {
    case shop::Buy::NoMoney:      msg = "Not enough jellybeans!"; break;
    case shop::Buy::TooManyProps: msg = "Gag bag is full!"; break;
    case shop::Buy::TooManyOfGag: msg = "Carrying maximum of that gag!"; break;
    case shop::Buy::NoSkill:      msg = "Gag not unlocked yet!"; break;
    default:                      break;
    }
    setNote(msg, nowMs_);
    return false;
}

void MinigamePurchasePanel::finish(bool cancel) {
    if (active_ && !pendingSend_) pendingSend_ = cancel ? 2 : 1;
}

void MinigamePurchasePanel::update(net::Client& client, uint32_t nowMs) {
    nowMs_ = nowMs;
    if (!active_) return;
    const int32_t remain = int32_t(deadlineMs_ - nowMs);
    if (!pendingSend_ && remain <= int32_t(kAutoSubmitMarginMs)) pendingSend_ = 1;
    if (pendingSend_) {
        const bool cancel = pendingSend_ == 2;
        bool valid = true;
        if (!cancel) valid = shop::validatePurchase(profile_, cart_.start, cart_.inv, cart_.startMoney, cart_.money);
        if (!valid) TT_LOG("minigame purchase: cart failed local validation, cancelling");
        const bool useCart = !cancel && valid;
        if (!useCart) {
            std::memcpy(cart_.inv, cart_.start, sizeof(cart_.inv));
            cart_.money = cart_.startMoney;
        }
        if (!client.sendPurchaseExit()) return;
        done_ = true;
        TT_LOG("minigame purchase: %s exit sent, %d beans left", useCart ? "done" : "cancel", cart_.money);
        close();
        return;
    }
    if (note_[0] && int32_t(nowMs - noteUntilMs_) >= 0) {
        note_[0] = 0;
        dirty_ = true;
    }
    const int sec = remain > 0 ? remain / 1000 : 0;
    if (sec != lastSecond_) {
        lastSecond_ = sec;
        dirty_ = true;
    }
}

u32 MinigamePurchasePanel::handleInput(u32 down, const touchPosition& t) {
    if (!active_) return 0;
    // ---- padnav: D-Down past the last track reaches the button row ([-] [+] Done Cancel);
    // there Left / Right move, A presses, D-Up returns to the gag grid.
    if (buttons_.hasFocus()) {
        if (down & KEY_DUP) {
            buttons_.blur();
            logGridFocus();
        } else {
            int activated = -1;
            buttons_.handleInput(down & (KEY_DLEFT | KEY_DRIGHT | KEY_A), touchPosition{}, &activated);
            pressButton(activated);
        }
        down &= ~(kDpadKeys | KEY_A);
    } else if ((down & KEY_DDOWN) && selTrack_ == gags::kTracks - 1) {
        buttons_.setFocusById(pad::counter::kIdDone);
        down &= ~KEY_DDOWN;
    }
    const int prevTrack = selTrack_;
    const int prevLevel = selLevel_;
    // ---- end padnav ----
    if ((down & KEY_DUP) && selTrack_ > 0) { selTrack_--; dirty_ = true; }
    if ((down & KEY_DDOWN) && selTrack_ < gags::kTracks - 1) { selTrack_++; dirty_ = true; }
    if ((down & KEY_DLEFT) && selLevel_ > 0) { selLevel_--; dirty_ = true; }
    if ((down & KEY_DRIGHT) && selLevel_ < kCols - 1) { selLevel_++; dirty_ = true; }
    // ---- padnav ----
    if (selTrack_ != prevTrack || selLevel_ != prevLevel) logGridFocus();
    if (down & KEY_A) buttons_.logActivate(gridId());
    // ---- end padnav ----
    if (down & KEY_A) buy(selTrack_, selLevel_);
    if (down & KEY_X) dirty_ |= cart_.remove(selTrack_, selLevel_);
    if (down & KEY_START) finish(false);
    if (down & KEY_B) finish(true);
    if (down & KEY_TOUCH) {
        for (int tr = 0; tr < gags::kTracks; tr++) {
            for (int l = 0; l < kCols; l++) {
                if (inRect(t, kGridX + l * kCellW, kGridY + tr * kCellH, kCellW, kCellH)) {
                    if (tr == selTrack_ && l == selLevel_) buy(tr, l);
                    selTrack_ = tr;
                    selLevel_ = l;
                    dirty_ = true;
                }
            }
        }
        if (inRect(t, kMinusX, kBtnY, kBtnSmallW, kBtnH)) dirty_ |= cart_.remove(selTrack_, selLevel_);
        if (inRect(t, kPlusX, kBtnY, kBtnSmallW, kBtnH)) buy(selTrack_, selLevel_);
        if (inRect(t, kDoneX, kBtnY, kDoneW, kBtnH)) finish(false);
        if (inRect(t, kCancelX, kBtnY, kCancelW, kBtnH)) finish(true);
        // ---- padnav: a touched button takes the focus; a touch above the row returns it to the grid
        const int hit = buttons_.itemAt(t.px, t.py);
        if (hit >= 0) {
            buttons_.setFocus(hit);
        } else if (t.py < kBtnY && buttons_.hasFocus()) {
            buttons_.blur();
        }
        // ---- end padnav ----
    }
    return 0xFFFFFFFFu;
}

// ---- padnav ----
int MinigamePurchasePanel::gridId() const {
    return selTrack_ * 10 + selLevel_;
}

void MinigamePurchasePanel::logGridFocus() const {
    TT_LOG("padnav: purchase_grid focus %d", gridId());
}

// The same actions as touching the buttons.
void MinigamePurchasePanel::pressButton(int id) {
    if (id == pad::counter::kIdMinus) dirty_ |= cart_.remove(selTrack_, selLevel_);
    if (id == pad::counter::kIdPlus) buy(selTrack_, selLevel_);
    if (id == pad::counter::kIdDone) finish(false);
    if (id == pad::counter::kIdCancel) finish(true);
}
// ---- end padnav ----

void MinigamePurchasePanel::rebuildGrid() {
    C2D_TextBufClear(gridBuf_);
    char buf[16];
    for (int t = 0; t < gags::kTracks; t++) {
        for (int l = 0; l < kCols; l++) {
            const bool open = profile_.trackAccess[t] && shop::expLevel(t, profile_.experience[t]) >= l;
            if (!open) {
                cellText_[t][l] = dashText_;
                continue;
            }
            snprintf(buf, sizeof(buf), "%u/%d", unsigned(cart_.inv[t][l]), shop::carryMax(profile_, t, l));
            tt::render::textParse(&cellText_[t][l], gridBuf_, buf);
            C2D_TextOptimize(&cellText_[t][l]);
        }
    }
}

void MinigamePurchasePanel::rebuildInfo(uint32_t nowMs) {
    C2D_TextBufClear(infoBuf_);
    char buf[96];
    tt::render::textParse(&titleText_, infoBuf_, "Gag Purchase");
    snprintf(buf, sizeof(buf), "Beans: %d (+%u won)", cart_.money, unsigned(beansWon_));
    tt::render::textParse(&beansText_, infoBuf_, buf);
    snprintf(buf, sizeof(buf), "Carry: %d/%d", shop::countProps(cart_.inv), int(profile_.maxCarry));
    tt::render::textParse(&carryText_, infoBuf_, buf);
    const int sec = deadlineMs_ > nowMs ? int((deadlineMs_ - nowMs) / 1000) : 0;
    snprintf(buf, sizeof(buf), "%02d:%02d", sec / 60, sec % 60);
    tt::render::textParse(&timerText_, infoBuf_, buf);
    const char* msg = note_[0] ? note_ : gags::kGagNames[selTrack_][selLevel_];
    tt::render::textParse(&infoText_, infoBuf_, msg);

    C2D_TextOptimize(&titleText_);
    C2D_TextOptimize(&beansText_);
    C2D_TextOptimize(&carryText_);
    C2D_TextOptimize(&timerText_);
    C2D_TextOptimize(&infoText_);
}

void MinigamePurchasePanel::draw() {
    if (!active_) return;
    if (dirty_) {
        rebuildGrid();
        rebuildInfo(nowMs_);
        dirty_ = false;
    }
    C2D_DrawRectSolid(0, 0, 0.1f, 320, 240, kBg);
    // Title on the left; timer, carry and beans right-aligned from the edge, measured so they
    // never run into each other.
    constexpr float kHeadScale = 0.42f;
    constexpr float kHeadGap = 8.0f;
    float w = 0.0f;
    C2D_DrawText(&titleText_, C2D_WithColor, 4, 4, 0.5f, 0.45f, 0.45f, kAccent);
    C2D_TextGetDimensions(&timerText_, kHeadScale, kHeadScale, &w, nullptr);
    float right = 316.0f - w;
    C2D_DrawText(&timerText_, C2D_WithColor, right, 5, 0.5f, kHeadScale, kHeadScale, kDim);
    C2D_TextGetDimensions(&carryText_, kHeadScale, kHeadScale, &w, nullptr);
    right -= kHeadGap + w;
    C2D_DrawText(&carryText_, C2D_WithColor, right, 5, 0.5f, kHeadScale, kHeadScale, kWhite);
    C2D_TextGetDimensions(&beansText_, kHeadScale, kHeadScale, &w, nullptr);
    right -= kHeadGap + w;
    C2D_DrawText(&beansText_, C2D_WithColor, right, 5, 0.5f, kHeadScale, kHeadScale, kGood);

    for (int t = 0; t < gags::kTracks; t++) {
        const float y = kGridY + t * kCellH;
        C2D_DrawText(&trackText_[t], C2D_WithColor, kNameX, y + 4, 0.5f, 0.42f, 0.42f,
                     profile_.trackAccess[t] ? kWhite : kDim);
        for (int l = 0; l < kCols; l++) {
            const float x = kGridX + l * kCellW;
            const bool open = profile_.trackAccess[t] && shop::expLevel(t, profile_.experience[t]) >= l;
            const bool sel = t == selTrack_ && l == selLevel_;
            const bool hasBought = cart_.bought(t, l) > 0;
            u32 bg = open ? (hasBought ? kBought : kPanel) : kLocked;
            if (sel) bg = C2D_Color32(0x40, 0x58, 0x8C, 0xFF);
            C2D_DrawRectSolid(x, y, 0.2f, kCellW - 2, kCellH - 2, bg);
            if (sel) {
                C2D_DrawRectSolid(x - 1, y - 1, 0.25f, kCellW, 1, kAccent);
                C2D_DrawRectSolid(x - 1, y + kCellH - 2, 0.25f, kCellW, 1, kAccent);
                C2D_DrawRectSolid(x - 1, y, 0.25f, 1, kCellH - 2, kAccent);
                C2D_DrawRectSolid(x + kCellW - 2, y, 0.25f, 1, kCellH - 2, kAccent);
            }
            C2D_DrawText(&cellText_[t][l], C2D_WithColor, x + 4, y + 4, 0.5f, 0.40f, 0.40f,
                         open ? (hasBought ? kAccent : kWhite) : kDim);
        }
    }

    C2D_DrawText(&infoText_, C2D_WithColor, 4, kInfoY, 0.5f, 0.45f, 0.45f, note_[0] ? kBad : kWhite);

    // Buttons
    C2D_DrawRectSolid(kMinusX, kBtnY, 0.3f, kBtnSmallW, kBtnH, kPanel);
    C2D_DrawRectSolid(kPlusX, kBtnY, 0.3f, kBtnSmallW, kBtnH, kPanel);
    C2D_DrawRectSolid(kDoneX, kBtnY, 0.3f, kDoneW, kBtnH, kGood);
    C2D_DrawRectSolid(kCancelX, kBtnY, 0.3f, kCancelW, kBtnH, kPanel);

    C2D_DrawText(&btnText_[0], C2D_WithColor, kMinusX + 16, kBtnY + 8, 0.5f, 0.6f, 0.6f, kWhite);
    C2D_DrawText(&btnText_[1], C2D_WithColor, kPlusX + 16, kBtnY + 8, 0.5f, 0.6f, 0.6f, kWhite);
    C2D_DrawText(&btnText_[2], C2D_WithColor, kDoneX + 14, kBtnY + 8, 0.5f, 0.55f, 0.55f, C2D_Color32(0x00, 0x30, 0x00, 0xFF));
    C2D_DrawText(&btnText_[3], C2D_WithColor, kCancelX + 8, kBtnY + 8, 0.5f, 0.5f, 0.5f, kWhite);
    buttons_.drawHighlight(); // padnav
}

} // namespace tt::ui
