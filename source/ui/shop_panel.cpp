#include "render/ui_font.h"
#include "ui/inventory_art.h"
#include "ui/theme.h"
#include "ui/shop_panel.h"

#include "ui/purchase_art.h"

#include <cstdio>
#include <cstring>

#include "core/log.h"
#include "ui/pad_tables.h"
#include "world/battle_fx.h"

namespace tt::ui {

namespace {

// Layout (bottom screen 320x240).
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
constexpr uint32_t kAutoSubmitMarginMs = 2000; // send before the AI's own 120 s timeout fires
constexpr uint32_t kNoteMs = 2500;

const u32 kInk = theme::kInk;

bool inRect(const touchPosition& t, float x, float y, float w, float h) {
    return t.px >= x && t.px < x + w && t.py >= y && t.py < y + h;
}

// TTLocalizer strings (clerk speech).
const char* const kNoticeNoMoney = "You need to ride the Trolley to get some jellybeans.";
const char* const kNoticeTimeout = "Need more time to think?";
const char* const kNoticeGoodbye = "See you later!";

} // namespace

bool ShopPanel::init() {
    staticBuf_ = C2D_TextBufNew(256);
    gridBuf_ = C2D_TextBufNew(512);
    infoBuf_ = C2D_TextBufNew(512);
    if (!staticBuf_ || !gridBuf_ || !infoBuf_) return false;
    for (int t = 0; t < gags::kTracks; t++) {
        tt::render::textParse(&trackText_[t], staticBuf_, theme::kTrackCaps[t]);
        C2D_TextOptimize(&trackText_[t]);
    }
    static const char* const kBtn[4] = {"-", "+", "DONE\nSHOPPING", "Cancel"}; // GagShopDoneShopping
    for (int i = 0; i < 4; i++) {
        tt::render::textParse(&btnText_[i], staticBuf_, kBtn[i]);
        C2D_TextOptimize(&btnText_[i]);
    }
    tt::render::textParse(&dashText_, staticBuf_, "-");
    C2D_TextOptimize(&dashText_);
    return true;
}

void ShopPanel::shutdown() {
    C2D_TextBuf* bufs[] = {&staticBuf_, &gridBuf_, &infoBuf_};
    for (C2D_TextBuf* b : bufs) {
        if (*b) C2D_TextBufDelete(*b);
        *b = nullptr;
    }
}

void ShopPanel::dismiss() {
    close();
}

void ShopPanel::close() {
    active_ = false;
    pendingSend_ = 0;
    clerk_ = 0;
}

void ShopPanel::setNote(const char* msg, uint32_t nowMs) {
    snprintf(note_, sizeof(note_), "%s", msg);
    noteUntilMs_ = nowMs + kNoteMs;
    dirty_ = true;
}

void ShopPanel::onMovie(const net::Client& client, uint32_t nowMs) {
    const net::ShopMovie& m = client.shopMovie();
    switch (m.mode) {
    case net::kPurchaseMovieStart: {
        if (m.avId != client.avatarId() || active_) return;
        const net::OwnStats& st = client.ownStats();
        for (int t = 0; t < gags::kTracks; t++) {
            profile_.experience[t] = st.experience[t];
            profile_.trackAccess[t] = st.trackAccess[t];
        }
        profile_.maxCarry = st.maxCarry;
        cart_.reset(st.inventory, st.money);
        clerk_ = m.clerkDoId;
        deadlineMs_ = m.deadlineMs;
        nowMs_ = nowMs;
        pendingSend_ = 0;
        note_[0] = 0;
        // Start the cursor on the first track we may use.
        selTrack_ = 4;
        selLevel_ = 0;
        pad::buildCounterButtons(buttons_); // padnav: the grid has the focus
        for (int t = 0; t < gags::kTracks; t++) {
            if (profile_.trackAccess[t]) {
                selTrack_ = t;
                break;
            }
        }
        active_ = true;
        dirty_ = true;
        TT_LOG("shop: open with clerk %u, %d beans, %d/%d gags", unsigned(clerk_), int(st.money),
               shop::countProps(st.inventory), int(st.maxCarry));
        break;
    }
    case net::kPurchaseMovieTimeout:
        if (m.avId == client.avatarId()) {
            if (active_) close();
            notice_ = kNoticeTimeout;
        }
        break;
    case net::kPurchaseMovieComplete:
        if (m.avId == client.avatarId()) {
            if (active_) close();
            notice_ = kNoticeGoodbye;
        }
        break;
    case net::kPurchaseMovieNoMoney:
        if (m.avId == client.avatarId()) notice_ = kNoticeNoMoney;
        break;
    default:
        break; // CLEAR: nothing to show
    }
}

bool ShopPanel::buy(int track, int level) {
    shop::Buy why = shop::Buy::Ok;
    if (cart_.add(profile_, track, level, &why)) {
        dirty_ = true;
        return true;
    }
    switch (why) {
    case shop::Buy::TooManyProps: setNote("Sorry, you have too many props", nowMs_); break;
    case shop::Buy::TooManyOfGag: setNote("Sorry, you have enough of those already", nowMs_); break;
    case shop::Buy::NoSkill:
    case shop::Buy::NotForSale: setNote("You do not have enough skill for that yet", nowMs_); break;
    case shop::Buy::NoMoney: setNote("You are out of jellybeans", nowMs_); break;
    default: break;
    }
    return false;
}

void ShopPanel::finish(bool cancel) {
    if (active_ && !pendingSend_) pendingSend_ = cancel ? 2 : 1;
}

void ShopPanel::update(net::Client& client, uint32_t nowMs) {
    nowMs_ = nowMs;
    if (!active_) return;
    if (!client.inZone() || !client.shopOpenFor(clerk_)) {
        TT_LOG("shop: session lost, closing");
        close();
        return;
    }
    const int32_t remain = int32_t(deadlineMs_ - nowMs);
    if (!pendingSend_ && remain <= int32_t(kAutoSubmitMarginMs)) pendingSend_ = 1; // __timerExpired -> handleDone
    if (pendingSend_) {
        const bool cancel = pendingSend_ == 2;
        bool valid = true;
        if (!cancel) valid = shop::validatePurchase(profile_, cart_.start, cart_.inv, cart_.startMoney, cart_.money);
        if (!valid) TT_LOG("shop: cart failed local validation, cancelling"); // cannot happen via Cart
        const bool useCart = !cancel && valid;
        client.sendClerkSetInventory(clerk_, useCart ? cart_.inv : cart_.start,
                                     int16_t(useCart ? cart_.money : cart_.startMoney), true);
        TT_LOG("shop: %s sent, %d beans left", useCart ? "done" : "cancel", useCart ? cart_.money : cart_.startMoney);
        close();
        return;
    }
    if (note_[0] && int32_t(nowMs - noteUntilMs_) >= 0) {
        note_[0] = 0;
        dirty_ = true;
    }
}

u32 ShopPanel::handleInput(u32 down, const touchPosition& t) {
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
                    if (tr == selTrack_ && l == selLevel_) buy(tr, l); // tap the selected gag again: +1
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
int ShopPanel::gridId() const {
    return selTrack_ * 10 + selLevel_;
}

void ShopPanel::logGridFocus() const {
    TT_LOG("padnav: shop_grid focus %d", gridId());
}

// The same actions as touching the buttons.
void ShopPanel::pressButton(int id) {
    if (id == pad::counter::kIdMinus) dirty_ |= cart_.remove(selTrack_, selLevel_);
    if (id == pad::counter::kIdPlus) buy(selTrack_, selLevel_);
    if (id == pad::counter::kIdDone) finish(false);
    if (id == pad::counter::kIdCancel) finish(true);
}
// ---- end padnav ----

void ShopPanel::rebuildGrid() {
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

void ShopPanel::rebuildInfo() {
    C2D_TextBufClear(infoBuf_);
    char buf[96];
    tt::render::textParse(&titleText_, infoBuf_, "Goofy's Gag Shop"); // TTLocalizer.GagShopName
    snprintf(buf, sizeof(buf), "%d", cart_.money); // on the jar
    tt::render::textParse(&beansText_, infoBuf_, buf);
    snprintf(buf, sizeof(buf), "Carry: %d/%d", shop::countProps(cart_.inv), int(profile_.maxCarry));
    tt::render::textParse(&carryText_, infoBuf_, buf);
    if (note_[0]) {
        snprintf(buf, sizeof(buf), "%s", note_);
    } else {
        const int t = selTrack_, l = selLevel_;
        const bool open = profile_.trackAccess[t] && shop::expLevel(t, profile_.experience[t]) >= l;
        if (!open) {
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
    }
    tt::render::textParse(&infoText_, infoBuf_, buf);
    C2D_Text* all[] = {&titleText_, &beansText_, &carryText_, &infoText_};
    for (C2D_Text* x : all) C2D_TextOptimize(x);
}

void ShopPanel::draw() {
    if (!active_) return;
    if (dirty_) {
        rebuildGrid();
        rebuildInfo();
        dirty_ = false;
    }
    // Upstream's purchase screen: cream gag panel, track-coloured row labels, gags bought this
    // visit in gold, Done green and Cancel red.
    theme::sky();
    // ClerkPurchase's title: the Goofys_Sign plank; the carry count on a paper tag beside it.
    drawGagShopSign(titleText_, 4, 0, kSignW, 24);
    theme::paper(kSignW + 10, 2, 318 - kSignW - 11, 20, 6.0f);
    theme::textCentered(carryText_, (kSignW + 10 + 318) * 0.5f, 5, 0.45f, kInk, 318 - kSignW - 20);
    // The countdown is ClerkPurchase's ToontownTimer in the top screen's corner (ui/hud).
    theme::paper(1, kGridY - 3, 318, gags::kTracks * kCellH + 5, 6.0f);
    for (int t = 0; t < gags::kTracks; t++) {
        const float y = kGridY + t * kCellH;
        // InventoryNew's track row in its colour, the name in its label (TextEncoder.upper).
        inventory::drawRow(kNameX, y, 316 - kNameX, kCellH - 1,
                           profile_.trackAccess[t] ? theme::kTrackColors[t]
                                                   : theme::mix(theme::kTrackColors[t], theme::kPaper, 0.55f), 0.5f);
        C2D_DrawText(&trackText_[t], C2D_WithColor, kNameX + 8, y + 4, 0.55f, 0.38f, 0.38f, kTrackInk);
        for (int l = 0; l < kCols; l++) {
            const float x = kGridX + l * kCellW;
            const bool open = profile_.trackAccess[t] && shop::expLevel(t, profile_.experience[t]) >= l;
            const bool canBuy = open && shop::canBuy(profile_, cart_.inv, cart_.money, t, l) == shop::Buy::Ok;
            const bool sel = t == selTrack_ && l == selLevel_;
            const bool bought = cart_.bought(t, l) > 0;
            const u32 ink = inventory::drawPurchaseCell(t, l, x, y, kCellW - 1, kCellH - 1, open, canBuy, bought, sel,
                                                        16.0f, 0.52f);
            C2D_DrawText(&cellText_[t][l], C2D_WithColor, x + (open ? 19 : 6), y + 4, 0.56f, 0.40f, 0.40f, ink);
        }
    }
    theme::paper(1, kInfoY - 3, 318, 18, 5.0f);
    C2D_DrawText(&infoText_, C2D_WithColor, 6, kInfoY - 1, 0.5f, 0.42f, 0.42f,
                 note_[0] ? theme::kRed : theme::kGreenDark);
    const bool canSell = cart_.bought(selTrack_, selLevel_) > 0;
    const bool canAdd = shop::canBuy(profile_, cart_.inv, cart_.money, selTrack_, selLevel_) == shop::Buy::Ok;
    // PurchaseBase.pointDisplay: our jellybeans on the Jar, between [+] and Done.
    drawBeanJar(beansText_, kJarX, kBtnY - 4, kJarH);
    // ClerkPurchase's PurchScrn_BTN buttons (orange, blue ink; dimmed when they cannot act).
    theme::purchaseButton(kMinusX, kBtnY, kBtnSmallW, kBtnH, canSell);
    theme::purchaseButton(kPlusX, kBtnY, kBtnSmallW, kBtnH, canAdd);
    theme::purchaseButton(kDoneX, kBtnY, kDoneW, kBtnH);
    theme::purchaseButton(kCancelX, kBtnY, kCancelW, kBtnH, true, false, 0.5f, true);
    theme::textCentered(btnText_[0], kMinusX + kBtnSmallW * 0.5f, kBtnY + 2, 0.8f,
                        canSell ? theme::kPurchaseInk : theme::kPurchaseInkOff, 0, 0.55f);
    theme::textCentered(btnText_[1], kPlusX + kBtnSmallW * 0.5f, kBtnY + 2, 0.8f,
                        canAdd ? theme::kPurchaseInk : theme::kPurchaseInkOff, 0, 0.55f);
    theme::textCentered(btnText_[2], kDoneX + kDoneW * 0.5f, kBtnY + 4, 0.42f, theme::kPurchaseInk, kDoneW - 6, 0.55f);
    theme::textCentered(btnText_[3], kCancelX + kCancelW * 0.5f, kBtnY + 9, 0.5f, theme::kWhite, kCancelW - 6, 0.55f);
    buttons_.drawHighlight(); // padnav
}

} // namespace tt::ui
