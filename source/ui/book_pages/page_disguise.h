#pragma once

#include "ui/book_pages/book_page.h"
#include "ui/focus.h"

namespace tt::ui {

class PageDisguise : public BookPage {
public:
    const char* title() const override { return "Cog Suits"; }
    const char* tabName() const override { return "Suits"; }

    void init() override;
    void shutdown() override;
    u32 handleInput(u32 keysDown, const touchPosition& touch, BookDrawContext& bdc) override;
    void draw(float x, float y, float w, float h, const BookDrawContext& bdc) override;

    int activeDept() const { return activeDept_; }
    void setActiveDept(int d) { if (d >= 0 && d < 4) activeDept_ = d; }

private:
    int activeDept_ = 0; // 0 Bossbot, 1 Lawbot, 2 Cashbot, 3 Sellbot
    C2D_TextBuf textBuf_ = nullptr;
    FocusGroup focus_{"book_disguise"};
};

} // namespace tt::ui
