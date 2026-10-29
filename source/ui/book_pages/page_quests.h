#pragma once

#include "ui/book_pages/book_page.h"
#include "ui/page_text.h"

namespace tt::ui {

// QuestPoster cards for up to maxShown carried ToonTasks, stacked in (x, y, w, h) (each at most
// 82 px tall). Returns how many were drawn. Shared by the book's ToonTasks page and the Toon tab.
int drawQuestPosters(PageText& text, const net::OwnQuests& q, float x, float y, float w, float h, int maxShown);

class PageQuests : public BookPage {
public:
    const char* title() const override { return "ToonTasks"; }
    const char* tabName() const override { return "Tasks"; }

    void init() override;
    void shutdown() override;
    void draw(float x, float y, float w, float h, const BookDrawContext& bdc) override;

private:
    PageText text_;
};

} // namespace tt::ui
