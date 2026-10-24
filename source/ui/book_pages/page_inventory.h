#pragma once

#include "ui/book_pages/book_page.h"

namespace tt::ui {

class PageInventory : public BookPage {
public:
    const char* title() const override { return "Gags & Training"; }
    const char* tabName() const override { return "Gags"; }

    void init() override;
    void shutdown() override;
    void draw(float x, float y, float w, float h, const BookDrawContext& bdc) override;

private:
    void rebuildText(const BookDrawContext& bdc);

    C2D_TextBuf textBuf_ = nullptr;
    uint32_t lastStatsVer_ = 0;
    uint32_t lastBookVer_ = 0;
};

} // namespace tt::ui
