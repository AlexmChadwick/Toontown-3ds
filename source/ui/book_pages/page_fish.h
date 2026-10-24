#pragma once

#include "ui/book_pages/book_page.h"

namespace tt::ui {

class PageFish : public BookPage {
public:
    const char* title() const override { return "Fish Album"; }
    const char* tabName() const override { return "Fish"; }

    void init() override;
    void shutdown() override;
    u32 handleInput(u32 keysDown, const touchPosition& touch, BookDrawContext& bdc) override;
    void draw(float x, float y, float w, float h, const BookDrawContext& bdc) override;

private:
    int activeGenusIdx_ = 0; // index into distinct genera (0..13)
    C2D_TextBuf textBuf_ = nullptr;
};

} // namespace tt::ui
