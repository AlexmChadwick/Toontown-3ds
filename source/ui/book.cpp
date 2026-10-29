#include "render/ui_font.h"
#include "ui/book.h"
#include "ui/theme.h"
#include <cstdio>

namespace tt::ui {

namespace {

constexpr u32 kCoverHeader = C2D_Color32(0x6E, 0x12, 0x12, 0xFF);
constexpr u32 kCoverBottom = C2D_Color32(0x50, 0x0C, 0x0C, 0xFF);
constexpr u32 kPaperBg = C2D_Color32(0xFA, 0xF6, 0xEE, 0xFF);
constexpr u32 kPaperBorder = C2D_Color32(0xCC, 0xC4, 0xB4, 0xFF);
constexpr u32 kTabActive = C2D_Color32(0xF5, 0xB8, 0x1A, 0xFF);
constexpr u32 kTabInactive = C2D_Color32(0x82, 0x1C, 0x1C, 0xFF);
constexpr u32 kInk = C2D_Color32(0x20, 0x18, 0x10, 0xFF);
constexpr u32 kWhite = C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF);
constexpr u32 kGoldText = C2D_Color32(0xFF, 0xEE, 0xAA, 0xFF);
constexpr u32 kCloseText = C2D_Color32(0xFF, 0x88, 0x88, 0xFF);

} // namespace

bool Book::init() {
    staticBuf_ = C2D_TextBufNew(1024);
    dynamicBuf_ = C2D_TextBufNew(512);
    if (!staticBuf_ || !dynamicBuf_) return false;

    tt::render::textParse(&prevText_, staticBuf_, "< Prev");
    tt::render::textParse(&nextText_, staticBuf_, "Next >");
    tt::render::textParse(&closeText_, staticBuf_, "X");

    for (int i = 0; i < kPageCount; i++) {
        tt::render::textParse(&tabTexts_[i], staticBuf_, pages_[i]->tabName());
        pages_[i]->init();
    }

    return true;
}

void Book::shutdown() {
    for (int i = 0; i < kPageCount; i++) {
        pages_[i]->shutdown();
    }
    if (staticBuf_) {
        C2D_TextBufDelete(staticBuf_);
        staticBuf_ = nullptr;
    }
    if (dynamicBuf_) {
        C2D_TextBufDelete(dynamicBuf_);
        dynamicBuf_ = nullptr;
    }
}

void Book::open(int page) {
    isOpen_ = true;
    if (page >= 0 && page < kPageCount) {
        setPage(page);
    } else {
        pages_[curPage_]->enter();
    }
}

void Book::close() {
    if (isOpen_) {
        pages_[curPage_]->exit();
        isOpen_ = false;
    }
}

void Book::toggle() {
    if (isOpen_) close();
    else open();
}

void Book::setPage(int page) {
    if (page >= 0 && page < kPageCount) {
        if (page != curPage_) {
            pages_[curPage_]->exit();
            curPage_ = page;
            pages_[curPage_]->enter();
        }
    }
}

void Book::nextPage() {
    setPage((curPage_ + 1) % kPageCount);
}

void Book::prevPage() {
    setPage((curPage_ + kPageCount - 1) % kPageCount);
}

u32 Book::handleInput(u32 keysDown, const touchPosition& touch, BookDrawContext& bdc) {
    if (!isOpen_) return 0;

    // ---- padnav: B closes the book like START (upstream's book has a close button only) ----
    if (keysDown & (KEY_START | KEY_B)) {
        close();
        return keysDown & (KEY_START | KEY_B);
    }

    if (keysDown & KEY_L) {
        prevPage();
        return KEY_L;
    }

    if (keysDown & KEY_R) {
        nextPage();
        return KEY_R;
    }
    // ---- end padnav ----

    if (keysDown & KEY_TOUCH) {
        // Top bar buttons
        if (touch.py < 24) {
            if (touch.px < 55) {
                prevPage();
                return KEY_TOUCH;
            }
            if (touch.px >= 220 && touch.px < 275) {
                nextPage();
                return KEY_TOUCH;
            }
            if (touch.px >= 280) {
                close();
                return KEY_TOUCH;
            }
        }
        // Bottom tab bar buttons
        else if (touch.py >= 214) {
            const float tabW = 320.0f / kPageCount;
            const int t = static_cast<int>(touch.px / tabW);
            if (t >= 0 && t < kPageCount) {
                setPage(t);
                return KEY_TOUCH;
            }
        }
    }

    // ---- padnav: the page sees the D-Pad and A first (options, map hoods, departments,
    // genus arrows); a D-Left / D-Right the page does not use turns the page as before.
    // Pages get only touches between the top bar and the tab bar, as before.
    constexpr float kTopBarH = 24.0f;
    constexpr float kTabBarY = 214.0f;
    const bool pageTouch = (keysDown & KEY_TOUCH) && touch.py >= kTopBarH && touch.py < kTabBarY;
    const u32 pageKeys = pageTouch ? keysDown : (keysDown & ~KEY_TOUCH);
    u32 consumed = pages_[curPage_]->handleInput(pageKeys, touch, bdc);
    if (pageTouch) consumed |= KEY_TOUCH; // a touch on the page never reaches the world
    if ((keysDown & KEY_DLEFT) && !(consumed & KEY_DLEFT)) {
        prevPage();
        consumed |= KEY_DLEFT;
    } else if ((keysDown & KEY_DRIGHT) && !(consumed & KEY_DRIGHT)) {
        nextPage();
        consumed |= KEY_DRIGHT;
    }
    return consumed;
    // ---- end padnav ----
}

void Book::draw(const BookDrawContext& bdc) {
    if (!isOpen_) return;

    // Cover, then a spine down the left and a cream page. Hit targets stay where they were:
    // Prev x < 55, Next 220..275, close >= 280, page tabs along y >= 214.
    C2D_DrawRectSolid(0, 0, 0.30f, 320, 240, kCoverBottom);
    C2D_DrawRectSolid(0, 0, 0.31f, 14, 240, kCoverHeader);
    C2D_DrawRectSolid(11, 0, 0.32f, 2, 240, C2D_Color32(0xC4, 0x8A, 0x3A, 0xFF));
    theme::roundRect(16, 20, 298, 194, 5, kPaperBorder, 0.33f);
    theme::roundRect(18, 22, 294, 190, 4, kPaperBg, 0.34f);

    theme::roundRect(3, 3, 50, 16, 4, kTabInactive, 0.41f);
    theme::roundRect(222, 3, 52, 16, 4, kTabInactive, 0.41f);
    theme::roundRect(284, 2, 32, 18, 4, theme::kRedDark, 0.41f);
    theme::roundRect(285, 3, 30, 16, 4, theme::kRed, 0.42f);
    theme::textCentered(prevText_, 28, 4, 0.40f, kGoldText, 46, 0.5f);
    theme::textCentered(nextText_, 248, 4, 0.40f, kGoldText, 48, 0.5f);
    theme::textCentered(closeText_, 300, 3, 0.48f, kWhite, 26, 0.5f);

    if (dynamicBuf_) {
        C2D_TextBufClear(dynamicBuf_);
        char titleBuf[64];
        snprintf(titleBuf, sizeof(titleBuf), "%s  %d/%d", pages_[curPage_]->title(), curPage_ + 1, kPageCount);
        C2D_Text titleText;
        tt::render::textParse(&titleText, dynamicBuf_, titleBuf);
        theme::textCentered(titleText, 148, 3, 0.44f, kGoldText, 150, 0.5f);
    }

    pages_[curPage_]->draw(2, 24, 316, 188, bdc);

    // Page tabs hanging off the bottom of the sheet. The open tab is the same cream as the
    // page and meets it; the others stay on the cover, shorter, so this is a book and not a
    // row of buttons. Touch columns are unchanged (width 320/9, y >= 214).
    const float tabW = 320.0f / kPageCount;
    for (int i = 0; i < kPageCount; i++) {
        const float tx = i * tabW;
        const bool active = (i == curPage_);
        const float ty = active ? 208.0f : 218.0f;
        const float th = 240.0f - ty + 8.0f;
        theme::roundRect(tx + 2, ty, tabW - 4, th, 5, active ? kPaperBorder : kCoverHeader, 0.43f);
        theme::roundRect(tx + 3, ty + (active ? 0.0f : 1.0f), tabW - 6, th - 2, 4,
                         active ? kPaperBg : kTabInactive, 0.44f);
        theme::textCentered(tabTexts_[i], tx + tabW * 0.5f, active ? 214.0f : 221.0f, active ? 0.38f : 0.34f,
                            active ? kInk : kGoldText, tabW - 8, 0.5f);
    }
}

} // namespace tt::ui
