// Shtiker Book manager (docs/design/shtiker_book.md).
#pragma once

#include <3ds.h>
#include <citro2d.h>

#include "ui/book_pages/book_page.h"
#include "ui/book_pages/page_inventory.h"
#include "ui/book_pages/page_quests.h"
#include "ui/book_pages/page_cogs.h"
#include "ui/book_pages/page_disguise.h"
#include "ui/book_pages/page_fish.h"
#include "ui/book_pages/page_trophies.h"
#include "ui/book_pages/page_sos.h"
#include "ui/book_pages/page_options.h"
#include "ui/book_pages/page_map.h"

namespace tt::ui {

class Book {
public:
    enum PageIndex : int {
        kPageGags = 0,
        kPageTasks,
        kPageCogs,
        kPageDisguise,
        kPageFish,
        kPageTrophies,
        kPageSos,
        kPageOptions,
        kPageMap,
        kPageCount
    };

    bool init();
    void shutdown();

    bool isOpen() const { return isOpen_; }
    void open(int page = -1);
    void close();
    void toggle();

    void setPage(int page);
    void nextPage();
    void prevPage();
    int currentPage() const { return curPage_; }

    u32 handleInput(u32 keysDown, const touchPosition& touch, BookDrawContext& bdc);
    void draw(const BookDrawContext& bdc);

private:
    void rebuildTabs();

    bool isOpen_ = false;
    int curPage_ = kPageGags;

    PageInventory pageGags_;
    PageQuests pageTasks_;
    PageCogs pageCogs_;
    PageDisguise pageDisguise_;
    PageFish pageFish_;
    PageTrophies pageTrophies_;
    PageSos pageSos_;
    PageOptions pageOptions_;
    PageMap pageMap_;

    BookPage* pages_[kPageCount] = {
        &pageGags_,
        &pageTasks_,
        &pageCogs_,
        &pageDisguise_,
        &pageFish_,
        &pageTrophies_,
        &pageSos_,
        &pageOptions_,
        &pageMap_
    };

    C2D_TextBuf staticBuf_ = nullptr;
    C2D_TextBuf dynamicBuf_ = nullptr;
    C2D_Text tabTexts_[kPageCount];
    C2D_Text prevText_;
    C2D_Text nextText_;
    C2D_Text closeText_;
};

} // namespace tt::ui
