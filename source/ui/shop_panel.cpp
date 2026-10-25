#include "ui/shop_panel.h"

#include <cstdio>
#include <cstring>

#include "core/log.h"
#include "world/battle_fx.h"

namespace tt::ui {

namespace {

// Layout (bottom screen 320x240).
constexpr float kNameX = 4, kGridX = 64, kGridY = 26, kCellW = 42, kCellH = 22;
constexpr float kInfoY = 184;
constexpr float kBtnY = 202, kBtnH = 34;
constexpr float kMinusX = 4, kPlusX = 52, kBtnSmallW = 44;
constexpr float kDoneX = 176, kDoneW = 70, kCancelX = 250, kCancelW = 66;
constexpr uint32_t kAutoSubmitMarginMs = 2000; // send before the AI's own 120 s timeout fires
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
        C2D_TextParse(&trackText_[t], staticBuf_, gags::kTrackNames[t]);
        C2D_TextOptimize(&trackText_[t]);
    }
    static const char* const kBtn[4] = {"-", "+", "Done", "Cancel"};
    for (int i = 0; i < 4; i++) {
        C2D_TextParse(&btnText_[i], staticBuf_, kBtn[i]);
        C2D_TextOptimize(&btnText_[i]);
    }
    C2D_TextParse(&dashText_, staticBuf_, "-");
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
        for (int t = 0; t < gags::kTracks; t++) {
            if (profile_.trackAccess[t]) {
                selTrack_ = t;
                break;
            }
        }
        active_ = true;
        dirty_ = true;
        lastSecond_ = -1;
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
    const int sec = remain > 0 ? remain / 1000 : 0;
    if (sec != lastSecond_) {
        lastSecond_ = sec;
        dirty_ = true;
    }
}

u32 ShopPanel::handleInput(u32 down, const touchPosition& t) {
    if (!active_) return 0;
    if ((down & KEY_DUP) && selTrack_ > 0) { selTrack_--; dirty_ = true; }
    if ((down & KEY_DDOWN) && selTrack_ < gags::kTracks - 1) { selTrack_++; dirty_ = true; }
    if ((down & KEY_DLEFT) && selLevel_ > 0) { selLevel_--; dirty_ = true; }
    if ((down & KEY_DRIGHT) && selLevel_ < kCols - 1) { selLevel_++; dirty_ = true; }
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
    }
    return 0xFFFFFFFFu;
}

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
            C2D_TextParse(&cellText_[t][l], gridBuf_, buf);
            C2D_TextOptimize(&cellText_[t][l]);
        }
    }
}

void ShopPanel::rebuildInfo(uint32_t nowMs) {
    C2D_TextBufClear(infoBuf_);
    char buf[96];
    C2D_TextParse(&titleText_, infoBuf_, "Gag Shop");
    snprintf(buf, sizeof(buf), "Beans: %d", cart_.money);
    C2D_TextParse(&beansText_, infoBuf_, buf);
    snprintf(buf, sizeof(buf), "Carry: %d/%d", shop::countProps(cart_.inv), int(profile_.maxCarry));
    C2D_TextParse(&carryText_, infoBuf_, buf);
    const int32_t remain = int32_t(deadlineMs_ - nowMs);
    const int sec = remain > 0 ? remain / 1000 : 0;
    snprintf(buf, sizeof(buf), "%d:%02d", sec / 60, sec % 60);
    C2D_TextParse(&timerText_, infoBuf_, buf);
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
        } else {
            snprintf(buf, sizeof(buf), "%s: have %u of %d. 1 bean each", gags::kGagNames[t][l],
                     unsigned(cart_.inv[t][l]), shop::carryMax(profile_, t, l));
        }
    }
    C2D_TextParse(&infoText_, infoBuf_, buf);
    C2D_Text* all[] = {&titleText_, &beansText_, &carryText_, &timerText_, &infoText_};
    for (C2D_Text* x : all) C2D_TextOptimize(x);
}

void ShopPanel::draw() {
    if (!active_) return;
    if (dirty_) {
        rebuildGrid();
        rebuildInfo(nowMs_);
        dirty_ = false;
    }
    C2D_DrawText(&titleText_, C2D_WithColor, 4, 3, 0.5f, 0.5f, 0.5f, kAccent);
    C2D_DrawText(&beansText_, C2D_WithColor, 96, 4, 0.5f, 0.45f, 0.45f, cart_.money > 0 ? kWhite : kBad);
    C2D_DrawText(&carryText_, C2D_WithColor, 186, 4, 0.5f, 0.45f, 0.45f, kWhite);
    C2D_DrawText(&timerText_, C2D_WithColor, 280, 4, 0.5f, 0.45f, 0.45f, kDim);
    for (int t = 0; t < gags::kTracks; t++) {
        const float y = kGridY + t * kCellH;
        C2D_DrawText(&trackText_[t], C2D_WithColor, kNameX, y + 5, 0.5f, 0.4f, 0.4f,
                     profile_.trackAccess[t] ? kWhite : kDim);
        for (int l = 0; l < kCols; l++) {
            const float x = kGridX + l * kCellW;
            const bool open = profile_.trackAccess[t] && shop::expLevel(t, profile_.experience[t]) >= l;
            const bool canBuy = open && shop::canBuy(profile_, cart_.inv, cart_.money, t, l) == shop::Buy::Ok;
            const bool sel = t == selTrack_ && l == selLevel_;
            const bool bought = cart_.bought(t, l) > 0;
            if (sel) C2D_DrawRectSolid(x, y, 0.5f, kCellW - 1, kCellH - 1, kWhite);
            const float in = sel ? 2.0f : 0.0f;
            C2D_DrawRectSolid(x + in, y + in, 0.52f, kCellW - 1 - 2 * in, kCellH - 1 - 2 * in,
                              !open ? kLocked : bought ? kBought : kPanel);
            if (open) {
                world::GagAtlas::instance().drawIcon(t, l, x + 2, y + 2, 16.0f, 0.54f, canBuy ? 1.0f : 0.45f);
            }
            C2D_DrawText(&cellText_[t][l], C2D_WithColor, x + (open ? 18 : 6), y + 5, 0.55f, 0.4f, 0.4f,
                         !open ? kDim : bought ? kAccent : canBuy ? kWhite : kDim);
        }
    }
    C2D_DrawText(&infoText_, C2D_WithColor, 4, kInfoY, 0.5f, 0.42f, 0.42f, note_[0] ? kBad : kGood);
    const bool canSell = cart_.bought(selTrack_, selLevel_) > 0;
    const bool canAdd = shop::canBuy(profile_, cart_.inv, cart_.money, selTrack_, selLevel_) == shop::Buy::Ok;
    C2D_DrawRectSolid(kMinusX, kBtnY, 0.5f, kBtnSmallW, kBtnH, canSell ? kPanel : kLocked);
    C2D_DrawRectSolid(kPlusX, kBtnY, 0.5f, kBtnSmallW, kBtnH, canAdd ? kPanel : kLocked);
    C2D_DrawRectSolid(kDoneX, kBtnY, 0.5f, kDoneW, kBtnH, kAccent);
    C2D_DrawRectSolid(kCancelX, kBtnY, 0.5f, kCancelW, kBtnH, kPanel);
    C2D_DrawText(&btnText_[0], C2D_WithColor, kMinusX + 18, kBtnY + 6, 0.55f, 0.8f, 0.8f, canSell ? kWhite : kDim);
    C2D_DrawText(&btnText_[1], C2D_WithColor, kPlusX + 16, kBtnY + 6, 0.55f, 0.8f, 0.8f, canAdd ? kWhite : kDim);
    C2D_DrawText(&btnText_[2], C2D_WithColor, kDoneX + 14, kBtnY + 10, 0.55f, 0.55f, 0.55f, kBg);
    C2D_DrawText(&btnText_[3], C2D_WithColor, kCancelX + 8, kBtnY + 10, 0.55f, 0.5f, 0.5f, kWhite);
}

} // namespace tt::ui
