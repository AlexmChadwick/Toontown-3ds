#include "render/ui_font.h"
#include "ui/book.h"
#include "ui/book_tab_icons.h"
#include "core/log.h"
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
    if (!isOpen_) TT_LOG("book: open on page %d", page >= 0 ? page : curPage_);
    // The tap (or button) that opened the book must not also act inside it.
    if (!isOpen_) touchBlocked_ = true;
    isOpen_ = true;
    if (page >= 0 && page < kPageCount) {
        setPage(page);
    } else {
        pages_[curPage_]->enter();
    }
}

void Book::close(const char* why) {
    if (isOpen_) {
        TT_LOG("book: close (%s) on page %d", why, curPage_);
        pages_[curPage_]->exit();
        isOpen_ = false;
    }
}

void Book::toggle() {
    if (isOpen_) close("toggle");
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

u32 Book::handleInput(u32 keysDown, u32 held, const touchPosition& touch, BookDrawContext& bdc) {
    if (!isOpen_) return 0;
    // The whole bottom screen is the book: no touch reaches the tab bar or the world under it.
    const u32 touchKeys = keysDown & KEY_TOUCH;
    if (!(held & KEY_TOUCH) && !touchKeys) touchBlocked_ = false;
    if (touchKeys) {
        // Only a fresh touch-down with a real point acts: the opening stylus (still down), and
        // the (0,0) / off-screen point some hardware reports on a touch's first or last frame,
        // are dropped.
        const bool valid = (touch.px != 0 || touch.py != 0) && touch.px < 320 && touch.py < 240;
        if (touchBlocked_ || !valid) {
            TT_LOG("book: ignored touch at %u,%u (%s)", unsigned(touch.px), unsigned(touch.py),
                   touchBlocked_ ? "stylus down since open" : "no point");
            keysDown &= ~KEY_TOUCH;
        }
        touchBlocked_ = false;
        if (keysDown & KEY_TOUCH)
            TT_LOG("book: touch %u,%u on page %d", unsigned(touch.px), unsigned(touch.py), curPage_);
    }

    // ---- padnav: B closes the book like START (upstream's book has a close button only) ----
    if (keysDown & (KEY_START | KEY_B)) {
        close((keysDown & KEY_B) ? "B" : "START");
        return (keysDown & (KEY_START | KEY_B)) | touchKeys;
    }

    if (keysDown & KEY_L) {
        prevPage();
        return KEY_L | touchKeys;
    }

    if (keysDown & KEY_R) {
        nextPage();
        return KEY_R | touchKeys;
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
                close("close button");
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
    return consumed | touchKeys;
    // ---- end padnav ----
}

void Book::draw(const BookDrawContext& bdc) {
    if (!isOpen_) return;

    // Binder cover header: Prev / Next as paper tabs, a red close button (touch areas in
    // handleInput: x < 55, 220..275, >= 280).
    C2D_DrawRectSolid(0, 0, 0.40f, 320, 24, kCoverHeader);
    theme::roundRect(3, 3, 50, 18, 5, kTabInactive, 0.41f);
    theme::roundRect(222, 3, 52, 18, 5, kTabInactive, 0.41f);
    theme::roundRect(284, 2, 32, 20, 6, theme::kRedDark, 0.41f);
    theme::roundRect(285, 2, 30, 18, 5, theme::kRed, 0.41f);
    theme::textCentered(prevText_, 28, 4, 0.42f, kGoldText, 46, 0.5f);
    theme::textCentered(nextText_, 248, 4, 0.42f, kGoldText, 48, 0.5f);
    theme::textCentered(closeText_, 300, 3, 0.5f, kWhite, 26, 0.5f);

    // Title & page number in center
    if (dynamicBuf_) {
        C2D_TextBufClear(dynamicBuf_);
        char titleBuf[64];
        snprintf(titleBuf, sizeof(titleBuf), "%s (%d/%d)", pages_[curPage_]->title(), curPage_ + 1, kPageCount);
        C2D_Text titleText;
        tt::render::textParse(&titleText, dynamicBuf_, titleBuf);
        theme::textCentered(titleText, 137, 4, 0.46f, kWhite, 160, 0.5f);
    }

    // Main page parchment paper
    C2D_DrawRectSolid(2, 24, 0.35f, 316, 188, kPaperBg);
    C2D_DrawRectSolid(2, 24, 0.36f, 316, 1, kPaperBorder);
    C2D_DrawRectSolid(2, 211, 0.36f, 316, 1, kPaperBorder);

    // Draw active page
    pages_[curPage_]->draw(2, 24, 316, 188, bdc);

    // Bottom tab bar: rounded page tabs hanging off the paper, the open one in gold, each with its
    // page's icon as upstream's page tabs (the page's name is in the header). Text if the icon
    // atlas is missing.
    C2D_DrawRectSolid(0, 212, 0.40f, 320, 28, kCoverBottom);
    const float tabW = 320.0f / kPageCount;
    for (int i = 0; i < kPageCount; i++) {
        const float tx = i * tabW;
        const bool active = (i == curPage_);
        const float ty = active ? 211.0f : 214.0f;
        theme::roundRect(tx + 1, ty, tabW - 2, 240 - ty + 6, 5, active ? kTabActive : kTabInactive, 0.42f);
        if (!drawBookTabIcon(i, tx + tabW * 0.5f, active ? 225.0f : 227.0f, active ? 24.0f : 21.0f, 0.5f)) {
            theme::textCentered(tabTexts_[i], tx + tabW * 0.5f, active ? 218.0f : 220.0f, 0.4f, active ? kInk : kWhite,
                                tabW - 4, 0.5f);
        }
    }
}

} // namespace tt::ui
