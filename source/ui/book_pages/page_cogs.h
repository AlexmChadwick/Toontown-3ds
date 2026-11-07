#pragma once

#include "ui/book_pages/book_page.h"
#include "ui/focus.h"
#include "ui/page_text.h"

namespace tt::ui {

class PageCogs : public BookPage {
public:
    const char* title() const override { return "Cog Gallery"; }
    const char* tabName() const override { return "Cogs"; }

    void init() override;
    void shutdown() override;
    u32 handleInput(u32 keysDown, const touchPosition& touch, BookDrawContext& bdc) override;
    void draw(float x, float y, float w, float h, const BookDrawContext& bdc) override;

    int activeDept() const { return activeDept_; }
    void setActiveDept(int d) { if (d >= 0 && d < 4) activeDept_ = d; }

private:
    int activeDept_ = 0; // 0 Bossbot, 1 Lawbot, 2 Cashbot, 3 Sellbot
    PageText text_;
    FocusGroup focus_{"book_cogs"}; // padnav
};

} // namespace tt::ui
