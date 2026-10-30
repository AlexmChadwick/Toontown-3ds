#include "ui/gag_table.h"

#include "ui/gag_data.h"
#include "ui/inventory_art.h"
#include "ui/theme.h"

namespace tt::ui {

void drawGagTable(PageText& text, const GagTableData& d, float x, float y, float w, float rowH) {
    // InventoryPage: InventoryNew in book mode. Each track is an InventoryRow in its colour with
    // the name in the row's label; every gag level reached is a button in BookUnpressableImage0Color
    // (0, 0.6, 1) whatever its count (makeBookUnpressable). The experience sits right of the row
    // (upstream's trackBars are too small to read here).
    constexpr float kExpW = 56.0f, kGap = 3.0f;
    const float rowW = w - kExpW - kGap, rh = rowH - 2.0f;
    const float labelW = rowW * inventory::kSlot0 - 12.0f;
    constexpr u32 kTrackInk = theme::rgb(0x33, 0x33, 0x33); // trackNameLabels text_fg (0.2, 0.2, 0.2)
    for (int t = 0; t < gags::kTracks; t++) {
        const float ry = y + t * rowH;
        const bool have = d.access && d.access[t];
        const u32 col = theme::kTrackColors[t];
        inventory::drawRow(x, ry, rowW, rh, col, 0.52f);
        text.fit(x + 9, ry + (rh - 14.0f) * 0.5f, theme::kTextTiny, kTrackInk, labelW, "%s", theme::kTrackCaps[t]);
        const uint16_t exp = d.exp ? d.exp[t] : 0;
        if (!have) {
            // hideTrack leaves the empty slots; the track being trained shows TrackPage's filmstrip
            // over them (15 frames to earn, getTrackProgressAsArray, and the last one the final
            // ToonTask fills).
            if (t == d.trainingTrack) {
                const float sx = inventory::slotX(x, rowW, 0) + 1, sw = inventory::slotX(x, rowW, 7) - sx - 4;
                theme::roundRect(sx, ry + 2, sw, rh - 4, 3, theme::kPaper, 0.54f);
                const float fw = (sw - 50.0f) / 16.0f;
                for (int f = 0; f < 16; f++) {
                    const bool got = f < 15 && (d.trainingBits >> f & 1u);
                    theme::roundRect(sx + 48 + f * fw, ry + 4, fw - 1.5f, rh - 8, 2, got ? col : theme::kPaperShade,
                                     0.55f);
                }
                text.fit(sx + 3, ry + (rh - 14.0f) * 0.5f, theme::kTextTiny, theme::shade(col, 0.55f), 44, "Training");
                text.right(x + w, ry + (rh - 14.0f) * 0.5f, theme::kTextTiny, theme::kInkSoft, kExpW, "%d/15",
                           __builtin_popcount(d.trainingBits & 0x7FFFu));
            }
            continue;
        }
        const int levels = gags::levelsUnlocked(t, exp);
        for (int l = 0; l < levels; l++) {
            const unsigned n = d.count ? d.count[t][l] : 0;
            float r[4];
            inventory::drawGagButton(InventoryArt::Flat, inventory::kBookFace, t, l, x, ry, rowW, rh, 1.0f, r, 0.55f);
            // The count: text white, right-aligned low on the button (text_pos 0.07, -0.04).
            text.right(r[0] + r[2] - 4.5f, r[1] + r[3] - 13.0f, theme::kTextTiny, theme::kWhite, 0, "%u", n);
        }
        // Experience: InventoryNew's "exp / next" (or "N to Go!" toward the uber gag), with a bar.
        const float ex = x + w - kExpW;
        const uint16_t next = levels < gags::kLevels ? gags::kLevelExp[t][levels] : 0;
        float frac = 1.0f;
        if (exp >= gags::kMaxSkill) {
            text.centered(ex + kExpW * 0.5f, ry, theme::kTextTiny, theme::kGreenDark, kExpW, "Maxed");
        } else if (!next) {
            text.centered(ex + kExpW * 0.5f, ry, theme::kTextTiny, theme::kInk, kExpW, "%d to Go!",
                          gags::kMaxSkill - exp);
            frac = float(exp - gags::kLevelExp[t][gags::kLevels - 1]) /
                   float(gags::kMaxSkill - gags::kLevelExp[t][gags::kLevels - 1]);
        } else {
            text.centered(ex + kExpW * 0.5f, ry, theme::kTextTiny, theme::kInk, kExpW, "%u/%u", unsigned(exp),
                          unsigned(next));
            frac = float(exp) / float(next);
        }
        const float by = ry + rh - 6.0f;
        theme::roundRect(ex + 2, by, kExpW - 4, 5, 2, theme::kGreyDark);
        if (frac > 0.0f) theme::roundRect(ex + 2, by, (kExpW - 4) * (frac > 1 ? 1 : frac), 5, 2, col);
    }
}

} // namespace tt::ui
