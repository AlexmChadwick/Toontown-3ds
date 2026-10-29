#include "ui/gag_table.h"

#include "ui/gag_data.h"
#include "ui/theme.h"
#include "world/battle_fx.h"

namespace tt::ui {

void drawGagTable(PageText& text, const GagTableData& d, float x, float y, float w, float rowH) {
    // The track name only needs a short pill. The experience column is wide enough for
    // "10000/10000" and "500 to Go!" once the scale shrinks to it, and the gag slots take
    // the rest. The icon fills the slot instead of stopping at 18 px.
    constexpr float kPillW = 50.0f, kExpW = 68.0f, kGap = 3.0f;
    const float cellsX = x + kPillW + kGap;
    const float cellW = (w - kPillW - kExpW - 2 * kGap) / gags::kLevels;
    const float cellH = rowH - 2.0f;
    float icon = cellH - 2.0f;
    if (icon > cellW - 2.0f) icon = cellW - 2.0f;
    for (int t = 0; t < gags::kTracks; t++) {
        const float ry = y + t * rowH;
        const bool have = d.access && d.access[t];
        const u32 col = theme::kTrackColors[t];
        // The track label: InventoryNew's coloured track button.
        theme::roundRect(x, ry, kPillW, cellH, 5, have ? theme::shade(col, 0.72f) : theme::kGreyDark);
        theme::roundRect(x + 1, ry + 1, kPillW - 2, cellH - 3, 4, have ? col : theme::kGrey);
        text.fit(x + 6, ry + (cellH - 15.0f) * 0.5f, theme::kTextSmall, have ? theme::kInk : theme::kInkSoft,
                 kPillW - 10, "%s", theme::kTrackTitles[t]);
        const uint16_t exp = d.exp ? d.exp[t] : 0;
        if (!have) {
            const char* note = t == d.trainingTrack ? "Training" : "Not trained yet";
            if (t == d.trainingTrack) {
                // TrackPage's filmstrip: 15 frames to earn (getTrackProgressAsArray) and the
                // last one, which the final ToonTask fills.
                const float fw = (cellW * gags::kLevels - 52.0f) / 16.0f;
                for (int f = 0; f < 16; f++) {
                    const bool got = f < 15 && (d.trainingBits >> f & 1u);
                    theme::roundRect(cellsX + 52 + f * fw, ry + 3, fw - 1.5f, cellH - 6, 2,
                                     got ? col : theme::kPaperShade);
                }
            }
            text.fit(cellsX + 2, ry + (cellH - 14.0f) * 0.5f, theme::kTextTiny,
                     t == d.trainingTrack ? theme::shade(col, 0.55f) : theme::kInkSoft, t == d.trainingTrack ? 48 : 0,
                     "%s", note);
            if (t == d.trainingTrack)
                text.right(x + w, ry + (cellH - 14.0f) * 0.5f, theme::kTextTiny, theme::kInkSoft, kExpW, "%d/15",
                           __builtin_popcount(d.trainingBits & 0x7FFFu));
            continue;
        }
        const int levels = gags::levelsUnlocked(t, exp);
        for (int l = 0; l < gags::kLevels; l++) {
            const float cx = cellsX + l * cellW;
            if (l >= levels) { // a gag still to earn: an empty slot
                theme::roundRect(cx + 1, ry + 1, cellW - 2, cellH - 2, 4, theme::kPaperShade);
                continue;
            }
            const unsigned n = d.count ? d.count[t][l] : 0;
            theme::roundRect(cx, ry, cellW - 1, cellH, 4, theme::shade(col, 0.7f));
            theme::roundRect(cx + 1, ry + 1, cellW - 3, cellH - 2, 3,
                             n ? theme::mix(col, theme::kWhite, 0.55f) : theme::kPaperShade);
            world::GagAtlas::instance().drawIcon(t, l, cx + 1, ry + 1, icon, 0.55f, n ? 1.0f : 0.35f);
            // The count on a paper badge in the cell's lower right, over the icon.
            const float bw = n >= 10 ? 13.0f : 8.0f;
            theme::roundRect(cx + cellW - bw - 2, ry + cellH - 11, bw, 10, 3, theme::kPaper, 0.58f);
            text.right(cx + cellW - 2.5f, ry + cellH - 13.5f, theme::kTextTiny, n ? theme::kInk : theme::kInkSoft, 0,
                       "%u", n);
        }
        // Experience sits above its bar, shrunk to the column, so "10000/10000" is not cut
        // by the slot edge or covered by the bar.
        const float ex = x + w - kExpW;
        const uint16_t next = levels < gags::kLevels ? gags::kLevelExp[t][levels] : 0;
        float frac = 1.0f;
        const float labelY = ry + 1.0f;
        if (exp >= gags::kMaxSkill) {
            text.centered(ex + kExpW * 0.5f, labelY, theme::kTextSmall, theme::kGreenDark, kExpW - 4, "Maxed");
        } else if (!next) {
            text.centered(ex + kExpW * 0.5f, labelY, theme::kTextSmall, theme::kInk, kExpW - 4, "%d to Go!",
                          gags::kMaxSkill - exp);
            frac = float(exp - gags::kLevelExp[t][gags::kLevels - 1]) /
                   float(gags::kMaxSkill - gags::kLevelExp[t][gags::kLevels - 1]);
        } else {
            text.centered(ex + kExpW * 0.5f, labelY, theme::kTextSmall, theme::kInk, kExpW - 4, "%u/%u",
                          unsigned(exp), unsigned(next));
            frac = float(exp) / float(next);
        }
        const float by = ry + cellH - 5.0f;
        theme::roundRect(ex + 2, by, kExpW - 4, 4, 2, theme::kGreyDark);
        if (frac > 0.0f) theme::roundRect(ex + 2, by, (kExpW - 4) * (frac > 1 ? 1 : frac), 4, 2, col);
    }
}

} // namespace tt::ui
