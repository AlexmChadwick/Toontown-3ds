#pragma once

#include "ui/book_pages/book_page.h"
#include "ui/focus.h"

namespace tt::ui {

class PageMap : public BookPage {
public:
    const char* title() const override { return "Toontown Map"; }
    const char* tabName() const override { return "Map"; }

    void init() override;
    void shutdown() override;
    u32 handleInput(u32 keysDown, const touchPosition& touch, BookDrawContext& bdc) override;
    void draw(float x, float y, float w, float h, const BookDrawContext& bdc) override;

private:
    C2D_TextBuf textBuf_ = nullptr;
    FocusGroup focus_{"book_map"};
};

} // namespace tt::ui
