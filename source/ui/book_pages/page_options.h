#pragma once

#include "ui/book_pages/book_page.h"
#include "ui/focus.h"

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
    void rebuildText(const BookDrawContext& bdc);

    C2D_TextBuf textBuf_ = nullptr;
    FocusGroup focus_{"book_options"};
    int lastMusicVol_ = -1;
    int lastSfxVol_ = -1;
    int lastStress_ = -1;
    bool lastStereo_ = false;
    bool lastDebug_ = false;
    int lastLod_ = -1;
    int lastNumDistricts_ = -1;
};

} // namespace tt::ui
