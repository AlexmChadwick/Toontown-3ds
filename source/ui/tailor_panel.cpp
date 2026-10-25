#include "ui/tailor_panel.h"

#include <cstdio>

#include "core/log.h"
#include "ui/pad_tables.h"

namespace tt::ui {

namespace {

constexpr float kTopY = pad::tailor::kTopY;
constexpr float kBottomY = pad::tailor::kBottomY;
constexpr float kButtonY = pad::tailor::kBtnY;
constexpr uint32_t kAutoSubmitMarginMs = 2000;
const u32 kBg = C2D_Color32(0x1E, 0x2A, 0x44, 0xFF);
const u32 kPanel = C2D_Color32(0x2C, 0x3C, 0x60, 0xFF);
const u32 kAccent = C2D_Color32(0xF5, 0xB8, 0x1A, 0xFF);
const u32 kWhite = C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF);
const u32 kDim = C2D_Color32(0xA8, 0xB4, 0xCC, 0xFF);
const u32 kGood = C2D_Color32(0x8C, 0xF0, 0x8C, 0xFF);

bool inRect(const touchPosition& t, float x, float y, float w, float h) {
    return t.px >= x && t.px < x + w && t.py >= y && t.py < y + h;
}

} // namespace

bool TailorPanel::init() {
    textBuf_ = C2D_TextBufNew(512);
    pad::buildTailor(focus_); // padnav
    return textBuf_ != nullptr;
}

void TailorPanel::shutdown() {
    if (textBuf_) C2D_TextBufDelete(textBuf_);
    textBuf_ = nullptr;
}

void TailorPanel::setPreview(world::ToonCache* cache, world::ToonLook* look) {
    cache_ = cache;
    look_ = look;
}

void TailorPanel::dismiss() {
    if (active_) {
        working_ = original_;
        preview();
        close();
    }
}

void TailorPanel::close() {
    active_ = false;
    tailor_ = 0;
    pendingSend_ = 0;
}

bool TailorPanel::topMatches(const world::ToonDna& dna, int i) const {
    const world::MatShirtChoice s = world::matShirt(dna.gender, static_cast<uint8_t>(i));
    return dna.topTex == s.topTex && dna.topTexColor == s.topTexColor && dna.sleeveTex == s.sleeveTex &&
           dna.sleeveTexColor == s.sleeveTexColor;
}

bool TailorPanel::bottomMatches(const world::ToonDna& dna, int i) const {
    const world::MatBottomChoice b = world::matBottom(dna.gender, static_cast<uint8_t>(i));
    return dna.botTex == b.botTex && dna.botTexColor == b.botTexColor;
}

void TailorPanel::preview() {
    if (cache_ && look_) cache_->acquire(working_, world::ToonLod::High, look_);
}

void TailorPanel::onMovie(const net::Client& client, uint32_t nowMs) {
    const net::TailorMovie& m = client.tailorMovie();
    if (m.avId != client.avatarId() && m.mode != net::kTailorMovieClear) return;
    if (m.mode == net::kTailorMovieStart || m.mode == net::kTailorMovieStartBrowse ||
        m.mode == net::kTailorMovieStartNoRoom) {
        if (active_) return;
        if (!world::parseNetString(client.avatarDna(), net::kDnaSize, &original_)) return;
        working_ = original_;
        tailor_ = m.tailorDoId;
        deadlineMs_ = m.deadlineMs;
        nowMs_ = nowMs;
        section_ = 0;
        focus_.setFocusById(pad::tailor::kIdShirt); // padnav
        topChoice_ = 0;
        bottomChoice_ = 0;
        for (int i = 0; i < world::matShirtCount(working_.gender); i++)
            if (topMatches(working_, i)) topChoice_ = i;
        for (int i = 0; i < world::matBottomCount(working_.gender); i++)
            if (bottomMatches(working_, i)) bottomChoice_ = i;
        changedTop_ = false;
        changedBottom_ = false;
        pendingSend_ = 0;
        notice_ = nullptr;
        active_ = true;
        dirty_ = true;
        TT_LOG("tailor: open with NPC %u", unsigned(tailor_));
    } else if (m.mode == net::kTailorMovieTimeout) {
        if (active_) {
            working_ = original_;
            preview();
            close();
        }
        notice_ = "The tailor needed more time.";
    } else if (m.mode == net::kTailorMovieComplete) {
        close();
        notice_ = "Your new clothes look great!";
    } else if (m.mode == net::kTailorMovieNoMoney) {
        close();
        notice_ = "You need a clothing ticket.";
    } else if (m.mode == net::kTailorMovieClear && m.tailorDoId == tailor_) {
        close();
    }
}

void TailorPanel::choose(int delta) {
    const int count = section_ == 0 ? world::matShirtCount(working_.gender) : world::matBottomCount(working_.gender);
    int& choice = section_ == 0 ? topChoice_ : bottomChoice_;
    choice = (choice + delta + count) % count;
    if (section_ == 0) {
        const world::MatShirtChoice s = world::matShirt(working_.gender, static_cast<uint8_t>(choice));
        working_.topTex = s.topTex;
        working_.topTexColor = s.topTexColor;
        working_.sleeveTex = s.sleeveTex;
        working_.sleeveTexColor = s.sleeveTexColor;
        changedTop_ = !topMatches(original_, choice);
    } else {
        const world::MatBottomChoice b = world::matBottom(working_.gender, static_cast<uint8_t>(choice));
        working_.botTex = b.botTex;
        working_.botTexColor = b.botTexColor;
        if (working_.gender == world::Gender::Female) working_.torso = (working_.torso % 3) + (b.skirt ? 3 : 0);
        changedBottom_ = !bottomMatches(original_, choice);
    }
    preview();
    dirty_ = true;
}

void TailorPanel::update(net::Client& client, uint32_t nowMs) {
    nowMs_ = nowMs;
    if (!active_) return;
    if (!client.inZone() || !client.tailorOpenFor(tailor_)) {
        working_ = original_;
        preview();
        close();
        return;
    }
    if (!pendingSend_ && int32_t(deadlineMs_ - nowMs) <= int32_t(kAutoSubmitMarginMs)) pendingSend_ = 2;
    if (!pendingSend_) return;
    const bool cancel = pendingSend_ == 2;
    const world::ToonDna& dna = cancel ? original_ : working_;
    uint8_t blob[net::kDnaSize];
    world::makeNetString(dna, blob);
    const uint8_t which = cancel ? 0 : uint8_t((changedTop_ ? net::kTailorWhichShirt : 0) |
                                               (changedBottom_ ? net::kTailorWhichShorts : 0));
    // DistributedNPCTailorAI.setDNA: finished 2 + which buys (needs a clothing ticket from a
    // ToonTask reward, or the tailor's freeClothes); finished 1 puts the old clothes back.
    client.sendTailorDNA(tailor_, blob, cancel || !which ? 1 : 2, which);
    if (cancel) {
        working_ = original_;
        preview();
    }
    close();
}

u32 TailorPanel::handleInput(u32 down, const touchPosition& touch) {
    if (!active_) return 0;
    // ---- padnav: D-Up / D-Down pick the shirt row, the shorts row, then Done / Cancel;
    // Left / Right change the style on a row or move between the buttons; A presses a button.
    const int focused = focus_.focusedId();
    const bool onButtons = focused == pad::tailor::kIdDone || focused == pad::tailor::kIdCancel;
    if (down & KEY_DUP) {
        section_ = onButtons ? 1 : 0;
        focus_.setFocusById(section_ == 0 ? pad::tailor::kIdShirt : pad::tailor::kIdShorts);
    } else if (down & KEY_DDOWN) {
        if (section_ == 1 && !onButtons) {
            focus_.setFocusById(pad::tailor::kIdDone);
        } else if (!onButtons) {
            section_ = 1;
            focus_.setFocusById(pad::tailor::kIdShorts);
        }
    }
    if (onButtons) {
        if (down & KEY_DLEFT) focus_.move(-1, 0);
        if (down & KEY_DRIGHT) focus_.move(1, 0);
        if (down & KEY_A) {
            focus_.logActivate(focused);
            pendingSend_ = focused == pad::tailor::kIdDone ? 1 : 2;
        }
    } else {
        if (down & KEY_DLEFT) choose(-1);
        if (down & KEY_DRIGHT) choose(1);
    }
    // ---- end padnav ----
    if (down & KEY_START) pendingSend_ = 1;
    if (down & KEY_B) pendingSend_ = 2;
    if (down & KEY_TOUCH) {
        if (inRect(touch, 0, kTopY, 320, 48)) {
            section_ = 0;
            focus_.setFocusById(pad::tailor::kIdShirt); // padnav
            choose(touch.px < 160 ? -1 : 1);
        } else if (inRect(touch, 0, kBottomY, 320, 48)) {
            section_ = 1;
            focus_.setFocusById(pad::tailor::kIdShorts); // padnav
            choose(touch.px < 160 ? -1 : 1);
        } else if (inRect(touch, 160, kButtonY, 76, 36)) {
            focus_.setFocusById(pad::tailor::kIdDone); // padnav
            pendingSend_ = 1;
        } else if (inRect(touch, 240, kButtonY, 76, 36)) {
            focus_.setFocusById(pad::tailor::kIdCancel); // padnav
            pendingSend_ = 2;
        }
    }
    return 0xFFFFFFFFu;
}

void TailorPanel::rebuildText() {
    C2D_TextBufClear(textBuf_);
    char buf[96];
    C2D_TextParse(&titleText_, textBuf_, "Clothing Store");
    const world::MatShirtChoice shirt = world::matShirt(working_.gender, static_cast<uint8_t>(topChoice_));
    snprintf(buf, sizeof(buf), "Shirt  style %u  colour %u/%u", unsigned(shirt.topTex), unsigned(shirt.topTexColor),
             unsigned(shirt.sleeveTexColor));
    C2D_TextParse(&topText_, textBuf_, buf);
    const world::MatBottomChoice bottom = world::matBottom(working_.gender, static_cast<uint8_t>(bottomChoice_));
    snprintf(buf, sizeof(buf), "%s  style %u  colour %u", working_.gender == world::Gender::Male ? "Shorts" : "Bottoms",
             unsigned(bottom.botTex), unsigned(bottom.botTexColor));
    C2D_TextParse(&bottomText_, textBuf_, buf);
    C2D_TextParse(&hintText_, textBuf_, "Left/Right changes the selected style and colour");
    const int32_t remain = int32_t(deadlineMs_ - nowMs_);
    snprintf(buf, sizeof(buf), "Time %d:%02d", remain > 0 ? int(remain / 60000) : 0,
             remain > 0 ? int((remain / 1000) % 60) : 0);
    C2D_TextParse(&timerText_, textBuf_, buf);
    C2D_TextParse(&doneText_, textBuf_, "Done");
    C2D_TextParse(&cancelText_, textBuf_, "Cancel");
    C2D_Text* all[] = {&titleText_, &topText_, &bottomText_, &hintText_, &timerText_, &doneText_, &cancelText_};
    for (C2D_Text* t : all) C2D_TextOptimize(t);
}

void TailorPanel::draw() {
    if (!active_) return;
    if (dirty_) {
        rebuildText();
        dirty_ = false;
    }
    C2D_DrawText(&titleText_, C2D_WithColor, 8, 8, 0.5f, 0.6f, 0.6f, kAccent);
    C2D_DrawText(&timerText_, C2D_WithColor, 238, 10, 0.5f, 0.42f, 0.42f, kDim);
    C2D_DrawRectSolid(4, kTopY, 0.5f, 312, 48, section_ == 0 ? kAccent : kPanel);
    C2D_DrawRectSolid(4, kBottomY, 0.5f, 312, 48, section_ == 1 ? kAccent : kPanel);
    C2D_DrawText(&topText_, C2D_WithColor, 12, kTopY + 14, 0.5f, 0.48f, 0.48f, kWhite);
    C2D_DrawText(&bottomText_, C2D_WithColor, 12, kBottomY + 14, 0.5f, 0.5f, 0.5f, kWhite);
    C2D_DrawText(&hintText_, C2D_WithColor, 8, 154, 0.5f, 0.38f, 0.38f, kDim);
    C2D_DrawRectSolid(160, kButtonY, 0.5f, 76, 36, kAccent);
    C2D_DrawRectSolid(240, kButtonY, 0.5f, 76, 36, kPanel);
    C2D_DrawText(&doneText_, C2D_WithColor, 180, kButtonY + 11, 0.5f, 0.52f, 0.52f, kBg);
    C2D_DrawText(&cancelText_, C2D_WithColor, 248, kButtonY + 11, 0.5f, 0.46f, 0.46f, kGood);
    focus_.drawHighlight(); // padnav
}

} // namespace tt::ui
