#include "render/ui_font.h"
#include "ui/inventory_art.h"
#include "ui/theme.h"
#include "ui/minigame_purchase_panel.h"

#include "ui/purchase_art.h"

#include <cstdio>
#include <cstring>

#include "core/log.h"
#include "ui/pad_tables.h"
#include "world/battle_fx.h"

namespace tt::ui {

MinigamePurchasePanel* MinigamePurchasePanel::activeInstance_ = nullptr;

namespace {

constexpr u32 kTrackInk = theme::rgb(0x33, 0x33, 0x33); // trackNameLabels text_fg (0.2, 0.2, 0.2)
constexpr float kNameX = 4, kGridX = 64, kGridY = 26, kCellW = 42, kCellH = 22;
constexpr float kInfoY = 184;
constexpr float kSignW = 170;                          // the title plank
constexpr float kJarH = 40, kJarX = 136 - kJarH * 0.41f; // the bean jar, centred between [+] and Done
// The button row is shared with its focus table (ui/pad_tables.h).
constexpr float kBtnY = pad::counter::kBtnY, kBtnH = pad::counter::kBtnH;
constexpr float kMinusX = pad::counter::kMinusX, kPlusX = pad::counter::kPlusX;
constexpr float kBtnSmallW = pad::counter::kBtnSmallW;
constexpr float kDoneX = pad::counter::kDoneX, kDoneW = pad::counter::kDoneW;
constexpr float kCancelX = pad::counter::kCancelX, kCancelW = pad::counter::kCancelW;
constexpr uint32_t kAutoSubmitMarginMs = 2000;
constexpr uint32_t kNoteMs = 2500;

const u32 kWhite = C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF);

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
        tt::render::textParse(&trackText_[t], staticBuf_, theme::kTrackCaps[t]);
        C2D_TextOptimize(&trackText_[t]);
    }
    static const char* const kBtn[4] = {"-", "+", "EXIT BACK TO\nPLAYGROUND", "Cancel"}; // GagShopBackToPlayground
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

void MinigamePurchasePanel::rebuildInfo() {
    C2D_TextBufClear(infoBuf_);
    char buf[96];
    tt::render::textParse(&titleText_, infoBuf_, "Goofy's Gag Shop"); // PurchaseBase's title sign
    snprintf(buf, sizeof(buf), "%d", cart_.money); // on the jar
    tt::render::textParse(&beansText_, infoBuf_, buf);
    snprintf(buf, sizeof(buf), "+%u won   Carry: %d/%d", unsigned(beansWon_), shop::countProps(cart_.inv),
             int(profile_.maxCarry));
    tt::render::textParse(&carryText_, infoBuf_, buf);
    // The Gag Shop's info line (ui/shop_panel.cpp), plus why a gag can't be added.
    const int t = selTrack_, l = selLevel_;
    const bool open = profile_.trackAccess[t] && shop::expLevel(t, profile_.experience[t]) >= l;
    if (note_[0]) {
        snprintf(buf, sizeof(buf), "%s", note_);
    } else if (!open) {
        snprintf(buf, sizeof(buf), "%s: not unlocked yet", gags::kGagNames[t][l]);
    } else if (cart_.bought(t, l) > 0) {
        snprintf(buf, sizeof(buf), "%s: have %u, +%d now. 1 bean each", gags::kGagNames[t][l],
                 unsigned(cart_.inv[t][l]), cart_.bought(t, l));
    } else if (shop::canBuy(profile_, cart_.inv, cart_.money, t, l) == shop::Buy::TooManyProps) {
        snprintf(buf, sizeof(buf), "%s: have %u. Your gag bag is full!", gags::kGagNames[t][l],
                 unsigned(cart_.inv[t][l]));
    } else {
        snprintf(buf, sizeof(buf), "%s: have %u of %d. 1 bean each", gags::kGagNames[t][l],
                 unsigned(cart_.inv[t][l]), shop::carryMax(profile_, t, l));
    }
    tt::render::textParse(&infoText_, infoBuf_, buf);

    C2D_TextOptimize(&titleText_);
    C2D_TextOptimize(&beansText_);
    C2D_TextOptimize(&carryText_);
    C2D_TextOptimize(&infoText_);
}

void MinigamePurchasePanel::draw() {
    if (!active_) return;
    if (dirty_) {
        rebuildGrid();
        rebuildInfo();
        dirty_ = false;
    }
    // The same cream gag panel on the sky as the Gag Shop (ui/shop_panel.cpp).
    theme::sky();
    theme::paper(1, kGridY - 3, 318, gags::kTracks * kCellH + 4, 6.0f, theme::kPaper, theme::kPaperEdge, 0.2f);
    theme::paper(1, kInfoY - 3, 318, 18, 5.0f, theme::kPaper, theme::kPaperEdge, 0.2f);
    // PurchaseBase's title plank; the beans won and the carry count on a paper tag beside it.
    // The countdown is Purchase's ToontownTimer in the top screen's corner (ui/hud).
    drawGagShopSign(titleText_, 4, 0, kSignW, 24, 0.2f);
    theme::paper(kSignW + 10, 2, 318 - kSignW - 11, 20, 6.0f, theme::kPaper, theme::kPaperEdge, 0.2f);
    theme::textCentered(carryText_, (kSignW + 10 + 318) * 0.5f, 5, 0.42f, theme::kInk, 318 - kSignW - 20, 0.3f);

    for (int t = 0; t < gags::kTracks; t++) {
        const float y = kGridY + t * kCellH;
        // InventoryNew's track row in its colour, the name in its label (TextEncoder.upper).
        inventory::drawRow(kNameX, y, 316 - kNameX, kCellH - 1,
                           profile_.trackAccess[t] ? theme::kTrackColors[t]
                                                   : theme::mix(theme::kTrackColors[t], theme::kPaper, 0.55f), 0.2f);
        C2D_DrawText(&trackText_[t], C2D_WithColor, kNameX + 8, y + 4, 0.3f, 0.38f, 0.38f, kTrackInk);
        for (int l = 0; l < kCols; l++) {
            const float x = kGridX + l * kCellW;
            const bool open = profile_.trackAccess[t] && shop::expLevel(t, profile_.experience[t]) >= l;
            const bool canBuy = open && shop::canBuy(profile_, cart_.inv, cart_.money, t, l) == shop::Buy::Ok;
            const bool sel = t == selTrack_ && l == selLevel_;
            const bool bought = cart_.bought(t, l) > 0;
            const u32 ink = inventory::drawPurchaseCell(t, l, x, y, kCellW - 1, kCellH - 1, open, canBuy, bought, sel,
                                                        16.0f, 0.25f);
            C2D_DrawText(&cellText_[t][l], C2D_WithColor, x + (open ? 19 : 6), y + 4, 0.3f, 0.40f, 0.40f, ink);
        }
    }

    C2D_DrawText(&infoText_, C2D_WithColor, 6, kInfoY - 2, 0.5f, 0.45f, 0.45f, note_[0] ? theme::kRed : theme::kInk);

    drawBeanJar(beansText_, kJarX, kBtnY - 4, kJarH, 0.3f); // PurchaseBase.pointDisplay
    // Purchase's PurchScrn_BTN buttons; Done is its backToPlayground (sendPurchaseExit).
    const bool canSell = cart_.bought(selTrack_, selLevel_) > 0;
    const bool canAdd = shop::canBuy(profile_, cart_.inv, cart_.money, selTrack_, selLevel_) == shop::Buy::Ok;
    theme::purchaseButton(kMinusX, kBtnY, kBtnSmallW, kBtnH, canSell, false, 0.3f);
    theme::purchaseButton(kPlusX, kBtnY, kBtnSmallW, kBtnH, canAdd, false, 0.3f);
    theme::purchaseButton(kDoneX, kBtnY, kDoneW, kBtnH, true, false, 0.3f);
    theme::purchaseButton(kCancelX, kBtnY, kCancelW, kBtnH, true, false, 0.3f, true);
    theme::textCentered(btnText_[0], kMinusX + kBtnSmallW * 0.5f, kBtnY + 2, 0.8f,
                        canSell ? theme::kPurchaseInk : theme::kPurchaseInkOff, 0, 0.35f);
    theme::textCentered(btnText_[1], kPlusX + kBtnSmallW * 0.5f, kBtnY + 2, 0.8f,
                        canAdd ? theme::kPurchaseInk : theme::kPurchaseInkOff, 0, 0.35f);
    theme::textCentered(btnText_[2], kDoneX + kDoneW * 0.5f, kBtnY + 4, 0.42f, theme::kPurchaseInk, kDoneW - 6, 0.35f);
    theme::textCentered(btnText_[3], kCancelX + kCancelW * 0.5f, kBtnY + 9, 0.5f, theme::kWhite, kCancelW - 6, 0.35f);
    buttons_.drawHighlight(); // padnav
}

} // namespace tt::ui
