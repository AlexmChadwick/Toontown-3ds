#pragma once

#include "ui/book_pages/book_page.h"
#include "ui/page_text.h"

namespace tt::ui {

class PageInventory : public BookPage {
public:
    const char* title() const override { return "Gags & Training"; }
    const char* tabName() const override { return "Gags"; }

    void init() override;
    void shutdown() override;
    void draw(float x, float y, float w, float h, const BookDrawContext& bdc) override;

private:
    PageText text_;
};

} // namespace tt::ui
