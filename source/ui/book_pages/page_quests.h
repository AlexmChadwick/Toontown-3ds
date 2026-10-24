#pragma once

#include "ui/book_pages/book_page.h"

namespace tt::ui {

class PageQuests : public BookPage {
public:
    const char* title() const override { return "ToonTasks"; }
    const char* tabName() const override { return "Tasks"; }

    void init() override;
    void shutdown() override;
    void draw(float x, float y, float w, float h, const BookDrawContext& bdc) override;

private:
    C2D_TextBuf textBuf_ = nullptr;
    uint32_t lastQuestVer_ = 0xFFFFFFFFu;
};

} // namespace tt::ui
