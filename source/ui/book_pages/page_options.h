#pragma once

#include "ui/book_pages/book_page.h"
#include "ui/focus.h"
#include "ui/page_text.h"

namespace tt::ui {

class PageOptions : public BookPage {
public:
    const char* title() const override { return "Options & Districts"; }
    const char* tabName() const override { return "Option"; }

    void init() override;
    void shutdown() override;
    u32 handleInput(u32 keysDown, const touchPosition& touch, BookDrawContext& bdc) override;
    void draw(float x, float y, float w, float h, const BookDrawContext& bdc) override;

private:
    void applyOption(int id, State& s); // padnav: the action behind each pad::book option id

    PageText text_;
    FocusGroup focus_{"book_options"}; // padnav
};

} // namespace tt::ui
