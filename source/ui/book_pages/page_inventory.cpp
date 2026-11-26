#include "ui/book_pages/page_inventory.h"

#include "net/client.h"
#include "ui/gag_data.h"
#include "ui/gag_table.h"
#include "ui/purchase_art.h"
#include "ui/theme.h"

namespace tt::ui {

void PageInventory::init() { text_.init(1024); }

void PageInventory::shutdown() { text_.shutdown(); }

// InventoryPage: the gag table with the jellybean count, and TrackPage's training film for the
// track being trained (shown in that track's row).
void PageInventory::draw(float x, float y, float w, float h, const BookDrawContext& bdc) {
    (void)h;
    const net::OwnStats* st = bdc.stats;
    int total = 0;
    if (st) {
        for (int t = 0; t < gags::kTracks; t++)
            for (int l = 0; l < gags::kLevels; l++) total += st->inventory[t][l];
    }
    text_.draw(x + 8, y + 3, theme::kTextBody, theme::kInk, "Gags  %d / %d", total, st ? st->maxCarry : 20);
    if (st) {
        // The jellybean jar, then the count (InventoryPage.moneyDisplay).
        const float bw = text_.right(x + w - 8, y + 3, theme::kTextBody, theme::rgb(0x80, 0x50, 0x00), 0,
                                     "Jellybeans  %d / %d", st->money, st->maxMoney);
        drawJarIcon(x + w - 30 - bw, y, 19);
    }
    GagTableData d;
    if (st) {
        d.count = st->inventory;
        d.exp = st->experience;
        d.access = st->trackAccess;
    }
    if (bdc.book) {
        d.trainingTrack = bdc.book->trackProgressId;
        d.trainingBits = bdc.book->trackProgress;
    }
    drawGagTable(text_, d, x + 5, y + 24, w - 10, 22.0f);
    if (!st) {
        text_.centered(x + w * 0.5f, y + 24 + 7 * 22 + 4, theme::kTextSmall, theme::kInkSoft, w - 16,
                       "Your gags appear here once you are online.");
    }
}

} // namespace tt::ui
