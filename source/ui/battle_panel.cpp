#include "ui/panel_geometry.h"
#include "ui/battle_panel.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "ui/gag_data.h"
#include "ui/inventory_art.h"
#include "ui/pad_tables.h"
#include "ui/sos_tables.h"
#include "core/log.h"
#include "world/battle_fx.h"
#include "ui/suit_heads_atlas.h"
#include "ui/theme.h"
#include "ui/tutorial_art.h"

namespace tt::ui {

namespace {

constexpr float kW = 320.0f;
// The layout is shared with the focus table (ui/pad_tables.h) so touch, D-Pad and drawing agree.
constexpr float kCogY = pad::battle::kCogY, kCogH = pad::battle::kCogH, kCogW = pad::battle::kCogW;
constexpr float kGridY = pad::battle::kGridY, kGridH = pad::battle::kGridH;
constexpr float kGridX = pad::battle::kGridX, kCellW = pad::battle::kCellW, kCellGap = pad::battle::kCellGap;
constexpr float kBtnY = pad::battle::kBtnY, kBtnH = pad::battle::kBtnH;
static_assert(pad::battle::kTracks == gags::kTracks && pad::battle::kLevels == gags::kLevels,
              "the battle focus table assumes the 7 x 7 gag grid");

const u32 kInk = theme::kInk;
const u32 kInkSoft = theme::kInkSoft;
constexpr u32 kInvBlue = theme::rgb(0x00, 0x99, 0xFF); // InventoryNew battle buttons: image_color (0, 0.6, 1)
constexpr u32 kSlipPink = theme::rgb(0xFF, 0xC4, 0xD4);
constexpr u32 kStar = theme::rgb(0xF6, 0xC0, 0x10), kStarDark = theme::rgb(0xA8, 0x70, 0x00);

// A five-pointed star of outer radius r (the SOS cards' rating, as the book's SOS page).
void star(float cx, float cy, float r, u32 c) {
    float px[10], py[10];
    for (int i = 0; i < 10; i++) {
        const float a = -1.5707963f + i * 0.6283185f, rr = (i & 1) ? r * 0.45f : r;
        px[i] = cx + cosf(a) * rr;
        py[i] = cy + sinf(a) * rr;
    }
    for (int i = 0; i < 10; i++) {
        const int j = (i + 1) % 10;
        C2D_DrawTriangle(cx, cy, c, px[i], py[i], c, px[j], py[j], c, 0.6f);
    }
}
// Suit.updateHealthBar's condition: 0 green (> 95%), 1 yellow (> 70%), 2 orange (> 30%),
// 3 red (> 5%), 4 blinking red (> 0), 5 defeated (fast blink).
// InventoryNew.showDetail's data for the focused gag, on one line: AvPropStrings, the
// InventoryDetailAmount count, AvTrackAccStrings and getAvPropDamage at our experience, then
// getSingleGroupStr. Organic / prop bonuses are not known to the client view, so none is added.
void gagDetail(char* out, size_t n, int track, int level, uint16_t exp, int count) {
    static constexpr const char* kAcc[gags::kTracks] = {"Medium", "Perfect", "Low", "High", "Medium", "High", "Low"};
    // AvPropTargetCat[AvPropTarget[track]][level]: 1 = the whole group.
    static constexpr uint8_t kGroup[gags::kTracks][gags::kLevels] = {
        {0, 1, 0, 1, 0, 1, 1}, {0, 0, 0, 0, 0, 0, 1}, {0, 1, 0, 1, 0, 1, 1}, {1, 1, 1, 1, 1, 1, 1},
        {0, 0, 0, 0, 0, 0, 1}, {0, 0, 0, 0, 0, 0, 1}, {0, 0, 0, 0, 0, 0, 1}};
    const int minD = gags::kDamage[track][level][0], maxD = gags::kDamage[track][level][1];
    const int minE = gags::kLevelExp[track][level];
    const int maxE = level + 1 < gags::kLevels ? gags::kLevelExp[track][level + 1] : gags::kMaxSkill;
    const int expVal = exp < maxE ? exp : maxE;
    const float expPerHp = float(maxE - minE + 1) / float(maxD - minD + 1);
    int damage = int(std::floor((expVal - minE) / expPerHp)) + minD;
    if (damage <= 0) damage = minD;
    const int unlocked = gags::levelsUnlocked(track, exp);
    const int maxItems = unlocked > 0 ? gags::kCarry[track][unlocked - 1][level] : 0;
    const bool heal = track == 0;
    snprintf(out, n, "%s %d/%d  Accuracy: %s  %s: %d  %s", gags::kGagNames[track][level], count,
             maxItems, kAcc[track], heal ? "Toon-up" : "Damage", damage,
             kGroup[track][level] ? (heal ? "All Toons" : "All Cogs") : (heal ? "One Toon" : "One Cog"));
}

int healthCondition(float f) {
    return f > 0.95f ? 0 : f > 0.7f ? 1 : f > 0.3f ? 2 : f > 0.05f ? 3 : f > 0.0f ? 4 : 5;
}

// Suit.healthColors / healthGlowColors. Conditions 4 and 5 blink between red and grey
// (__blinkRed / __blinkGray: red 0.75 s then grey 0.1 s; 0.25 s / 0.1 s once defeated).
void healthColors(int condition, u32* light, u32* glow) {
    static constexpr u32 kLight[5] = {theme::rgb(0x00, 0xFF, 0x00), theme::rgb(0xFF, 0xFF, 0x00),
                                      theme::rgb(0xFF, 0x80, 0x00), theme::rgb(0xFF, 0x00, 0x00),
                                      theme::rgb(0x4C, 0x4C, 0x4C)};
    static constexpr u32 kGlow[5] = {theme::rgb(0x40, 0xFF, 0x40, 0x80), theme::rgb(0xFF, 0xFF, 0x40, 0x80),
                                     theme::rgb(0xFF, 0x80, 0x40, 0x80), theme::rgb(0xFF, 0x40, 0x40, 0x80),
                                     theme::rgb(0x4C, 0x4C, 0x4C, 0x00)};
    int i = condition;
    if (condition >= 4) {
        const u64 period = condition == 4 ? 850 : 350, red = condition == 4 ? 750 : 250;
        i = osGetTime() % period < red ? 3 : 4;
    }
    *light = kLight[i];
    *glow = kGlow[i];
}

// The suit's chest light (Suit.generateHealthBar: a round button with a glow) beside a meter
// in the same colour, so the health reads at a glance on the small screen.
void drawHealthMeter(float x, float y, float w, float f) {
    u32 light, glow;
    healthColors(healthCondition(f), &light, &glow);
    constexpr float kD = 11.0f; // light diameter
    theme::roundRect(x - 1.5f, y - 1.5f, kD + 3, kD + 3, (kD + 3) * 0.5f, glow, 0.55f);
    theme::roundRect(x, y, kD, kD, kD * 0.5f, theme::rgb(0x30, 0x30, 0x30), 0.56f);
    theme::roundRect(x + 1, y + 1, kD - 2, kD - 2, (kD - 2) * 0.5f, light, 0.57f);
    theme::roundRect(x + 3, y + 2.5f, 3, 2, 1, theme::mix(light, theme::kWhite, 0.7f), 0.58f); // shine
    const float bx = x + kD + 3, bw = w - kD - 3, by = y + 2, bh = kD - 4;
    theme::roundRect(bx, by, bw, bh, bh * 0.5f, theme::rgb(0x30, 0x30, 0x30), 0.56f);
    theme::roundRect(bx + 1, by + 1, bw - 2, bh - 2, (bh - 2) * 0.5f, theme::rgb(0x5A, 0x5A, 0x5A), 0.57f);
    if (f > 0.0f) {
        const float fw = (bw - 2) * f < bh - 2 ? bh - 2 : (bw - 2) * f; // never thinner than round
        theme::roundRect(bx + 1, by + 1, fw, bh - 2, (bh - 2) * 0.5f, light, 0.58f);
        theme::roundRect(bx + 2, by + 1.5f, fw - 2 > 0 ? fw - 2 : 0, 1.0f, 0.5f,
                         theme::mix(light, theme::kWhite, 0.6f), 0.59f);
    }
}

// InventoryNew's track labels: TextEncoder.upper(Tracks[track]).
constexpr const char* kTrackLabels[7] = {"TOON-UP", "TRAP", "LURE", "SOUND", "THROW", "SQUIRT", "DROP"};

int headIndex(const char* code) {
    if (!code[0]) return -1;
    for (int i = 0; i < kNumSuitHeads; i++) {
        if (!strcmp(kSuitHeadSubTex[i].code, code)) return i;
    }
    return -1;
}

} // namespace

bool BattlePanel::init() {
    textCache_.clear();
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
    textCache_.parse(&t, buf_, line, 1024);
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
    // TownBattle.checkHealTrapLure: no Toon-Up alone, no Trap once every Cog is trapped or lured,
    // no Lure (or Trap) once every Cog is lured.
    int lured = 0;
    int trappedOrLured = 0;
    for (int i = 0; i < v.numCogs; i++) {
        lured += v.cogs[i].lured;
        trappedOrLured += v.cogs[i].lured || v.cogs[i].trapped;
    }
    if (track == world::kTrackHeal && v.numToons == 1) return false;
    if (track == world::kTrackTrap && (trappedOrLured == v.numCogs || lured == v.numCogs)) return false;
    if (track == world::kTrackLure && lured == v.numCogs) return false;
    return world::attackAffectsGroup(track, level) || targetFor(v, track) != 0;
}

u32 BattlePanel::handleInput(u32 keysDown, const touchPosition& touch, const world::BattleView& v,
                             const BattleGags& g) {
    // The battle owns every button while its view is shown (main.cpp also freezes the toon).
    constexpr u32 kAllButtons = 0xFFFFFFFFu;
    if (v.phase == world::BattlePhase::Reward) {
        // Continue: touch it, or A / START (the AI ignores a second rewardDone; Battle guards it).
        pad::buildBattleReward(focus_);
        if (!focus_.hasFocus()) focus_.setFocusById(pad::battle::kIdContinue);
        int activated = -1;
        focus_.handleInput(keysDown & ~KEY_A, touch, &activated);
        if (activated < 0 && (keysDown & (KEY_A | KEY_START))) {
            activated = pad::battle::kIdContinue;
            focus_.logActivate(activated);
        }
        if (activated == pad::battle::kIdContinue) pending_.kind = world::BattleAction::Continue;
        return kAllButtons;
    }
    if (v.phase != world::BattlePhase::Input) {
        focus_.clear(); // the next round starts on the first usable gag again
        mode_ = Mode::Attack;
        padDetail_ = false;
        return kAllButtons;
    }
    if (mode_ == Mode::Sos && !v.canSos) mode_ = Mode::Attack;
    if (mode_ == Mode::Fire && !v.canFire) mode_ = Mode::Attack;
    auto enter = [&](Mode m, int focusId) {
        mode_ = m;
        focus_.clear();
        wantFocus_ = focusId; // applied once the new panel's table is built
        if (focusId < 0) wantFocus_ = -1;
        TT_LOG("padnav: battle panel %s", m == Mode::Sos ? "sos" : m == Mode::Fire ? "fire" : "attack");
    };

    // ---- TownBattleSOSPanel: Rescued Toons cards, BACK (B too) ----
    if (mode_ == Mode::Sos) {
        const int views = (v.numSosCards + pad::battle::kSosPerView - 1) / pad::battle::kSosPerView;
        if (sosView_ >= views) sosView_ = 0;
        const int first = sosView_ * pad::battle::kSosPerView;
        int n = v.numSosCards - first;
        if (n > pad::battle::kSosPerView) n = pad::battle::kSosPerView;
        if (n < 0) n = 0;
        bool callable[pad::battle::kSosPerView] = {};
        for (int i = 0; i < n; i++) callable[i] = sosCallable(v, first + i);
        pad::buildBattleSos(focus_, n, callable, views > 1);
        if (wantFocus_ >= 0) {
            focus_.setFocusById(wantFocus_);
            wantFocus_ = -1;
        }
        if (!focus_.hasFocus()) {
            for (int i = 0; i < n && !focus_.hasFocus(); i++)
                if (callable[i]) focus_.setFocusById(pad::battle::kIdSosCard0 + i);
            if (!focus_.hasFocus()) focus_.setFocusById(pad::battle::kIdBack);
        }
        int activated = -1;
        focus_.handleInput(keysDown & ~KEY_B, touch, &activated);
        if (activated < 0 && (keysDown & KEY_B)) activated = pad::battle::kIdBack;
        if (views > 1 && (keysDown & (KEY_L | KEY_R))) activated = pad::battle::kIdSosPage;
        if (activated == pad::battle::kIdBack) {
            enter(Mode::Attack, returnFocus_);
        } else if (activated == pad::battle::kIdSosPage) {
            sosView_ = (sosView_ + 1) % views;
            TT_LOG("padnav: battle sos page %d", sosView_ + 1);
        } else if (activated >= pad::battle::kIdSosCard0 && activated < pad::battle::kIdSosCard0 + n) {
            const int card = first + activated - pad::battle::kIdSosCard0;
            if (sosCallable(v, card)) {
                pending_.kind = world::BattleAction::NpcSos;
                pending_.target = v.sosCards[card].npcId;
                TT_LOG("battle: call SOS toon %u (%u left)", unsigned(v.sosCards[card].npcId),
                       unsigned(v.sosCards[card].count));
                mode_ = Mode::Attack;
            }
        }
        return kAllButtons;
    }

    // ---- FireCogPanel: "Fire which Cog?" on the Cog cards, BACK (B too) ----
    if (mode_ == Mode::Fire) {
        bool ok[pad::battle::kMaxCogs] = {};
        for (int i = 0; i < v.numCogs && i < pad::battle::kMaxCogs; i++) ok[i] = fireable(v, i);
        pad::buildBattleFire(focus_, v.numCogs, ok);
        if (wantFocus_ >= 0) {
            focus_.setFocusById(wantFocus_);
            wantFocus_ = -1;
        }
        if (!focus_.hasFocus()) {
            if (target_ < v.numCogs && ok[target_]) focus_.setFocusById(pad::battle::kIdCog0 + target_);
            for (int i = 0; i < v.numCogs && !focus_.hasFocus(); i++)
                if (ok[i]) focus_.setFocusById(pad::battle::kIdCog0 + i);
            if (!focus_.hasFocus()) focus_.setFocusById(pad::battle::kIdBack);
        }
        int activated = -1;
        focus_.handleInput(keysDown & ~KEY_B, touch, &activated);
        if (activated < 0 && (keysDown & KEY_B)) activated = pad::battle::kIdBack;
        if (activated == pad::battle::kIdBack) {
            enter(Mode::Attack, returnFocus_);
        } else if (activated >= pad::battle::kIdCog0 && activated < pad::battle::kIdCog0 + pad::battle::kMaxCogs) {
            const int cog = activated - pad::battle::kIdCog0;
            if (cog < v.numCogs && fireable(v, cog)) {
                pending_.kind = world::BattleAction::Fire;
                pending_.target = v.cogs[cog].doId;
                TT_LOG("battle: fire cog %u (%d pink slips)", unsigned(v.cogs[cog].doId), v.pinkSlips);
                mode_ = Mode::Attack;
            }
        }
        return kAllButtons;
    }

    // ---- padnav: one focus table for the cogs, the gag grid and Fire / Pass / Run / SOS ----
    bool cogActive[pad::battle::kMaxCogs] = {};
    for (int i = 0; i < v.numCogs && i < pad::battle::kMaxCogs; i++) cogActive[i] = v.cogs[i].active;
    bool usableGag[gags::kTracks * gags::kLevels];
    for (int t = 0; t < gags::kTracks; t++) {
        for (int l = 0; l < gags::kLevels; l++) usableGag[t * gags::kLevels + l] = usable(v, g, t, l);
    }
    bool owned[gags::kTracks];
    for (int t = 0; t < gags::kTracks; t++) owned[t] = g.access && g.access[t];
    pad::buildBattleInput(focus_, v.numCogs, cogActive, usableGag, v.canRun, v.canFire, v.canSos, owned);
    // ---- tutorial ---- InventoryNew tutorialFlag: Pass is hidden with Run, SOS and Fire
    if (v.tutorialArrows && focus_.indexOf(pad::battle::kIdPass) >= 0)
        focus_.setItemEnabled(focus_.indexOf(pad::battle::kIdPass), false);
    if (wantFocus_ >= 0) {
        focus_.setFocusById(wantFocus_);
        wantFocus_ = -1;
    }
    if (!focus_.hasFocus()) {
        // Like TownBattle's attack panel: start on the first gag we can use, else Pass.
        for (int i = 0; i < focus_.count(); i++) {
            if (focus_.item(i).enabled && pad::battle::isGagId(focus_.item(i).id)) {
                focus_.setFocus(i);
                break;
            }
        }
        if (!focus_.hasFocus()) focus_.setFocusById(pad::battle::kIdPass);
    }

    // L / R cycle the target cog without leaving the gag grid.
    const int step = (keysDown & KEY_R) ? 1 : (keysDown & KEY_L) ? -1 : 0;
    if (step != 0 && v.numCogs > 1) {
        for (int k = 1; k <= v.numCogs; k++) {
            const int next = ((target_ + step * k) % v.numCogs + v.numCogs) % v.numCogs;
            if (v.cogs[next].active) {
                target_ = next;
                TT_LOG("padnav: battle target %d", target_);
                break;
            }
        }
    }

    int activated = -1;
    const int before = focus_.focusedId(); // BACK returns here (a touch moves the focus first)
    focus_.handleInput(keysDown, touch, &activated);
    if (keysDown & (KEY_DUP | KEY_DDOWN | KEY_DLEFT | KEY_DRIGHT)) padDetail_ = true;
    if (activated == pad::battle::kIdSos || activated == pad::battle::kIdFire) returnFocus_ = before >= 0 ? before : activated;
    if (activated >= pad::battle::kIdCog0 && activated < pad::battle::kIdCog0 + pad::battle::kMaxCogs) {
        target_ = activated - pad::battle::kIdCog0;
        TT_LOG("padnav: battle target %d", target_);
    } else if (pad::battle::isGagId(activated)) {
        const int t = activated / 10;
        const int l = activated % 10;
        if (usable(v, g, t, l)) {
            pending_.kind = world::BattleAction::Attack;
            pending_.track = t;
            pending_.level = l;
            pending_.target = world::attackAffectsGroup(t, l) ? 0 : targetFor(v, t);
        }
    } else if (activated == pad::battle::kIdPass) {
        pending_.kind = world::BattleAction::Pass;
    } else if (activated == pad::battle::kIdRun && v.canRun) {
        pending_.kind = world::BattleAction::Run;
    } else if (activated == pad::battle::kIdSos && v.canSos) {
        sosView_ = 0;
        enter(Mode::Sos, -1);
    } else if (activated == pad::battle::kIdFire && v.canFire) {
        enter(Mode::Fire, -1);
    }
    // ---- end padnav ----
    return kAllButtons;
}

// TownBattleSOSPanel.__updateNPCFriendsPanel: a card with calls left, unless it is a Lure card
// when every Cog is lured or a Trap card when none can be trapped (TownBattle.checkHealTrapLure).
bool BattlePanel::sosCallable(const world::BattleView& v, int card) const {
    if (card < 0 || card >= v.numSosCards || v.sosCards[card].count == 0) return false;
    const SosCardDef* def = findSosCard(v.sosCards[card].npcId);
    if (!def) return false;
    int lured = 0, trappedOrLured = 0;
    for (int i = 0; i < v.numCogs; i++) {
        lured += v.cogs[i].lured;
        trappedOrLured += v.cogs[i].lured || v.cogs[i].trapped;
    }
    if (def->track == world::kTrackLure && lured == v.numCogs) return false;
    if (def->track == world::kTrackTrap && (trappedOrLured == v.numCogs || lured == v.numCogs)) return false;
    return true;
}

// FireCogPanel.__placeButtons: every Cog costs one pink slip (TownBattle.cogFireCosts).
bool BattlePanel::fireable(const world::BattleView& v, int cog) const {
    return cog >= 0 && cog < v.numCogs && v.cogs[cog].active && v.pinkSlips >= 1;
}

void BattlePanel::draw(const world::BattleView& v, const BattleGags& g) {
    // Upstream's TownBattle: the cream gag panel with a row per track in its colour, Cog panels
    // with the suit's health light colours, and round Pass / Run buttons.
    theme::sky();
    // TownBattleSOSPanel / FireCogPanel titles while those panels are open.
    const char* status = v.status ? v.status : "";
    if (v.phase == world::BattlePhase::Input && mode_ == Mode::Sos)
        status = v.numSosCards > 0 ? "Call which friend?" : "No friends to call!";
    if (v.phase == world::BattlePhase::Input && mode_ == Mode::Fire) status = "Fire which Cog?";
    // InventoryNew's detail panel follows the rollover; here it follows the D-Pad focus once the
    // player has moved it (until then the panel says "Click a gag to attack", as upstream does
    // before any rollover).
    const bool detail = padDetail_ && v.phase == world::BattlePhase::Input && mode_ == Mode::Attack;
    const int focusId = detail ? focus_.focusedId() : -1;
    const int nc = v.numCogs < pad::battle::kMaxCogs ? v.numCogs : pad::battle::kMaxCogs;
    // InventoryNew uses Toontown's default dialog card for this prompt. Put the real card beside
    // the Cog cards, shortened and reflowed so it covers neither the gags nor a Cog. Four-Cog
    // battles keep the compact status strip because the cards fill the whole row.
    const bool bigHint = v.phase == world::BattlePhase::Input && mode_ == Mode::Attack && !detail &&
                         v.tutorialArrows && nc < 4;
    if (bigHint) {
        const float cogsRight = pad::battle::kCogX + nc * kCogW +
                                (nc > 0 ? (nc - 1) * pad::battle::kCogGap : 0);
        const float hintX = cogsRight + pad::battle::kHintGap +
                            (kW - 2.0f - (cogsRight + pad::battle::kHintGap) - pad::battle::kHintW) * 0.5f;
        if (invArtReady()) {
            invTintBegin();
            drawInvHint(hintX, pad::battle::kHintY, pad::battle::kHintW, pad::battle::kHintH, 0.55f,
                        theme::rgb(0xFF, 0xFF, 0xBF));
            invTintEnd();
        } else {
            theme::paper(hintX, pad::battle::kHintY, pad::battle::kHintW, pad::battle::kHintH, 7.0f,
                        theme::rgb(0xFF, 0xFF, 0xBF));
        }
        centered(hintX + pad::battle::kHintW * 0.5f, pad::battle::kHintY + pad::battle::kHintTextTop,
                 pad::battle::kHintTextScale, kInk, pad::battle::kHintTextMaxW, "Click a gag", 0.6f, true);
        centered(hintX + pad::battle::kHintW * 0.5f,
                 pad::battle::kHintY + pad::battle::kHintTextTop + pad::battle::kHintTextStep,
                 pad::battle::kHintTextScale, kInk, pad::battle::kHintTextMaxW, "to attack", 0.6f, true);
    } else {
        theme::paper(1, 0, kW - 2, 15, 5.0f);
        if (pad::battle::isGagId(focusId) && g.count && g.exp) {
            const int t = focusId / 10, l = focusId % 10;
            char detailText[96];
            gagDetail(detailText, sizeof detailText, t, l, g.exp[t], g.count[t][l]);
            C2D_Text d;
            textCache_.parse(&d, buf_, detailText, 1024);
            theme::textFit(d, 6, -1, 0.44f, kInk, kW - 12, 0.6f);
        } else {
            text(6, -1, 0.44f, kInk, "%s", status);
            if (g.count) {
                int total = 0;
                for (int t = 0; t < gags::kTracks; t++) {
                    for (int l = 0; l < gags::kLevels; l++) {
                        total += g.count[t][l];
                    }
                }
                text(kW - 96, -1, 0.40f, kInk, "Total gags %d/%u", total, unsigned(g.maxCarry));
            }
        }
    }
    // The round timer is TownBattle's ToontownTimer in the top screen's top-right corner (ui/hud).

    // Cogs: name, head, level and the chest light's health. The target wears a red frame.
    SuitHeadAtlas::instance().init(); // once; the Cog Gallery shares it
    for (int i = 0; i < v.numCogs && i < 4; i++) {
        const world::BattleCogView& c = v.cogs[i];
        const float x = 2 + i * (kCogW + 1);
        const bool target = i == target_ && v.phase == world::BattlePhase::Input;
        theme::paper(x, kCogY, kCogW, kCogH, 5.0f, c.active ? theme::kPaper : theme::kGrey,
                     target ? theme::kRed : theme::kGreyDark);
        if (target) theme::roundRect(x + 1, kCogY + 1, kCogW - 2, 3, 1, theme::kRed);
        // The head on the left, then the name, the level (or Lured / Fire) and the health light.
        const int head = c.skelecog ? -1 : headIndex(c.code);
        float rx = x + 4;
        if (head >= 0) {
            theme::roundRect(x + 3, kCogY + 4, 26, 26, 5, theme::kPaperShade, 0.56f);
            SuitHeadAtlas::instance().drawHead(head / 8, head % 8, x + 3.5f, kCogY + 4.5f, 25.0f, 0.58f);
            rx = x + 32;
        }
        C2D_Text name;
        textCache_.parse(&name, buf_, c.name, 1024);
        theme::textFit(name, rx, kCogY + 1, 0.36f, c.active ? kInk : kInkSoft, x + kCogW - 3 - rx, 0.6f);
        if (mode_ == Mode::Fire && v.phase == world::BattlePhase::Input && fireable(v, i)) {
            // FireCogPanel's avatar buttons: each Cog card is a pink-slip target.
            theme::roundRect(rx - 1, kCogY + 11, 26, 10, 4, theme::kRed, 0.57f);
            text(rx + 3, kCogY + 10, 0.32f, theme::kWhite, "Fire");
        } else if (c.lured) {
            theme::roundRect(rx - 1, kCogY + 11, 30, 10, 4, theme::kGreen, 0.57f);
            text(rx + 3, kCogY + 10, 0.32f, theme::kWhite, "Lured");
        } else if (c.level > 0) {
            text(rx, kCogY + 10, 0.32f, kInkSoft, "Level %d", c.level);
        }
        if (loggedCog_[i] != c.doId || loggedHp_[i] != c.hp || loggedMax_[i] != c.maxHp) {
            loggedCog_[i] = c.doId;
            loggedHp_[i] = c.hp;
            loggedMax_[i] = c.maxHp;
            TT_LOG("battle: cog %u %s hp %d/%d", unsigned(c.doId), c.code, c.hp, c.maxHp);
        }
        // A Cog joins at full health; until its max is known draw it full.
        const float f = c.maxHp > 0 ? hpFraction(c.hp, c.maxHp) : 1.0f;
        drawHealthMeter(rx, kCogY + 20, x + kCogW - 4 - rx, f);

    }

    if (v.phase == world::BattlePhase::Reward && !v.floorReward) {
        // RewardPanel: the defeated Cogs have left, so the panel takes the space under the status.
        const float top = kCogY + 2;
        theme::paper(2, top, kW - 4, kBtnY - top - 4, 6.0f);
        text(10, top + 3, 0.5f, theme::kRedDark, "Cogs defeated: %d", v.cogsDefeated);
        // RewardPanel.incrementExp per track: a bar in the track colour on a darker frame, range =
        // the next gag level's experience, value = our experience ("exp/next" centred), or the
        // uber "N to Go!" past regMaxSkill.
        constexpr int kRegMaxSkill = 10000, kUberSkill = 500;
        constexpr float kBarX = 76, kBarW = 180, kBarH = 13;
        for (int i = 0; i < v.numRewardRows; i++) {
            const world::BattleRewardRow& r = v.reward[i];
            const float y = top + 22 + i * 18;
            int value = r.orig + r.earned;
            if (value > gags::kMaxSkill) value = gags::kMaxSkill;
            int range = kUberSkill;
            char label[24];
            if (value >= kRegMaxSkill) {
                value -= kRegMaxSkill;
                snprintf(label, sizeof label, "%d to Go!", kUberSkill - value);
            } else {
                for (int l = 0; l < gags::kLevels; l++) {
                    if (value < gags::kLevelExp[r.track][l]) {
                        range = gags::kLevelExp[r.track][l];
                        break;
                    }
                }
                snprintf(label, sizeof label, "%d/%d", value, range);
            }
            const float f = range > 0 ? (value >= range ? 1.0f : float(value) / float(range)) : 1.0f;
            const u32 col = theme::kTrackColors[r.track];
            text(10, y, 0.42f, kInk, "%s", theme::kTrackTitles[r.track]);
            theme::roundRect(kBarX, y + 1, kBarW, kBarH, 4, theme::shade(col, 0.7f));
            if (f > 0.0f) theme::roundRect(kBarX + 1, y + 2, (kBarW - 2) * f, kBarH - 2, 3, col, 0.55f);
            C2D_Text t;
            textCache_.parse(&t, buf_, label, 1024);
            theme::textCentered(t, kBarX + kBarW * 0.5f, y + 0.5f, 0.38f, kInk, kBarW - 6, 0.6f);
            if (r.earned > 0) text(kBarX + kBarW + 6, y, 0.44f, theme::kGreenDark, "+%d", r.earned);
        }
        drawToons(v, pad::battle::kToonsRewardX, pad::battle::kToonsRewardW);
        theme::button(pad::battle::kContinueX, pad::battle::kContinueY, pad::battle::kContinueW, pad::battle::kContinueH,
                      v.rewardSent ? theme::kGrey : theme::kGreen, v.rewardSent ? theme::kGreyDark : theme::kGreenDark);
        centered(pad::battle::kContinueX + pad::battle::kContinueW * 0.5f, pad::battle::kContinueY + 5, 0.5f,
                 v.rewardSent ? kInkSoft : theme::kWhite, pad::battle::kContinueW - 8, "Continue");
        // ---- tutorial ---- Movie.playTutorialReward's TTDialog (Acknowledge) in GlobalDialogColor over the panel
        if (v.tutorialDialog) {
            constexpr float dx = 40, dy = 40, dw = kW - 80, dh = 100;
            theme::roundRect(dx - 2, dy - 2, dw + 4, dh + 4, 9, theme::rgb(0x50, 0x50, 0x50));
            theme::roundRect(dx, dy, dw, dh, 8, theme::rgb(0xFF, 0xFF, 0xBF));
            C2D_Text t;
            textCache_.parse(&t, buf_, v.tutorialDialog, 1024);
            C2D_DrawText(&t, C2D_WithColor | C2D_WordWrap, dx + 12, dy + 12, 0.6f, 0.5f, 0.5f, kInk, dw - 24);
            centered(dx + dw * 0.5f, dy + dh - 22, 0.5f, theme::kGreenDark, dw - 20, "OK (A)");
        }
        focus_.drawHighlight();
        return;
    }

    const bool choosing = v.phase == world::BattlePhase::Input;
    if (choosing && mode_ == Mode::Sos) {
        drawSos(v);
    } else if (choosing && mode_ == Mode::Fire) {
        drawFire(v);
    } else {
        drawGagGrid(v, g); // ends with drawButtons
    }
    drawToons(v, pad::battle::kToonsX, pad::battle::kToonsW);
    if (choosing) focus_.drawHighlight();
}

void BattlePanel::drawGagGrid(const world::BattleView& v, const BattleGags& g) {
    const bool choosing = v.phase == world::BattlePhase::Input;
    const bool art = invArtReady();
    bool owned[gags::kTracks];
    for (int t = 0; t < gags::kTracks; t++) owned[t] = g.access && g.access[t];
    float rowY[gags::kTracks], rowH[gags::kTracks];
    pad::battleRows(owned, rowY, rowH);
    int levels[gags::kTracks] = {};
    bool ok[gags::kTracks][gags::kLevels] = {};
    for (int t = 0; t < gags::kTracks; t++) {
        if (!owned[t] || !g.count) continue;
        levels[t] = gags::levelsUnlocked(t, g.exp[t]);
        for (int l = 0; l < levels[t]; l++) ok[t][l] = choosing && usable(v, g, t, l);
    }
    const int focused = choosing ? focus_.focusedId() : -1;
    const float cellX0 = kGridX;
    auto cellX = [&](int l) { return cellX0 + l * (kCellW + kCellGap); };

    // Art pass: the frame, the rows in their track colours and the button faces (one tint batch).
    if (art) {
        invTintBegin();
        drawInvMenu(pad::battle::kFrameX, pad::battle::kFrameY, pad::battle::kFrameW, pad::battle::kFrameH, 0.5f);
        for (int t = 0; t < gags::kTracks; t++) {
            drawInvRow(pad::battle::kRowX, rowY[t], pad::battle::kRowW, rowH[t], kGridX, 0.52f, theme::kTrackColors[t]);
            for (int l = 0; l < levels[t]; l++) {
                // makePressable: Up (Rollover under the focus) in PressableImageColor; makeUnpressable:
                // Flat in UnpressableImageColor.
                const bool f = focused == pad::battle::gagId(t, l);
                const InvArt face = ok[t][l] ? (f ? InvArt::ButtonRollover : InvArt::ButtonUp) : InvArt::ButtonFlat;
                drawInvArt(face, cellX(l), rowY[t], kCellW, rowH[t], 0.54f, ok[t][l] ? kInvPressable : kInvUnpressable);
            }
        }
        invTintEnd();
    } else {
        theme::paper(1, kGridY - 3, kW - 2, kGridH + 4, 5.0f);
        for (int t = 0; t < gags::kTracks; t++) {
            theme::roundRect(pad::battle::kRowX, rowY[t], pad::battle::kRowW, rowH[t], 4, theme::kTrackColors[t], 0.52f);
            for (int l = 0; l < levels[t]; l++)
                theme::roundRect(cellX(l), rowY[t], kCellW, rowH[t], 4, ok[t][l] ? kInvPressable : theme::kGrey, 0.54f);
        }
    }

    // Labels, icons and counts.
    for (int t = 0; t < gags::kTracks; t++) {
        const float y = rowY[t], h = rowH[t];
        C2D_Text label;
        textCache_.parse(&label, buf_, kTrackLabels[t], 1024);
        float lw = 0, lh = 0;
        float ls = 0.46f;
        C2D_TextGetDimensions(&label, ls, ls, &lw, &lh);
        const float room = kGridX - pad::battle::kRowX - 8;
        if (lw > room) {
            ls *= room / lw;
            C2D_TextGetDimensions(&label, ls, ls, &lw, &lh);
        }
        const bool showExp = (t == 4 || t == 5) && g.exp && h >= 24.0f;
        drawOutlinedText(label, pad::battle::kRowX + 4, y + (showExp ? 1.0f : (h - lh) * 0.5f), 0.56f, ls,
                         theme::rgb(0x33, 0x33, 0x33), theme::kWhite);
        if (showExp) {
            const int next = gags::kLevelExp[t][1];
            const float progress = next > 0 ? (g.exp[t] < next ? float(g.exp[t]) / next : 1.0f) : 1.0f;
            char expStr[16];
            snprintf(expStr, sizeof expStr, "%u/%d", unsigned(g.exp[t]), next);
            C2D_Text et;
            textCache_.parse(&et, buf_, expStr, 1024);
            const float barY = y + h - 6.0f;
            drawOutlinedText(et, pad::battle::kRowX + 5, barY - 9.0f, 0.59f, 0.32f,
                             theme::kWhite, theme::rgb(0x20, 0x20, 0x20));
            theme::roundRect(pad::battle::kRowX + 4, barY, room - 4, 4, 1, theme::rgb(0x30, 0x30, 0x30, 0xC0));
            if (progress > 0.0f)
                theme::roundRect(pad::battle::kRowX + 4, barY, (room - 4) * progress, 4, 1,
                                 theme::mix(theme::kTrackColors[t], theme::kWhite, 0.35f), 0.58f);
        }
        for (int l = 0; l < levels[t]; l++) {
            const float x = cellX(l);
            // geom_scale 0.7 of the button: the icon fills most of the face, a little left of centre.
            float icon = h * 0.84f;
            if (icon > kCellW * 0.78f) icon = kCellW * 0.78f;
            world::GagAtlas::instance().drawIcon(t, l, x + (kCellW - icon) * 0.5f - 2, y + (h - icon) * 0.5f, icon, 0.58f,
                                                 ok[t][l] ? 1.0f : 0.3f); // UnpressableGeomColor alpha 0.3
            char num[8];
            snprintf(num, sizeof num, "%u", unsigned(g.count[t][l]));
            C2D_Text n;
            textCache_.parse(&n, buf_, num, 1024);
            const float ns = h >= 24 ? 0.42f : h >= 16 ? 0.36f : 0.3f;
            float nw = 0, nh = 0;
            C2D_TextGetDimensions(&n, ns, ns, &nw, &nh);
            // text_pos (0.07, -0.04), right-aligned: the count sits in the bottom-right corner.
            drawOutlinedText(n, x + kCellW - 3 - nw, y + h - nh + 1, 0.6f, ns,
                             ok[t][l] ? theme::kWhite : theme::rgb(0xFF, 0xFF, 0xFF, 0x60),
                             ok[t][l] ? theme::rgb(0x00, 0x2A, 0x55) : theme::rgb(0x20, 0x20, 0x20, 0x60));
        }
    }
    drawButtons(v, g);
}

// InventoryNew's battle button: InventoryButtonUp in image_color (0, 0.6, 1) with white text; a
// disabled one (Fire without pink slips) in (0.4, 0.4, 0.4).
void BattlePanel::smallButton(float x, float y, float w, bool enabled, const char* label) {
    const u32 face = enabled ? kInvPressable : kInvDisabledButton;
    if (invArtReady()) {
        invTintBegin();
        drawInvArt(InvArt::ButtonUp, x, y, w, pad::battle::kSmallH, 0.55f, face);
        invTintEnd();
    } else {
        theme::button(x, y, w, pad::battle::kSmallH, face, theme::shade(face, 0.62f), false, 5.0f);
    }
    C2D_Text t;
    textCache_.parse(&t, buf_, label, 1024);
    float s = 0.42f, tw = 0, th = 0;
    C2D_TextGetDimensions(&t, s, s, &tw, &th);
    if (tw > w - 8) {
        s *= (w - 8) / tw;
        C2D_TextGetDimensions(&t, s, s, &tw, &th);
    }
    drawOutlinedText(t, x + (w - tw) * 0.5f, y + (pad::battle::kSmallH - th) * 0.5f, 0.6f, s,
                     enabled ? theme::kWhite : theme::rgb(0xD8, 0xD8, 0xD8), theme::rgb(0x00, 0x2A, 0x55, enabled ? 0xFF : 0x80));
}

void BattlePanel::drawButtons(const world::BattleView& v, const BattleGags& g) {
    const bool choosing = v.phase == world::BattlePhase::Input;
    // ---- tutorial ---- InventoryNew.setActivateMode(tutorialFlag=1) hides RUN, SOS, PASS and FIRE;
    // show() blinks tutArrows at the Throw and Squirt gags (arrowsOn(-0.43, -0.12, 180, -0.43, -0.24, 180,
    // onTime 1.0, offTime 0.2)), each only when the Toon has that gag.
    if (choosing && v.tutorialArrows) {
        const float s = static_cast<float>(osGetTime() % 1200) / 1000.0f;
        const float a = s < 1.0f ? 0.25f + 0.75f * s : 1.0f - 0.75f * (s - 1.0f) / 0.2f;
        bool owned[gags::kTracks];
        for (int t = 0; t < gags::kTracks; t++) owned[t] = g.access && g.access[t];
        float rowY[gags::kTracks], rowH[gags::kTracks];
        pad::battleRows(owned, rowY, rowH);
        for (int t : {4, 5}) { // THROW_TRACK, SQUIRT_TRACK
            if (!g.count || !g.count[t][0]) continue;
            drawTutorialArrow(kGridX + kCellW + 4, rowY[t] + rowH[t] * 0.5f, 180, 20.0f, a);
        }
        return;
    }
    smallButton(pad::battle::kFireX, kBtnY, pad::battle::kSmallW, choosing && v.canFire, "FIRE");
    smallButton(pad::battle::kPassX, kBtnY, pad::battle::kSmallW, choosing, "PASS");
    smallButton(pad::battle::kRunX, pad::battle::kRow2Y, pad::battle::kSmallW, choosing && v.canRun, "RUN");
    smallButton(pad::battle::kSosX, pad::battle::kRow2Y, pad::battle::kSmallW, choosing && v.canSos, "SOS");
}

// TownBattleSOSPanel: "Call which friend?" over the Rescued Toons (NPCFriendPanel's cards: the
// ability in its track colour, the name, stars and calls left); BACK returns to the gags. The
// stock server has no FriendManager, so the friends column upstream shows is always empty.
void BattlePanel::drawSos(const world::BattleView& v) {
    theme::paper(1, kGridY - 3, kW - 2, kGridH + 4, 5.0f);
    if (v.numSosCards == 0) {
        centered(kW * 0.5f, kGridY + 30, 0.6f, theme::kRedDark, kW - 20, "No friends to call!");
        centered(kW * 0.5f, kGridY + 58, 0.42f, kInk, kW - 30, "Rescue Toons from the Sellbot VP");
        centered(kW * 0.5f, kGridY + 74, 0.42f, kInk, kW - 30, "to call them into battle.");
    }
    const int first = sosView_ * pad::battle::kSosPerView;
    for (int i = 0; i < pad::battle::kSosPerView && first + i < v.numSosCards; i++) {
        const world::BattleView::SosCard& e = v.sosCards[first + i];
        const SosCardDef* def = findSosCard(e.npcId);
        const bool ok = sosCallable(v, first + i);
        const float cx = pad::battle::kCardX + (i % 4) * (pad::battle::kCardW + pad::battle::kCardGap), cy = pad::battle::kCardY + (i / 4) * (pad::battle::kCardH + pad::battle::kCardGap);
        const u32 col = def && def->track < 7 ? theme::kTrackColors[def->track] : theme::rgb(0xF6, 0xD2, 0x60);
        theme::roundRect(cx, cy, pad::battle::kCardW, pad::battle::kCardH, 6, ok ? theme::shade(col, 0.6f) : theme::kGreyDark);
        theme::roundRect(cx + 1, cy + 1, pad::battle::kCardW - 2, pad::battle::kCardH - 2, 5, ok ? theme::kPaper : theme::kGrey);
        theme::roundRect(cx + 1, cy + 1, pad::battle::kCardW - 2, 14, 5, ok ? col : theme::kGreyDark);
        centered(cx + pad::battle::kCardW * 0.5f, cy + 1, 0.36f, kInk, pad::battle::kCardW - 6, def ? sosTrackName(def->track, def->level) : "SOS");
        centered(cx + pad::battle::kCardW * 0.5f, cy + 17, 0.38f, ok ? kInk : kInkSoft, pad::battle::kCardW - 6, def ? def->name : "Toon");
        const int stars = def ? def->stars : 1;
        for (int k = 0; k < stars; k++) {
            const float sx = cx + pad::battle::kCardW * 0.5f + (k - (stars - 1) * 0.5f) * 11.0f;
            star(sx, cy + 38, 5.5f, ok ? kStarDark : theme::kGreyDark);
            star(sx, cy + 38, 4.2f, ok ? kStar : theme::kGrey);
        }
        char left[24];
        snprintf(left, sizeof left, "%u Remaining", unsigned(e.count)); // NPCFriendPanelRemaining
        centered(cx + pad::battle::kCardW * 0.5f, cy + 47, 0.34f, ok ? theme::kGreenDark : kInkSoft, pad::battle::kCardW - 6, left);
    }
    smallButton(pad::battle::kBackX, kBtnY, pad::battle::kBackW, true, "BACK");
    const int views = (v.numSosCards + pad::battle::kSosPerView - 1) / pad::battle::kSosPerView;
    if (views > 1) {
        char page[32];
        snprintf(page, sizeof page, "Page %d / %d", sosView_ + 1, views);
        smallButton(pad::battle::kBackX, pad::battle::kRow2Y, pad::battle::kBackW, true, page);
    }
}

// FireCogPanel: "PINK SLIPS LEFT: n / FIRE WHICH COG?" (or NOT ENOUGH SLIPS!); the Cog cards
// above are the buttons.
void BattlePanel::drawFire(const world::BattleView& v) {
    theme::paper(1, kGridY - 3, kW - 2, kGridH + 4, 5.0f);
    // A pink slip, like the prop the Cogs hand out.
    const float px = 110, py = kGridY + 14, pw = 100, ph = 62;
    theme::roundRect(px + 3, py + 3, pw, ph, 3, theme::rgb(0xC8, 0x9A, 0xA8, 0x80));
    theme::roundRect(px, py, pw, ph, 3, kSlipPink);
    for (int k = 0; k < 4; k++) theme::roundRect(px + 12, py + 22 + k * 9, pw - 24, 1.5f, 0.5f, theme::rgb(0xD0, 0x7A, 0x90));
    centered(px + pw * 0.5f, py + 4, 0.5f, theme::kRedDark, pw - 8, "FIRED!");
    char line[40];
    snprintf(line, sizeof line, "PINK SLIPS LEFT: %d", v.pinkSlips);
    centered(kW * 0.5f, kGridY + 88, 0.5f, kInk, kW - 20, line);
    bool any = false;
    for (int i = 0; i < v.numCogs; i++) any = any || fireable(v, i);
    centered(kW * 0.5f, kGridY + 108, 0.5f, any ? theme::kBlueDark : theme::kRedDark, kW - 20,
             any ? "FIRE WHICH COG?" : "NOT ENOUGH SLIPS!");
    smallButton(pad::battle::kBackX, kBtnY, pad::battle::kBackW, true, "BACK");
}

void BattlePanel::centered(float cx, float y, float scale, u32 color, float maxW, const char* str, float z, bool bold) {
    C2D_Text t;
    textCache_.parse(&t, buf_, str, 1024);
    theme::textCentered(t, cx, y, scale, color, maxW, z);
    if (bold) theme::textCentered(t, cx + 0.5f, y, scale, color, maxW, z);
}

// TownBattleToonPanel: every toon in the battle on ToonBtl_Status_BG (image_color (0.5, 0.9, 0.5,
// 0.7)) with its laff meter (the species head in its head colour), "hp/max" under it and the
// chosen gag in the panel's blue square ("?" until then, or Pass / SOS / Fire). Ours wears a
// yellow edge; a toon that ran is greyed. The top screen's meter is hidden meanwhile.
void BattlePanel::drawToons(const world::BattleView& v, float x0, float w) {
    const int n = v.numToons < world::kBattleToonCards ? v.numToons : world::kBattleToonCards;
    if (n <= 0) return;
    constexpr float kGap = 2.0f, kMaxCardW = 66.0f;
    float cw = (w - (n - 1) * kGap) / n;
    if (cw > kMaxCardW) cw = kMaxCardW;
    const float ch = kBtnH;
    const bool art = invArtReady();
    // The blue square in ToonBtl_Status_BG, as fractions of the panel (measured on the art).
    constexpr float kSqX0 = 0.58f, kSqX1 = 0.89f, kSqY0 = 0.12f, kSqY1 = 0.51f;
    for (int i = 0; i < n; i++) {
        const world::BattleToonView& t = v.toons[i];
        const float x = x0 + i * (cw + kGap);
        const bool gone = t.running || !t.active;
        if (art) {
            invTintBegin();
            drawInvArt(InvArt::ToonStatus, x, kBtnY, cw, ch, 0.55f,
                       gone ? theme::rgb(0x90, 0x90, 0x90, 0xC0) : theme::rgb(0x80, 0xE6, 0x80, 0xFF));
            invTintEnd();
        } else {
            theme::paper(x, kBtnY, cw, ch, 5.0f, gone ? theme::kGrey : theme::kPaper, theme::kPaperEdge);
        }
        if (pulseId_[i] != t.doId) {
            pulseId_[i] = t.doId;
            pulse_[i] = LaffPulse{};
        }
        LaffFace face;
        face.hp = t.hp;
        face.maxHp = t.maxHp > 0 ? t.maxHp : (t.hp > 0 ? t.hp : 15);
        for (int k = 0; k < 3; k++) face.rgb[k] = t.rgb[k];
        face.species = t.species;
        // Stock TownBattleToonPanel puts the LaffMeter head in the upper half and health text below it.
        // Retain the real face with eyes unobstructed, and place the outlined hp/max text directly below.
        const float meter = 19.0f;
        const float s = meter * pulse_[i].update(t.hp);
        const float mx = x + cw * 0.33f, my = kBtnY + 11.5f;
        drawLaffMeter(face, mx - s * 0.5f, my - s * 0.5f, s, nullptr, nullptr, 0.58f);
        char num[24];
        snprintf(num, sizeof num, "%d/%d", t.hp > 0 ? t.hp : 0, face.maxHp);
        const bool low = t.maxHp > 0 && t.hp * 4 <= t.maxHp;
        C2D_Text hp;
        textCache_.parse(&hp, buf_, num, 1024);
        float hs = 0.34f, hw = 0, hh = 0;
        C2D_TextGetDimensions(&hp, hs, hs, &hw, &hh);
        const float maxW = cw * 0.56f;
        if (hw > maxW) {
            hs *= maxW / hw;
            C2D_TextGetDimensions(&hp, hs, hs, &hw, &hh);
        }
        drawOutlinedText(hp, mx - hw * 0.5f, kBtnY + 23.5f, 0.61f, hs,
                         gone ? theme::rgb(0xE0, 0xE0, 0xE0) : low ? theme::rgb(0xFF, 0xD8, 0xD0) : theme::kWhite,
                         gone ? kInkSoft : low ? theme::kRedDark : theme::rgb(0x16, 0x4A, 0x16));
        // TownBattleToonPanel.setValues while choosing: the chosen gag, Pass, SOS or Fire ("?" until then).
        const bool picking = !gone && (v.phase == world::BattlePhase::Input || v.phase == world::BattlePhase::Chosen);
        const char* pick = nullptr;
        if (picking && t.choice == world::BattleToonView::kChoiceNone) pick = "?";
        if (picking && t.choice == world::BattleToonView::kChoicePass) pick = "Pass";
        if (picking && t.choice == world::BattleToonView::kChoiceSos) pick = "SOS";
        if (picking && t.choice == world::BattleToonView::kChoiceFire) pick = "Fire";
        const bool gag = picking && t.choice == world::BattleToonView::kChoiceGag;
        const float sx = x + cw * kSqX0, sy = kBtnY + ch * kSqY0, sw = cw * (kSqX1 - kSqX0), sh = ch * (kSqY1 - kSqY0);
        if (gag) {
            const float icon = (sw < sh ? sw : sh) * 1.25f;
            world::GagAtlas::instance().drawIcon(t.chosenTrack, t.chosenLevel, sx + (sw - icon) * 0.5f,
                                                 sy + (sh - icon) * 0.5f, icon, 0.6f, 1.0f);
        } else if (pick) {
            C2D_Text p;
            textCache_.parse(&p, buf_, pick, 1024);
            float ps = pick[0] == '?' ? 0.5f : 0.3f, pw = 0, ph = 0;
            C2D_TextGetDimensions(&p, ps, ps, &pw, &ph);
            if (pw > sw + 6) {
                ps *= (sw + 6) / pw;
                C2D_TextGetDimensions(&p, ps, ps, &pw, &ph);
            }
            drawOutlinedText(p, sx + (sw - pw) * 0.5f, sy + (sh - ph) * 0.5f, 0.6f, ps, theme::kWhite,
                             theme::rgb(0x10, 0x30, 0x60));
        }
    }
}

} // namespace tt::ui
