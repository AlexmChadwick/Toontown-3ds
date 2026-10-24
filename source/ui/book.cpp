#include "ui/book.h"
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

    C2D_TextParse(&prevText_, staticBuf_, "< Prev");
    C2D_TextParse(&nextText_, staticBuf_, "Next >");
    C2D_TextParse(&closeText_, staticBuf_, "[X]");

    for (int i = 0; i < kPageCount; i++) {
        C2D_TextParse(&tabTexts_[i], staticBuf_, pages_[i]->tabName());
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

    if (keysDown & KEY_START) {
        close();
        return KEY_START;
    }

    if (keysDown & (KEY_L | KEY_DLEFT)) {
        prevPage();
        return keysDown & (KEY_L | KEY_DLEFT);
    }

    if (keysDown & (KEY_R | KEY_DRIGHT)) {
        nextPage();
        return keysDown & (KEY_R | KEY_DRIGHT);
    }

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
        // Page content interaction
        else {
            u32 consumed = pages_[curPage_]->handleInput(keysDown, touch, bdc);
            return consumed ? consumed : static_cast<u32>(KEY_TOUCH);
        }
    }

    return 0;
}

void Book::draw(const BookDrawContext& bdc) {
    if (!isOpen_) return;

    // Binder cover header
    C2D_DrawRectSolid(0, 0, 0.40f, 320, 24, kCoverHeader);

    // Prev / Next / Close
    C2D_DrawText(&prevText_, C2D_WithColor, 8, 5, 0.5f, 0.42f, 0.42f, kGoldText);
    C2D_DrawText(&nextText_, C2D_WithColor, 226, 5, 0.5f, 0.42f, 0.42f, kGoldText);
    C2D_DrawText(&closeText_, C2D_WithColor, 290, 5, 0.5f, 0.42f, 0.42f, kCloseText);

    // Title & page number in center
    if (dynamicBuf_) {
        C2D_TextBufClear(dynamicBuf_);
        char titleBuf[64];
        snprintf(titleBuf, sizeof(titleBuf), "%s (%d/%d)", pages_[curPage_]->title(), curPage_ + 1, kPageCount);
        C2D_Text titleText;
        C2D_TextParse(&titleText, dynamicBuf_, titleBuf);
        C2D_DrawText(&titleText, C2D_WithColor, 70, 5, 0.5f, 0.42f, 0.42f, kWhite);
    }

    // Main page parchment paper
    C2D_DrawRectSolid(2, 24, 0.35f, 316, 188, kPaperBg);
    C2D_DrawRectSolid(2, 24, 0.36f, 316, 1, kPaperBorder);
    C2D_DrawRectSolid(2, 211, 0.36f, 316, 1, kPaperBorder);

    // Draw active page
    pages_[curPage_]->draw(2, 24, 316, 188, bdc);

    // Bottom tab bar
    C2D_DrawRectSolid(0, 212, 0.40f, 320, 28, kCoverBottom);
    const float tabW = 320.0f / kPageCount;
    for (int i = 0; i < kPageCount; i++) {
        const float tx = i * tabW;
        const bool active = (i == curPage_);
        C2D_DrawRectSolid(tx + 1, active ? 212 : 215, 0.42f, tabW - 2, active ? 28 : 25, active ? kTabActive : kTabInactive);
        C2D_DrawText(&tabTexts_[i], C2D_WithColor, tx + 2, 218, 0.5f, 0.36f, 0.36f, active ? kInk : kWhite);
    }
}

} // namespace tt::ui
