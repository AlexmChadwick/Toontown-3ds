#pragma once

#include "ui/book_pages/book_page.h"

namespace tt::ui {

class PageTrophies : public BookPage {
public:
    const char* title() const override { return "Trophies & Records"; }
    const char* tabName() const override { return "Trophies"; }

    void init() override;
    void shutdown() override;
    void draw(float x, float y, float w, float h, const BookDrawContext& bdc) override;

private:
    C2D_TextBuf textBuf_ = nullptr;
};

} // namespace tt::ui
