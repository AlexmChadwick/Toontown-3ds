#pragma once

#include "ui/book_pages/book_page.h"
#include "ui/focus.h"

namespace tt::ui {

class PageSos : public BookPage {
public:
    const char* title() const override { return "SOS ToonFriends"; }
    const char* tabName() const override { return "SOS"; }

    void init() override;
    void shutdown() override;
    u32 handleInput(u32 keysDown, const touchPosition& touch, BookDrawContext& bdc) override;
    void draw(float x, float y, float w, float h, const BookDrawContext& bdc) override;

private:
    int viewPage_ = 0; // 0 for cards 0..7, 1 for cards 8..15
    C2D_TextBuf textBuf_ = nullptr;
    FocusGroup focus_{"book_sos"};
};

} // namespace tt::ui
