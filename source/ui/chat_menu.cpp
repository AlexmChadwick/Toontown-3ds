#include "ui/chat_menu.h"

#include <cstdio>
#include <cstring>

#include "core/log.h"

namespace tt::ui {

namespace {

constexpr float kZ = 0.5f;
constexpr float kMarginX = 6.0f;
constexpr float kRowGap = 2.0f;
constexpr float kArrowW = 18.0f;
constexpr float kScrollBarW = 4.0f;
constexpr float kBackW = 64.0f;
constexpr float kLabelScale = 0.5f, kMinLabelScale = 0.4f;

// Same palette as render/ui.cpp.
const u32 kBg = C2D_Color32(0x1E, 0x2A, 0x44, 0xFF);
const u32 kPanel = C2D_Color32(0x2C, 0x3C, 0x60, 0xFF);
const u32 kPanelMenu = C2D_Color32(0x34, 0x4C, 0x7C, 0xFF);
const u32 kPressed = C2D_Color32(0x4A, 0x64, 0x9C, 0xFF);
const u32 kAccent = C2D_Color32(0xF5, 0xB8, 0x1A, 0xFF);
const u32 kWhite = C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF);
const u32 kDim = C2D_Color32(0xA8, 0xB4, 0xCC, 0xFF);
const u32 kMenuText = C2D_Color32(0xFF, 0xE0, 0x8A, 0xFF);

float rowTextW(bool isMenu) { return ChatMenu::kW - 2 * kMarginX - 12.0f - kScrollBarW - (isMenu ? kArrowW : 0.0f); }

} // namespace

bool ChatMenu::init() {
    setEmoteAccess(nullptr);
    // Breadcrumb + every label of the largest menu, plus slack for wrapping.
    menuBuf_ = C2D_TextBufNew(sc::kMaxMenuGlyphs + 96);
    staticBuf_ = C2D_TextBufNew(96);
    if (!menuBuf_ || !staticBuf_) return false;
    C2D_TextParse(&back_, staticBuf_, "< Back");
    C2D_TextParse(&arrow_, staticBuf_, ">");
    C2D_TextParse(&hint_, staticBuf_, "Up/Down + A");
    C2D_TextOptimize(&back_);
    C2D_TextOptimize(&arrow_);
    C2D_TextOptimize(&hint_);
    enter(0, -1);
    return true;
}

void ChatMenu::shutdown() {
    if (menuBuf_) C2D_TextBufDelete(menuBuf_);
    if (staticBuf_) C2D_TextBufDelete(staticBuf_);
    menuBuf_ = staticBuf_ = nullptr;
}

void ChatMenu::enter(int menu, int focusItem) {
    menu_ = menu;
    const sc::Menu& m = sc::kMenus[menu];
    C2D_TextBufClear(menuBuf_);

    // Breadcrumb: "PLACES > COGS" (the root just says SpeedChat).
    char crumb[96];
    if (m.parent < 0 || sc::kMenus[m.parent].parent < 0) {
        snprintf(crumb, sizeof(crumb), "%s", m.title);
    } else {
        snprintf(crumb, sizeof(crumb), "%s > %s", sc::kMenus[m.parent].title, m.title);
    }
    C2D_TextParse(&title_, menuBuf_, crumb);
    C2D_TextOptimize(&title_);
    float tw, th;
    C2D_TextGetDimensions(&title_, 0.5f, 0.5f, &tw, &th);
    const float titleAvail = kW - kBackW - 2 * kMarginX - 8.0f;
    titleScale_ = tw > titleAvail ? 0.5f * titleAvail / tw : 0.5f;

    for (int i = 0; i < m.numItems; i++) {
        const sc::Item& it = sc::kItems[m.firstItem + i];
        C2D_TextParse(&labels_[i], menuBuf_, it.label);
        C2D_TextOptimize(&labels_[i]);
        float w, h;
        C2D_TextGetDimensions(&labels_[i], kLabelScale, kLabelScale, &w, &h);
        const float avail = rowTextW(it.isMenu);
        float s = kLabelScale;
        if (w > avail) s = kLabelScale * avail / w;
        labelWrap_[i] = s < kMinLabelScale;
        if (labelWrap_[i]) s = kMinLabelScale + 0.02f; // two lines at 0.42 fit a 30 px row
        labelScale_[i] = s;
        C2D_TextGetDimensions(&labels_[i], s, s, &w, &h);
        labelH_[i] = labelWrap_[i] ? h * 2.0f : h;
    }
    scroll_ = 0.0f;
    focus_ = focusItem;
    pressedRow_ = -1;
    touching_ = dragged_ = pressedBack_ = false;
}

void ChatMenu::blur() {
    focus_ = -1;
    pressedRow_ = -1;
    touching_ = dragged_ = pressedBack_ = false;
}

float ChatMenu::maxScroll(float listH) const {
    const float content = sc::kMenus[menu_].numItems * kRowH;
    return content > listH ? content - listH : 0.0f;
}

void ChatMenu::ensureVisible(int item, float listH) {
    const float top = item * kRowH, bottom = top + kRowH;
    if (top < scroll_) scroll_ = top;
    if (bottom > scroll_ + listH) scroll_ = bottom - listH;
}

int ChatMenu::rowAt(float y, float listH) const {
    if (y < kHeaderH || y >= kHeaderH + listH) return -1;
    const int row = static_cast<int>((y - kHeaderH + scroll_) / kRowH);
    return row < sc::kMenus[menu_].numItems ? row : -1;
}

void ChatMenu::activate(int item) {
    const sc::Menu& m = sc::kMenus[menu_];
    if (item < 0 || item >= m.numItems) return;
    const sc::Item& it = sc::kItems[m.firstItem + item];
    if (it.isMenu) {
        enter(it.value, focus_ >= 0 ? 0 : -1);
        return;
    }
    if (qCount_ < kQueueLen) {
        if (it.value==65535) {
            if (it.emote<0 || it.emote>=25 || !emoteAccess_[it.emote]) return;
            queue_[(qHead_ + qCount_) % kQueueLen]=-1-it.emote;
        } else queue_[(qHead_ + qCount_) % kQueueLen] = it.value;
        qCount_++;
        if (it.value!=65535 && it.emote>=0 && it.emote<25 && emoteAccess_[it.emote] && qCount_<kQueueLen) {
            queue_[(qHead_+qCount_)%kQueueLen]=-1-it.emote; ++qCount_;
        }
    }
    enter(0, -1);
}

void ChatMenu::goBack(float listH) {
    const int parent = sc::kMenus[menu_].parent;
    if (parent < 0) {
        focus_ = -1;
        return;
    }
    // Re-highlight the sub-menu we came from (only in D-pad mode).
    int from = -1;
    const sc::Menu& pm = sc::kMenus[parent];
    for (int i = 0; i < pm.numItems; i++) {
        const sc::Item& it = sc::kItems[pm.firstItem + i];
        if (it.isMenu && it.value == menu_) from = i;
    }
    const bool dpad = focus_ >= 0;
    enter(parent, dpad ? from : -1);
    if (from >= 0) ensureVisible(from, listH);
}

bool ChatMenu::popSpeech(int& msgIndex) {
    if (qCount_ == 0) return false;
    msgIndex = queue_[qHead_];
    qHead_ = (qHead_ + 1) % kQueueLen;
    qCount_--;
    return true;
}

u32 ChatMenu::handleInput(u32 down, u32 held, const touchPosition& t, float panelBottom) {
    const float listH = panelBottom - kHeaderH;
    const int n = sc::kMenus[menu_].numItems;
    u32 consumed = 0;

    // Buttons.
    if (down & (KEY_DUP | KEY_DDOWN)) {
        if (focus_ < 0) {
            focus_ = rowAt(kHeaderH + 1.0f, listH); // top visible row
            if (focus_ < 0) focus_ = 0;
        } else if (down & KEY_DUP) {
            focus_ = focus_ > 0 ? focus_ - 1 : n - 1;
        } else {
            focus_ = focus_ < n - 1 ? focus_ + 1 : 0;
        }
        ensureVisible(focus_, listH);
        TT_LOG("padnav: chat focus %d", focus_);
        consumed |= down & (KEY_DUP | KEY_DDOWN);
    } else if ((down & KEY_A) && focus_ >= 0) {
        TT_LOG("padnav: chat activate %d", focus_);
        activate(focus_);
        consumed |= KEY_A;
    } else if ((down & KEY_B) && focus_ >= 0) {
        TT_LOG("padnav: chat back");
        goBack(listH);
        consumed |= KEY_B;
    }

    // Touch: press -> track -> release. Taps fire on release so drags can scroll.
    const bool touchHeld = (held & KEY_TOUCH) != 0;
    if ((down & KEY_TOUCH) && t.py < panelBottom) {
        touching_ = true;
        dragged_ = false;
        touchStartX_ = t.px;
        touchStartY_ = t.py;
        scrollStart_ = scroll_;
        pressedBack_ = sc::kMenus[menu_].parent >= 0 && t.py < kHeaderH && t.px < kBackW + kMarginX;
        pressedRow_ = rowAt(t.py, listH);
    } else if (touching_ && touchHeld) {
        const float dy = t.py - touchStartY_, dx = t.px - touchStartX_;
        if (!dragged_ && (dy * dy + dx * dx) > kTapSlop * kTapSlop) {
            dragged_ = true;
            pressedBack_ = false;
            pressedRow_ = -1;
        }
        if (dragged_) {
            scroll_ = scrollStart_ - dy;
            const float ms = maxScroll(listH);
            if (scroll_ < 0) scroll_ = 0;
            if (scroll_ > ms) scroll_ = ms;
        }
    }
    if (touching_ && !touchHeld) { // released (same frame as the press for scripted taps)
        touching_ = false;
        if (!dragged_) {
            if (pressedBack_) {
                focus_ = -1;
                goBack(listH);
            } else if (pressedRow_ >= 0) {
                focus_ = -1; // touch use hides the D-pad highlight
                activate(pressedRow_);
            }
        }
        pressedRow_ = -1;
        pressedBack_ = false;
    }
    return consumed;
}

void ChatMenu::draw(float panelBottom) {
    const float listH = panelBottom - kHeaderH;
    const sc::Menu& m = sc::kMenus[menu_];

    // Rows (drawn first; the header is drawn over rows scrolled above it, the tab bar
    // over rows below).
    const int firstRow = static_cast<int>(scroll_ / kRowH);
    const int lastRow = static_cast<int>((scroll_ + listH) / kRowH);
    const float rowX = kMarginX, rowW = kW - 2 * kMarginX - kScrollBarW - 4.0f;
    for (int i = firstRow; i <= lastRow && i < m.numItems; i++) {
        const sc::Item& it = sc::kItems[m.firstItem + i];
        const float y = kHeaderH + i * kRowH - scroll_;
        const bool focused = i == focus_;
        const u32 bg = focused ? kAccent : (i == pressedRow_ ? kPressed : (it.isMenu ? kPanelMenu : kPanel));
        C2D_DrawRectSolid(rowX, y + kRowGap * 0.5f, kZ, rowW, kRowH - kRowGap, bg);
        const bool locked = it.value==65535 && it.emote>=0 && !emoteAccess_[it.emote];
        const u32 fg = locked ? kDim : focused ? kBg : (it.isMenu ? kMenuText : kWhite);
        const float s = labelScale_[i];
        const float ty = y + (kRowH - labelH_[i]) * 0.5f;
        if (labelWrap_[i]) {
            C2D_DrawText(&labels_[i], C2D_WithColor | C2D_WordWrap, rowX + 6.0f, ty, kZ, s, s, fg,
                         rowTextW(it.isMenu));
        } else {
            C2D_DrawText(&labels_[i], C2D_WithColor, rowX + 6.0f, ty, kZ, s, s, fg);
        }
        if (it.isMenu) {
            C2D_DrawText(&arrow_, C2D_WithColor, rowX + rowW - kArrowW + 4.0f, y + 6.0f, kZ, 0.55f, 0.55f, fg);
        }
    }

    // Scroll indicator.
    const float ms = maxScroll(listH);
    if (ms > 0.0f) {
        const float content = m.numItems * kRowH;
        const float barH = listH * listH / content;
        const float barY = kHeaderH + (listH - barH) * (scroll_ / ms);
        C2D_DrawRectSolid(kW - kMarginX - kScrollBarW, kHeaderH, kZ, kScrollBarW, listH, kPanel);
        C2D_DrawRectSolid(kW - kMarginX - kScrollBarW, barY, kZ, kScrollBarW, barH, kDim);
    }

    // Header.
    C2D_DrawRectSolid(0, 0, kZ, kW, kHeaderH, kBg);
    float titleX = kMarginX + 2.0f;
    if (m.parent >= 0) {
        C2D_DrawRectSolid(kMarginX, 2, kZ, kBackW, kHeaderH - 4, pressedBack_ ? kPressed : kPanelMenu);
        C2D_DrawText(&back_, C2D_WithColor, kMarginX + 8.0f, 6.0f, kZ, 0.5f, 0.5f, kWhite);
        titleX = kMarginX + kBackW + 8.0f;
    } else if (focus_ < 0) {
        C2D_DrawText(&hint_, C2D_WithColor, kW - 92.0f, 8.0f, kZ, 0.42f, 0.42f, kDim);
    }
    C2D_DrawText(&title_, C2D_WithColor, titleX, 6.0f + (0.5f - titleScale_) * 14.0f, kZ, titleScale_, titleScale_,
                 kAccent);
}

} // namespace tt::ui
