#include "render/ui_font.h"
#include "ui/tailor_panel.h"

#include <cstdio>

#include "core/log.h"
#include "ui/pad_tables.h"
#include "ui/theme.h"

namespace tt::ui {

namespace {

constexpr float kTopY = pad::tailor::kTopY;
constexpr float kBottomY = pad::tailor::kBottomY;
constexpr float kButtonY = pad::tailor::kBtnY;
constexpr uint32_t kAutoSubmitMarginMs = 2000;
const u32 kWhite = C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF);

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
    tt::render::textParse(&titleText_, textBuf_, "Clothing Store");
    // As the Make-a-Toon clothes step: which of the tailor's choices is on the toon.
    snprintf(buf, sizeof(buf), "Shirt   %u of %u", unsigned(topChoice_ + 1), unsigned(world::matShirtCount(working_.gender)));
    tt::render::textParse(&topText_, textBuf_, buf);
    snprintf(buf, sizeof(buf), "%s   %u of %u", working_.gender == world::Gender::Male ? "Shorts" : "Bottoms",
             unsigned(bottomChoice_ + 1), unsigned(world::matBottomCount(working_.gender)));
    tt::render::textParse(&bottomText_, textBuf_, buf);
    tt::render::textParse(&hintText_, textBuf_, "Up/Down picks shirt or bottoms; Left/Right changes it.");
    const int32_t remain = int32_t(deadlineMs_ - nowMs_);
    snprintf(buf, sizeof(buf), "Time %d:%02d", remain > 0 ? int(remain / 60000) : 0,
             remain > 0 ? int((remain / 1000) % 60) : 0);
    tt::render::textParse(&timerText_, textBuf_, buf);
    tt::render::textParse(&doneText_, textBuf_, "Done");
    tt::render::textParse(&cancelText_, textBuf_, "Cancel");
    C2D_Text* all[] = {&titleText_, &topText_, &bottomText_, &hintText_, &timerText_, &doneText_, &cancelText_};
    for (C2D_Text* t : all) C2D_TextOptimize(t);
}

void TailorPanel::draw() {
    if (!active_) return;
    if (dirty_) {
        rebuildText();
        dirty_ = false;
    }
    // MakeClothesGUI in the tailor's shop: the clothes choices on cream cards, Done / Cancel.
    namespace th = theme;
    th::sky();
    th::paper(4, 3, 312, kTopY - 8, 7.0f);
    C2D_DrawText(&titleText_, C2D_WithColor, 12, 7, 0.5f, 0.58f, 0.58f, th::kRedDark);
    th::roundRect(232, 7, 80, 20, 7, th::kYellow);
    C2D_DrawText(&timerText_, C2D_WithColor, 240, 9, 0.5f, 0.42f, 0.42f, th::kInk);
    for (int sec = 0; sec < 2; ++sec) {
        const float y = sec == 0 ? kTopY : kBottomY;
        const bool on = section_ == sec;
        th::paper(4, y, 312, 48, 7.0f, on ? th::rgb(0xFF, 0xEE, 0xA8) : th::kPaper, on ? th::kYellowDark : th::kPaperEdge);
    }
    // Touching a row's left / right half steps back / forward (handleInput): show it as arrows.
    const float rowsY[2] = {kTopY, kBottomY};
    for (float y : rowsY) {
        for (int side = 0; side < 2; ++side) {
            const float bx = side ? 276 : 12, cy = y + 22;
            th::button(bx, y + 8, 32, 32, th::kScFrame, th::kScFrameEdge, false, 7.0f);
            const float tip = side ? bx + 22 : bx + 10, base = side ? bx + 12 : bx + 20;
            C2D_DrawTriangle(tip, cy - 1.5f, th::kBlueDark, base, cy - 8.5f, th::kBlueDark, base, cy + 5.5f, th::kBlueDark, 0.55f);
        }
    }
    th::textCentered(topText_, 160, kTopY + 14, 0.5f, th::kInk, 220);
    th::textCentered(bottomText_, 160, kBottomY + 14, 0.5f, th::kInk, 220);
    th::roundRect(4, 150, 312, 20, 6, th::mix(th::kPaper, th::kSkyBottom, 0.3f));
    C2D_DrawText(&hintText_, C2D_WithColor, 10, 153, 0.5f, 0.4f, 0.4f, th::kInkSoft);
    th::button(160, kButtonY, 76, 36, th::kGreen, th::kGreenDark, false, 7.0f);
    th::button(240, kButtonY, 76, 36, th::kRed, th::kRedDark, false, 7.0f);
    th::textCentered(doneText_, 198, kButtonY + 8, 0.52f, th::kWhite, 70);
    th::textCentered(cancelText_, 278, kButtonY + 8, 0.5f, th::kWhite, 70);
    focus_.drawHighlight(); // padnav
}

} // namespace tt::ui
