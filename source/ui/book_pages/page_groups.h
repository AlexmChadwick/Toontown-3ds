// Groups page of the Shticker Book (docs/design/groups.md). Built on first use.
#pragma once

#include "net/groups_logic.h"
#include "ui/book_pages/book_page.h"
#include "ui/focus.h"
#include "ui/page_text.h"

namespace tt::ui {

class PageGroups : public BookPage {
public:
    const char* title() const override { return "Groups"; }
    const char* tabName() const override { return "Groups"; }

    void init() override {}
    void shutdown() override;
    void enter() override;
    void exit() override;
    bool hideTabs() const override { return picker_; }
    u32 handleInput(u32 keysDown, const touchPosition& touch, BookDrawContext& bdc) override;
    void draw(float x, float y, float w, float h, const BookDrawContext& bdc) override;

private:
    void ensure();
    void rebuild(const groups::GroupsState* g);
    void activate(int id, BookDrawContext& bdc);
    const groups::GroupView* visible(const groups::GroupsState& g) const;

    PageText text_;
    FocusGroup focus_{"groups"};
    bool built_ = false;
    bool picker_ = false;
    int pickKind_ = 0;
    int row_ = 0;
};

} // namespace tt::ui
