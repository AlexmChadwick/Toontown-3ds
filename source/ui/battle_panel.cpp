#include "ui/battle_panel.h"

#include <cstdarg>
#include <cstdio>

#include "ui/gag_data.h"

namespace tt::ui {

namespace {

constexpr float kW = 320.0f;
constexpr float kCogY = 18, kCogH = 44, kCogW = 78;
constexpr float kGridY = 66, kRowH = 20, kGridX = 60, kCellW = 35;
constexpr float kBtnY = 210, kBtnH = 28;

const u32 kBg = C2D_Color32(0x1E, 0x2A, 0x44, 0xFF);
const u32 kPanel = C2D_Color32(0x2C, 0x3C, 0x60, 0xFF);
const u32 kAccent = C2D_Color32(0xF5, 0xB8, 0x1A, 0xFF);
const u32 kWhite = C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF);
const u32 kDim = C2D_Color32(0xA8, 0xB4, 0xCC, 0xFF);
const u32 kHp = C2D_Color32(0x5C, 0xD0, 0x5C, 0xFF);
const u32 kHpLow = C2D_Color32(0xE0, 0x50, 0x40, 0xFF);
// Track colours, roughly the Shticker Book's gag pages.
const u32 kTrackColor[gags::kTracks] = {
    C2D_Color32(0xD8, 0x6A, 0xC8, 0xFF), C2D_Color32(0xD8, 0xC8, 0x40, 0xFF), C2D_Color32(0x40, 0xB0, 0x40, 0xFF),
    C2D_Color32(0x40, 0x70, 0xD8, 0xFF), C2D_Color32(0xE0, 0x80, 0x30, 0xFF), C2D_Color32(0xE0, 0x50, 0xA0, 0xFF),
    C2D_Color32(0x40, 0xB8, 0xD8, 0xFF),
};

bool inRect(const touchPosition& t, float x, float y, float w, float h) {
    return t.px >= x && t.px < x + w && t.py >= y && t.py < y + h;
}

} // namespace

bool BattlePanel::init() {
    buf_ = C2D_TextBufNew(1024);
    return buf_ != nullptr;
}

void BattlePanel::shutdown() {
    if (buf_) C2D_TextBufDelete(buf_);
    buf_ = nullptr;
}

void BattlePanel::text(float x, float y, float scale, u32 color, const char* fmt, ...) {
    char line[64];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    C2D_Text t;
    C2D_TextParse(&t, buf_, line);
    C2D_DrawText(&t, C2D_WithColor, x, y, 0.6f, scale, scale, color);
}

// A toon-up on one toon needs another toon to heal (toons never heal themselves in battle).
uint32_t BattlePanel::targetFor(const world::BattleView& v, int track) const {
    if (track == world::kTrackHeal) {
        for (int i = 0; i < v.numToons; i++)
            if (v.toons[i].active && !v.toons[i].local) return v.toons[i].doId;
        return 0;
    }
    if (target_ < v.numCogs && v.cogs[target_].active) return v.cogs[target_].doId;
    for (int i = 0; i < v.numCogs; i++)
        if (v.cogs[i].active) return v.cogs[i].doId;
    return 0;
}

bool BattlePanel::usable(const world::BattleView& v, const BattleGags& g, int track, int level) const {
    if (!g.count || !g.access[track] || g.count[track][level] == 0) return false;
    if (level >= gags::levelsUnlocked(track, g.exp[track])) return false;
    return world::attackAffectsGroup(track, level) || targetFor(v, track) != 0;
}

u32 BattlePanel::handleInput(u32 keysDown, const touchPosition& touch, const world::BattleView& v,
                             const BattleGags& g) {
    if (!(keysDown & KEY_TOUCH)) return keysDown;
    if (v.phase == world::BattlePhase::Reward) {
        if (inRect(touch, 218, kBtnY, 100, kBtnH)) pending_.kind = world::BattleAction::Continue;
        return keysDown;
    }
    if (v.phase != world::BattlePhase::Input) return keysDown;
    for (int i = 0; i < v.numCogs; i++) {
        if (inRect(touch, 2 + i * (kCogW + 1), kCogY, kCogW, kCogH) && v.cogs[i].active) target_ = i;
    }
    for (int t = 0; t < gags::kTracks; t++) {
        for (int l = 0; l < gags::kLevels; l++) {
            if (!inRect(touch, kGridX + l * (kCellW + 2), kGridY + t * kRowH, kCellW, kRowH - 2)) continue;
            if (!usable(v, g, t, l)) continue;
            pending_.kind = world::BattleAction::Attack;
            pending_.track = t;
            pending_.level = l;
            pending_.target = world::attackAffectsGroup(t, l) ? 0 : targetFor(v, t);
        }
    }
    if (inRect(touch, 2, kBtnY, 100, kBtnH)) pending_.kind = world::BattleAction::Pass;
    if (v.canRun && inRect(touch, 110, kBtnY, 100, kBtnH)) pending_.kind = world::BattleAction::Run;
    return keysDown;
}

void BattlePanel::draw(const world::BattleView& v, const BattleGags& g) {
    C2D_TextBufClear(buf_);
    C2D_DrawRectSolid(0, 0, 0.5f, kW, 240, kBg);
    text(4, 2, 0.45f, kWhite, "%s", v.status ? v.status : "");
    if (v.secondsLeft >= 0) text(286, 2, 0.5f, v.secondsLeft <= 5 ? kHpLow : kAccent, "%d", v.secondsLeft);

    // Cogs: name, level, HP bar. The target has a gold frame.
    for (int i = 0; i < v.numCogs && i < 4; i++) {
        const world::BattleCogView& c = v.cogs[i];
        const float x = 2 + i * (kCogW + 1);
        if (i == target_ && v.phase == world::BattlePhase::Input) {
            C2D_DrawRectSolid(x - 1, kCogY - 1, 0.5f, kCogW + 2, kCogH + 2, kAccent);
        }
        C2D_DrawRectSolid(x, kCogY, 0.5f, kCogW, kCogH, c.active ? kPanel : kBg);
        text(x + 3, kCogY + 2, 0.38f, c.active ? kWhite : kDim, "%s", c.name);
        text(x + 3, kCogY + 15, 0.38f, kDim, "Level %d%s", c.level, c.lured ? " lured" : "");
        const float f = c.maxHp > 0 ? static_cast<float>(c.hp) / c.maxHp : 0.0f;
        C2D_DrawRectSolid(x + 3, kCogY + 32, 0.55f, kCogW - 6, 7, kBg);
        C2D_DrawRectSolid(x + 3, kCogY + 32, 0.56f, (kCogW - 6) * (f < 0 ? 0 : f), 7, f > 0.35f ? kHp : kHpLow);
    }

    if (v.phase == world::BattlePhase::Reward) {
        text(8, kGridY, 0.5f, kWhite, "Cogs defeated: %d", v.cogsDefeated);
        for (int i = 0; i < v.numRewardRows; i++) {
            const world::BattleRewardRow& r = v.reward[i];
            text(8, kGridY + 20 + i * 18, 0.45f, kWhite, "%s", gags::kTrackNames[r.track]);
            text(110, kGridY + 20 + i * 18, 0.45f, kAccent, "+%d", r.earned);
            text(170, kGridY + 20 + i * 18, 0.45f, kDim, "(%d)", r.orig + r.earned);
        }
        C2D_DrawRectSolid(218, kBtnY, 0.5f, 100, kBtnH, v.rewardSent ? kPanel : kAccent);
        text(236, kBtnY + 6, 0.5f, v.rewardSent ? kDim : kBg, "Continue");
        return;
    }

    // Gag grid: one row per track, a cell per level with our count.
    const bool choosing = v.phase == world::BattlePhase::Input;
    for (int t = 0; t < gags::kTracks; t++) {
        const float y = kGridY + t * kRowH;
        const bool have = g.access && g.access[t];
        text(4, y + 2, 0.38f, have ? kWhite : kDim, "%s", gags::kTrackNames[t]);
        if (!have || !g.count) continue;
        const int levels = gags::levelsUnlocked(t, g.exp[t]);
        for (int l = 0; l < levels; l++) {
            const float x = kGridX + l * (kCellW + 2);
            const bool ok = choosing && usable(v, g, t, l);
            C2D_DrawRectSolid(x, y, 0.5f, kCellW, kRowH - 2, ok ? kTrackColor[t] : kPanel);
            text(x + 12, y + 2, 0.4f, ok ? kBg : kDim, "%u", unsigned(g.count[t][l]));
        }
    }

    C2D_DrawRectSolid(2, kBtnY, 0.5f, 100, kBtnH, choosing ? kPanel : kBg);
    text(36, kBtnY + 6, 0.5f, choosing ? kWhite : kDim, "Pass");
    C2D_DrawRectSolid(110, kBtnY, 0.5f, 100, kBtnH, choosing && v.canRun ? kPanel : kBg);
    text(146, kBtnY + 6, 0.5f, choosing && v.canRun ? kWhite : kDim, "Run");
}

} // namespace tt::ui
