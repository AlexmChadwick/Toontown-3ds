#include "game/front_end.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "audio/audio.h"
#include "core/log.h"
#include "game/name_check.h"
#include "game/loading_tips.h"
#include "game/name_tables.h"
#include "ui/laff_meter.h"
#include "ui/loading_atlas.h"
#include "ui/mat_room_tracks.h"
#include "ui/mat_art.h"
#include "ui/pick_a_toon_atlas.h"
#include "ui/theme.h"

namespace tt::game {

namespace {

static char sTestTypedName[64] = {};
static bool sHasTestTypedName = false;

constexpr float kW = 320.0f;
const u32 kBg = C2D_Color32(0x1E, 0x2A, 0x44, 0xFF);
const u32 kAccent = C2D_Color32(0xF5, 0xB8, 0x1A, 0xFF);
const u32 kWhite = C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF);
const u32 kDim = C2D_Color32(0xA8, 0xB4, 0xCC, 0xFF);
const u32 kRed = C2D_Color32(0xFF, 0x80, 0x80, 0xFF);
// Bottom screen: the shared Toontown theme (sky, paper card, cartoon buttons, ink text). The
// constants above stay for the top screen's title text over the 3D scene.
const u32 kBInk = ui::theme::kInk;
const u32 kBSoft = ui::theme::kInkSoft;
const u32 kBErr = ui::theme::kRedDark;
const u32 kBHi = ui::theme::kBlueDark;

const char* const kSpeciesLabel[9] = {"Dog", "Cat", "Horse", "Mouse", "Rabbit", "Duck", "Monkey", "Bear", "Pig"};
const char* const kSizeLabel[3] = {"Short", "Medium", "Tall"};
// Make-A-Toon's Clothes step: two shuffle frames (centre rows) with their arrows' hit boxes.
constexpr float kShirtY = 70, kBottomY = 148, kShuffleLX = 34, kShuffleRX = 226;
constexpr float kShuffleW = 136; // the frame, aspect kept
// ColorShop's frames (centre, size with the shuffle frame's aspect kept, upstream's tilt) down the
// left half, and the swatch grid for the touched frame's part on the right.
struct ColorFrame {
    float cx, cy, w, h, tilt;
};
constexpr ColorFrame kColorFrames[4] = {
    {84, 52, 116, 48.6f, 0.0f}, {84, 96, 86, 36.0f, 2.0f}, {84, 134, 86, 36.0f, -2.0f}, {84, 172, 86, 36.0f, 3.0f}};
constexpr float kSwatchX = 174, kSwatchY = 44, kSwatchStep = 23.5f;
constexpr int kSwatchCols = 6;
const u32 kFrameGlow = C2D_Color32(0xFF, 0xE0, 0x60, 0xFF);

// Immediate-mode layout: each screen function is called once to handle input and
// once to draw; buttons are numbered in call order for D-pad navigation.
struct Gui {
    bool drawing = false;
    int index = 0;    // next button number
    int count = 0;    // buttons seen last pass
    int cursor = 0;   // highlighted button (D-pad)
    bool activate = false; // A pressed this frame
    bool cursorShown = false;
};
Gui gGui;

} // namespace

void FrontEnd::setTestTypedName(const char* name) {
    if (name) {
        snprintf(sTestTypedName, sizeof(sTestTypedName), "%s", name);
        sHasTestTypedName = true;
    } else {
        sTestTypedName[0] = '\0';
        sHasTestTypedName = false;
    }
}

const char* FrontEnd::testTypedName() {
    return sHasTestTypedName ? sTestTypedName : nullptr;
}

void FrontEnd::updateConstructedName() {
    if (nameIsTyped_) {
        snprintf(currentName_, sizeof(currentName_), "%s", typedName_);
        return;
    }
    const bool male = choice_.gender == world::Gender::Male;
    int16_t p1 = titleActive_ ? getTitleId(male, titleIndex_) : -1;
    uint8_t f1 = (p1 != -1 && !strcmp(getNameString(p1), "von")) ? 0 : 1;
    int16_t p2 = firstActive_ ? getFirstId(male, firstIndex_) : -1;
    uint8_t f2 = 1;
    int16_t p3 = lastActive_ ? getPrefixId(prefixIndex_) : -1;
    uint8_t f3 = 1;
    int16_t p4 = lastActive_ ? getSuffixId(suffixIndex_) : -1;
    uint8_t f4 = (p3 != -1 && isCapPrefix(p3)) ? 1 : 0;
    buildPatternName(p1, f1, p2, f2, p3, f3, p4, f4, currentName_, sizeof(currentName_));
}

void FrontEnd::randomizePatternName() {
    nameIsTyped_ = false;
    nameError_[0] = '\0';
    const bool male = choice_.gender == world::Gender::Male;
    int16_t p[4] = {-1, -1, -1, -1};
    uint8_t f[4] = {0, 0, 0, 0};
    randomPattern(male, p, f);
    titleActive_ = (p[0] != -1);
    if (titleActive_) titleIndex_ = findTitleIndex(male, p[0]);
    else titleIndex_ = 0;
    if (titleIndex_ < 0) titleIndex_ = 0;

    firstActive_ = (p[1] != -1);
    if (firstActive_) firstIndex_ = findFirstIndex(male, p[1]);
    else firstIndex_ = 0;
    if (firstIndex_ < 0) firstIndex_ = 0;

    lastActive_ = (p[2] != -1 && p[3] != -1);
    if (lastActive_) {
        prefixIndex_ = findPrefixIndex(p[2]);
        suffixIndex_ = findSuffixIndex(p[3]);
    } else {
        prefixIndex_ = 0;
        suffixIndex_ = 0;
    }
    if (prefixIndex_ < 0) prefixIndex_ = 0;
    if (suffixIndex_ < 0) suffixIndex_ = 0;

    buildPatternName(p[0], f[0], p[1], f[1], p[2], f[2], p[3], f[3], currentName_, sizeof(currentName_));
}

bool FrontEnd::init(world::ToonCache* cache) {
    cache_ = cache;
    textCache_.clear();
    textBuf_ = C2D_TextBufNew(4096);
    if (!logo_.loaded() && !logo_.load("romfs:/baked/title_logo.ttm")) TT_LOG("frontend: no title_logo.ttm");
    if (!loadingBg_.loaded() && !loadingBg_.load("romfs:/baked/loading_bg.ttm")) TT_LOG("frontend: no loading_bg.ttm");
    setPreviewDna(world::defaultToonDna());
    preview_.play(world::kAnimWave, 1.0f);
    reset();
    return textBuf_ != nullptr;
}

void FrontEnd::setServer(const char* host, int port, bool online) {
    if (online) snprintf(serverLine_, sizeof(serverLine_), "Server: %s:%d (server.txt)", host, port);
    else snprintf(serverLine_, sizeof(serverLine_), "Online play is off in server.txt");
}

void FrontEnd::shutdown() {
    if (cache_) cache_->release(&previewLook_);
    if (textBuf_) C2D_TextBufDelete(textBuf_);
    textBuf_ = nullptr;
    logo_.unload();
    loadingBg_.unload();
    loadingGui_.unload();
    releaseRoom();
    pickArt_.unload();
    ui::releaseMatArt();
}

void FrontEnd::releaseArt() {
    releaseRoom();
    for (int& id : crashSfx_) {
        if (id >= 0) audio::freeSfx(id);
        id = -1;
    }
    crashTried_ = false;
    loadingGui_.unload();
    loadingGuiTried_ = false;
    pickArt_.unload();
    pickArtTried_ = false;
    ui::releaseMatArt();
}

void FrontEnd::reset() {
    confirmDelete_ = false;
    nameIsTyped_ = false;
    typedName_[0] = '\0';
    nameError_[0] = '\0';
    go(Screen::Title);
    setPreviewDna(world::defaultToonDna());
}

void FrontEnd::go(Screen s) {
    if ((s == Screen::Connecting || s == Screen::Entering) && s != screen_)
        // ToontownLoadingScreen.getTip: random.choice, but never the tip the last screen showed.
        tip_ = int((u64(tip_) + 1 + osGetTime() % u64(kLoadingTipCount - 1)) % u64(kLoadingTipCount));
    screen_ = s;
    cursor_ = 0;
    gGui.cursor = 0;
    gGui.cursorShown = false;
}

void FrontEnd::setPreviewDna(const world::ToonDna& dna) {
    if (!cache_) return;
    cache_->acquire(dna, world::ToonLod::High, &previewLook_);
    preview_.setLook(&previewLook_, &cache_->fallback());
}

bool FrontEnd::tapped(const Button& b) const {
    return touchReleased_ && lastTouch_.px >= b.x && lastTouch_.px < b.x + b.w && lastTouch_.py >= b.y &&
           lastTouch_.py < b.y + b.h;
}

void FrontEnd::text(float x, float y, float scale, u32 color, const char* fmt, ...) {
    char buf[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    C2D_Text t;
    textCache_.parse(&t, textBuf_, buf, 4096);
    C2D_DrawText(&t, C2D_WithColor | C2D_WordWrap, x, y, 0.5f, scale, scale, color, kW - x - 8.0f);
}

void FrontEnd::textCentered(const Button& b, float scale, u32 color, const char* s) {
    C2D_Text t;
    textCache_.parse(&t, textBuf_, s, 4096);
    float w, h;
    C2D_TextGetDimensions(&t, scale, scale, &w, &h);
    if (w > b.w && w > 0.0f) { // never spill past the box (or the screen edge): shrink to fit
        scale *= b.w / w;
        C2D_TextGetDimensions(&t, scale, scale, &w, &h);
    }
    C2D_DrawText(&t, C2D_WithColor, b.x + (b.w - w) * 0.5f, b.y + (b.h - h) * 0.5f, 0.5f, scale, scale, color);
}

// A centred, word-wrapped line inside the waiting / error dialog (drawBottom).
void FrontEnd::dialogText(float y, float scale, u32 color, const char* s) {
    if (!s || !s[0]) return;
    C2D_Text t;
    textCache_.parse(&t, textBuf_, s, 4096);
    C2D_DrawText(&t, C2D_WithColor | C2D_AlignCenter | C2D_WordWrap, kW * 0.5f, y, 0.5f, scale, scale, color, kW - 72.0f);
}

// Make-A-Toon's own buttons (tt_m_gui_mat_mainGui, ui/mat_art): MakeAToon's round next / back
// (the next image mirrored) and OK buttons beside their caption, GenderShop's boy / girl buttons
// and the shuffle frames' gold arrows. Returns false for labels drawn as ordinary pills.
bool FrontEnd::matButton(const Button& b, const char* label, bool selected, bool enabled, bool focused) {
    namespace th = ui::theme;
    using ui::MatArt;
    const bool left = !strcmp(label, "<"), right = !strcmp(label, ">");
    if (left || right) { // ShuffleButton arrows: gold, darker pressed, grey when there is no choice
        const float h = b.h < 48 ? b.h : 48, w = h * ui::matArtAspect(MatArt::ArrowUp);
        const float x = b.x + (b.w - w) * 0.5f, y = b.y + (b.h - h) * 0.5f;
        if (focused) th::roundRing(x - 4, y - 3, w + 8, h + 6, 6, 2, th::kRed, 0.5f);
        const MatArt art = !enabled ? MatArt::ArrowDisabled : touching_ && inside(b) ? MatArt::ArrowDown : MatArt::ArrowUp;
        return ui::drawMatArt(art, x, y, w, h, 0.52f, right); // the art points left; > is mirrored
    }
    const bool next = !strcmp(label, "Next"), ok = !strcmp(label, "Create!") || !strcmp(label, "Done");
    const bool back = !strcmp(label, "Back");
    if (next || ok || back) { // a round button at the outer end, the caption inboard of it
        const float d = b.h + 6, y = b.y - 3;
        const float x = back ? b.x : b.x + b.w - d;
        const bool down = touching_ && inside(b);
        const MatArt art = ok ? (down ? MatArt::OkDown : MatArt::OkUp) : (down ? MatArt::NextDown : MatArt::NextUp);
        if (focused) th::roundRing(b.x - 3, b.y - 5, b.w + 6, b.h + 10, 12, 2, th::kRed, 0.5f);
        if (!ui::drawMatArt(art, x, y, d, d, 0.52f, back)) return false;
        const Button cap{back ? b.x + d + 2 : b.x, b.y, b.w - d - 2, b.h};
        textCentered(cap, 0.6f, th::kInk, label);
        return true;
    }
    if (!strcmp(label, "Shuffle")) { // NameShop's shuffle bar (tt_t_gui_mat_shuffleUp / Down)
        const float h = b.w / ui::matArtAspect(MatArt::ShuffleUp) < b.h ? b.w / ui::matArtAspect(MatArt::ShuffleUp) : b.h;
        const float w = h * ui::matArtAspect(MatArt::ShuffleUp), x = b.x + (b.w - w) * 0.5f, y = b.y + (b.h - h) * 0.5f;
        if (focused) th::roundRing(x - 3, y - 3, w + 6, h + 6, 8, 2, th::kRed, 0.5f);
        // z 0.5: the caption (drawn at 0.5 after it) must pass the depth test over the bar.
        if (!ui::drawMatArt(touching_ && inside(b) ? MatArt::ShuffleDown : MatArt::ShuffleUp, x, y, w, h, 0.5f)) return false;
        textCentered(Button{x, y, w, h}, 0.56f, kWhite, label);
        return true;
    }
    if (step_ == MakeStep::Species) { // the species' laff-meter head in our body colour on its pill
        int species = -1;
        for (int i = 0; i < 9; i++)
            if (!strcmp(label, kSpeciesLabel[i])) species = i;
        if (species < 0) return false;
        if (focused) th::roundRect(b.x - 3, b.y - 3, b.w + 6, b.h + 6, 9, th::kRed);
        const th::Pill style = selected ? th::Pill::Cyan : th::Pill::Yellow;
        th::pill(b.x, b.y, b.w, b.h, style);
        ui::LaffFace face;
        face.hp = face.maxHp = 1;
        face.species = species;
        const float* c = world::bodyColor(makeDna_.headColor);
        for (int k = 0; k < 3; k++) face.rgb[k] = c[k];
        const float d = b.h - 6;
        ui::drawLaffMeter(face, b.x + 9, b.y + 2, d, nullptr, nullptr, 0.52f);
        textCentered(Button{b.x + 9 + d, b.y + 1, b.w - d - 16, b.h}, 0.52f, th::pillInk(style), label);
        return true;
    }
    const bool boy = !strcmp(label, "Boy"), girl = !strcmp(label, "Girl");
    if (boy || girl) { // GenderShop: the chosen one bright, the other dimmed
        const float d = b.h + 6, y = b.y - 3;
        const MatArt art = boy ? (selected ? MatArt::BoyUp : MatArt::BoyDown) : (selected ? MatArt::GirlUp : MatArt::GirlDown);
        if (selected) th::roundRect(b.x - 2, b.y - 4, b.w + 4, b.h + 8, 12, th::kYellow, 0.5f);
        if (focused) th::roundRing(b.x - 3, b.y - 5, b.w + 6, b.h + 10, 12, 2, th::kRed, 0.51f);
        if (!ui::drawMatArt(art, b.x, y, d, d, 0.52f)) return false;
        textCentered(Button{b.x + d, b.y, b.w - d, b.h}, 0.58f, th::kInk, label);
        return true;
    }
    return false;
}

bool FrontEnd::inside(const Button& b) const {
    return lastTouch_.px >= b.x && lastTouch_.px < b.x + b.w && lastTouch_.py >= b.y && lastTouch_.py < b.y + b.h;
}

void FrontEnd::button(const Button& b, const char* label, bool selected, bool enabled) {
    const bool focused = gGui.cursorShown && gGui.index - 1 == gGui.cursor;
    namespace th = ui::theme;
    if (screen_ == Screen::Make && matButton(b, label, selected, enabled, focused)) return;
    if (focused) th::roundRect(b.x - 3, b.y - 3, b.w + 6, b.h + 6, 9, th::kRed); // the D-Pad cursor
    // Deleting a toon is the one destructive action here: red, like a close button.
    const bool danger = enabled && !strcmp(label, "Delete");
    // Toontown's quit_button pill: yellow at rest, the cyan rollover face for the current choice.
    const th::Pill style = !enabled ? th::Pill::Grey : danger ? th::Pill::Red : selected ? th::Pill::Cyan : th::Pill::Yellow;
    th::pill(b.x, b.y, b.w, b.h, style);
    const float end = b.h * 0.3f; // keep the label off the round ends
    textCentered(Button{b.x + end, b.y + 1, b.w - 2 * end, b.h}, 0.55f, th::pillInk(style), label);
}

// A check box at the left end of a toggle button (draw pass only): white box, red tick when on.
void FrontEnd::checkBox(const Button& b, bool on) {
    if (!gGui.drawing) return;
    namespace th = ui::theme;
    const float x = b.x + 6, y = b.y + (b.h - 3) * 0.5f - 6;
    th::roundRect(x, y, 12, 12, 3, th::kInk);
    th::roundRect(x + 1, y + 1, 10, 10, 2, th::kWhite);
    if (on) {
        C2D_DrawLine(x + 2.5f, y + 6, th::kRed, x + 5, y + 9, th::kRed, 2.2f, 0.6f);
        C2D_DrawLine(x + 5, y + 9, th::kRed, x + 10, y + 2.5f, th::kRed, 2.2f, 0.6f);
    }
}

// One button: draws in the draw pass, reports a press (tap or A on the cursor) otherwise.
#define TT_BUTTON(rect, label, selected, enabled)                                                      \
    [&]() -> bool {                                                                                     \
        const Button tt_b = rect;                                                                       \
        gGui.index++;                                                                                   \
        if (gGui.drawing) {                                                                             \
            button(tt_b, label, selected, enabled);                                                     \
            return false;                                                                               \
        }                                                                                               \
        if (!(enabled)) return false;                                                                   \
        return tapped(tt_b) || (gGui.activate && gGui.cursorShown && gGui.index - 1 == gGui.cursor);  \
    }()

void FrontEnd::rebuildMakeDna() {
    world::ToonDna dna;
    if (world::buildMakeAToon(choice_, &dna)) {
        makeDna_ = dna;
        setPreviewDna(makeDna_);
    }
}

void FrontEnd::updatePickPreview() {
    if (!net_) return;
    const net::PotentialAvatar* pa = net_->avatarInSlot(static_cast<uint8_t>(pickSlot_));
    world::ToonDna dna;
    if (pa && pa->hasDna && world::parseNetString(pa->dna, net::kDnaSize, &dna)) {
        setPreviewDna(dna);
    } else {
        setPreviewDna(world::defaultToonDna());
    }
}

Action FrontEnd::update(u32 down, u32 held, const touchPosition& touch, float dt) {
    time_ += dt;
    previewSpin_ += dt * 25.0f;
    preview_.update(dt);
    // MakeAToon's rotate buttons (rotateToonLeft / Right: hprDelta 1 degree a frame at 60 Hz) on L / R;
    // each shop starts the Toon at that shop's heading.
    if (screen_ == Screen::Make) {
        if (turnStep_ != step_) {
            turnStep_ = step_;
            makeTurn_ = 0.0f;
        }
        if (held & KEY_L) makeTurn_ -= 60.0f * dt;
        if (held & KEY_R) makeTurn_ += 60.0f * dt;
        // A room load stalls the frame it happens in; cap the step so the drop is not skipped.
        roomAnimT_ += dt < 1.0f / 30.0f ? dt : 1.0f / 30.0f;
    } else {
        turnStep_ = MakeStep::Count;
    }
    // A tap fires when the stylus lifts, at the last touched point.
    touchReleased_ = touching_ && !(held & KEY_TOUCH);
    if (held & KEY_TOUCH) lastTouch_ = touch;
    touching_ = held & KEY_TOUCH;

    const bool pickOwnsPad = screen_ == Screen::Pick && !confirmDelete_;
    if (!pickOwnsPad && (down & (KEY_DUP | KEY_DDOWN | KEY_DLEFT | KEY_DRIGHT))) {
        if (!gGui.cursorShown) {
            gGui.cursorShown = true;
        } else if (gGui.count > 0) {
            const int delta = (down & (KEY_DUP | KEY_DLEFT)) ? -1 : 1;
            gGui.cursor = (gGui.cursor + delta + gGui.count) % gGui.count;
        }
    }
    if (down & KEY_TOUCH) gGui.cursorShown = false;
    gGui.activate = down & KEY_A;
    const bool back = down & KEY_B;

    gGui.drawing = false;
    gGui.index = 0;
    Action action = Action::None;

    switch (screen_) {
    case Screen::Title:
        if (TT_BUTTON((Button{40, 70, 240, 48}), "Play online", true, true)) {
            go(Screen::Connecting);
            action = Action::StartOnline;
        }
        if (TT_BUTTON((Button{40, 130, 240, 40}), "Play offline", false, true)) action = Action::StartOffline;
        break;

    case Screen::Connecting:
        if (TT_BUTTON((Button{105, 196, 110, 34}), "Back", false, true) || back) {
            if (net_) net_->disconnect();
            go(Screen::Title);
            break;
        }
        if (net_ && net_->fatal()) {
            snprintf(error_, sizeof(error_), "%s", net_->lastError());
            go(Screen::Error);
        } else if (net_ && net_->avatarListReady()) {
            pickSlot_ = 0;
            updatePickPreview();
            go(Screen::Pick);
        } else if (net_ && net_->inZone()) {
            action = Action::EnterWorld; // reconnect path: the client re-played our toon
        }
        break;

    case Screen::Pick: {
        if (!net_ || !net_->avatarListReady()) {
            go(Screen::Connecting);
            break;
        }
        if (confirmDelete_) {
            const net::PotentialAvatar* sel = net_->avatarInSlot(static_cast<uint8_t>(pickSlot_));
            if (TT_BUTTON((Button{35, 140, 110, 34}), "Cancel", false, true) || back) {
                confirmDelete_ = false;
                break;
            }
            if (TT_BUTTON((Button{175, 140, 110, 34}), "Delete", true, true)) {
                if (sel) {
                    net_->sendRequestRemoveAvatar(sel->id);
                }
                confirmDelete_ = false;
                break;
            }
            break;
        }
        pickInput(down, back);
        break;
    }

    case Screen::Make:
        makeStepInput(down);
        if (screen_ != Screen::Make) break;
        if (TT_BUTTON((Button{10, 200, 110, 34}), "Back", false, true) || back) {
            if (renameAvId_) { // naming an existing toon: back to Pick-a-Toon
                renameAvId_ = 0;
                updatePickPreview();
                go(Screen::Pick);
            } else if (step_ == MakeStep::Species) {
                updatePickPreview();
                go(Screen::Pick);
            } else {
                step_ = static_cast<MakeStep>(static_cast<int>(step_) - 1);
                go(Screen::Make);
            }
            break;
        }
        if (step_ != MakeStep::Name) {
            if (TT_BUTTON((Button{200, 200, 110, 34}), "Next", true, true)) {
                step_ = static_cast<MakeStep>(static_cast<int>(step_) + 1);
                go(Screen::Make);
            }
        } else if (renameAvId_) {
            if (TT_BUTTON((Button{200, 200, 110, 34}), "Done", true, true)) {
                if (sendRename()) {
                    renameAvId_ = 0;
                    updatePickPreview();
                    go(Screen::Pick);
                } else {
                    snprintf(nameError_, sizeof(nameError_), "Couldn't send the name. Try again.");
                }
            }
        } else if (TT_BUTTON((Button{200, 200, 110, 34}), "Create!", true, true)) {
            // TutorialManager only accepts the choice after the avatar exists, but before we
            // enter a place.  Keep it on its own screen so the player has the same explicit
            // choice as ToontownClientRepository.enterTutorialQuestion.
            go(Screen::TutorialChoice);
        }
        break;

    case Screen::TutorialChoice:
        if (TT_BUTTON((Button{20, 128, 130, 44}), "Play it", true, true)) {
            if (createToon(net::Client::TutorialChoice::Play)) go(Screen::Entering);
            else {
                snprintf(error_, sizeof(error_), "That Toon could not be created. Try different choices.");
                go(Screen::Error);
            }
        }
        if (TT_BUTTON((Button{170, 128, 130, 44}), "Skip", false, true) || back) {
            if (createToon(net::Client::TutorialChoice::Skip)) go(Screen::Entering);
            else {
                snprintf(error_, sizeof(error_), "That Toon could not be created. Try different choices.");
                go(Screen::Error);
            }
        }
        break;

    case Screen::Entering:
        if (net_ && net_->inZone()) {
            action = Action::EnterWorld;
        } else if (net_ && net_->fatal()) {
            snprintf(error_, sizeof(error_), "%s", net_->lastError());
            go(Screen::Error);
        }
        break;

    case Screen::Error:
        if (TT_BUTTON((Button{100, 190, 120, 38}), "OK", true, true) || back || gGui.activate) {
            if (net_) net_->disconnect();
            reset();
        }
        break;
    }
    gGui.count = gGui.index;
    if (gGui.count > 0 && gGui.cursor >= gGui.count) gGui.cursor = gGui.count - 1;
    return action;
}

// AvatarChoice's "Name Your Toon!" path: the same name messages Make-A-Toon sends after
// createAvatar, for an avatar that already exists (the server answers with namePatternAnswer /
// nameTypedResponse and the slot's name state follows).
bool FrontEnd::sendRename() {
    if (!net_ || !renameAvId_) return false;
    if (nameIsTyped_) {
        net_->setPendingTypedName(typedName_);
        return net_->sendSetNameTyped(renameAvId_, typedName_);
    }
    const bool male = choice_.gender == world::Gender::Male;
    const int16_t p1 = titleActive_ ? getTitleId(male, titleIndex_) : -1;
    const uint8_t f1 = (p1 != -1 && !strcmp(getNameString(p1), "von")) ? 0 : 1;
    const int16_t p2 = firstActive_ ? getFirstId(male, firstIndex_) : -1;
    const int16_t p3 = lastActive_ ? getPrefixId(prefixIndex_) : -1;
    const int16_t p4 = lastActive_ ? getSuffixId(suffixIndex_) : -1;
    const uint8_t f4 = (p3 != -1 && isCapPrefix(p3)) ? 1 : 0;
    net_->setPendingPatternName(p1, f1, p2, 1, p3, 1, p4, f4);
    return net_->sendSetNamePattern(renameAvId_, p1, f1, p2, 1, p3, 1, p4, f4);
}

bool FrontEnd::createToon(net::Client::TutorialChoice tutorial) {
    if (!net_) return false;
    if (nameIsTyped_) {
        net_->setPendingTypedName(typedName_);
    } else {
        const bool male = choice_.gender == world::Gender::Male;
        const int16_t p1 = titleActive_ ? getTitleId(male, titleIndex_) : -1;
        const uint8_t f1 = (p1 != -1 && !strcmp(getNameString(p1), "von")) ? 0 : 1;
        const int16_t p2 = firstActive_ ? getFirstId(male, firstIndex_) : -1;
        const int16_t p3 = lastActive_ ? getPrefixId(prefixIndex_) : -1;
        const int16_t p4 = lastActive_ ? getSuffixId(suffixIndex_) : -1;
        const uint8_t f4 = (p3 != -1 && isCapPrefix(p3)) ? 1 : 0;
        net_->setPendingPatternName(p1, f1, p2, 1, p3, 1, p4, f4);
    }
    uint8_t net15[world::kDnaNetStringSize];
    world::makeNetString(makeDna_, net15);
    net_->setTutorialChoice(tutorial);
    return net_->createAvatar(net15, static_cast<uint8_t>(pickSlot_));
}

int FrontEnd::shuffleRow(int row, const char* name, bool glow, bool enabled) {
    const ColorFrame& f = kColorFrames[row];
    const Button frame{f.cx - f.w * 0.5f, f.cy - f.h * 0.5f, f.w, f.h};
    int result = 0;
    if (gGui.drawing) {
        if (glow) ui::theme::roundRect(frame.x - 4, frame.y - 3, frame.w + 8, frame.h + 6, 12, kFrameGlow);
        if (!ui::drawMatArtRotated(ui::MatArt::ShuffleFrame, f.cx, f.cy, f.w, f.h, f.tilt))
            ui::theme::paper(frame.x, frame.y, frame.w, frame.h, 8.0f);
        textCentered(Button{frame.x + 14, frame.y + 1, frame.w - 28, frame.h}, row == 0 ? 0.56f : 0.5f, kWhite, name);
    } else if (tapped(frame)) {
        result = 2;
    }
    const float ah = f.h * 0.82f, aw = ah * 0.6f; // the arrows' touch boxes at the frame's ends
    if (TT_BUTTON((Button{f.cx - f.w * 0.5f - aw * 0.75f, f.cy - ah * 0.5f, aw, ah}), "<", false, enabled)) result = -1;
    if (TT_BUTTON((Button{f.cx + f.w * 0.5f - aw * 0.25f, f.cy - ah * 0.5f, aw, ah}), ">", false, enabled)) result = 1;
    return result;
}

void FrontEnd::makeStepInput(u32 down) {
    (void)down;
    using world::Gender;
    using world::Species;
    switch (step_) {
    case MakeStep::Species:
        for (int i = 0; i < 9; i++) {
            const Button b{10.0f + (i % 3) * 102.0f, 28.0f + (i / 3) * 52.0f, 96.0f, 46.0f};
            if (TT_BUTTON(b, kSpeciesLabel[i], static_cast<int>(choice_.species) == i, true)) {
                choice_.species = static_cast<Species>(i);
                choice_.headVariant = 0;
                rebuildMakeDna();
            }
        }
        break;
    case MakeStep::Body: {
        // BodyShop: the Species frame (scale 1.3) and the Head / Body / Legs frames (0.9, tilted),
        // each stepping its choice with the gold arrows; GenderShop's boy / girl buttons beside them.
        int r = shuffleRow(0, kSpeciesLabel[static_cast<int>(choice_.species)], false, true);
        if (r == 1 || r == -1) {
            choice_.species = static_cast<Species>((static_cast<int>(choice_.species) + r + 9) % 9);
            choice_.headVariant = 0;
            rebuildMakeDna();
        }
        const u8 heads = world::headVariantCount(choice_.species);
        r = shuffleRow(1, "Head", false, heads > 1); // BodyShopHead
        if ((r == 1 || r == -1) && heads > 1) {
            choice_.headVariant = static_cast<u8>((choice_.headVariant + r + heads) % heads);
            rebuildMakeDna();
        }
        r = shuffleRow(2, "Body", false, true); // BodyShopBody: the torso size
        if (r == 1 || r == -1) {
            choice_.torsoSize = static_cast<u8>((choice_.torsoSize + r + 3) % 3);
            rebuildMakeDna();
        }
        r = shuffleRow(3, "Legs", false, true); // BodyShopLegs
        if (r == 1 || r == -1) {
            choice_.legSize = static_cast<u8>((choice_.legSize + r + 3) % 3);
            rebuildMakeDna();
        }
        for (int g = 0; g < 2; g++) {
            const Gender gender = g == 0 ? Gender::Male : Gender::Female;
            if (TT_BUTTON((Button{kSwatchX - 2 + g * 72.0f, 34, 68, 34}), g == 0 ? "Boy" : "Girl",
                          choice_.gender == gender, true)) {
                if (choice_.gender != gender) {
                    choice_.gender = gender;
                    // Colour and clothing lists differ by gender: start from their first entries.
                    choice_.headColor = choice_.armColor = choice_.legColor = world::matColor(gender, 0);
                    choice_.shirt = choice_.bottom = 0;
                    randomizePatternName();
                }
                rebuildMakeDna();
            }
        }
        break;
    }
    case MakeStep::Color: {
        // ColorShop: the Toon Color frame (all three parts, scale 1.3) and the Head / Body / Legs
        // frames (scale 0.9, tilted 2 / -2 / 3 degrees), each a tt_t_gui_mat_shuffleFrame whose gold
        // arrows step that part through the gender's colours. Touching a frame (or its arrows) also
        // points the swatch grid at that part, for picking a colour outright.
        const u8 n = world::matColorCount(choice_.gender);
        auto partColor = [&](int t) { return t == 2 ? choice_.armColor : t == 3 ? choice_.legColor : choice_.headColor; };
        auto paint = [&](int t, u8 c) {
            if (t == 0 || t == 1) choice_.headColor = c;
            if (t == 0 || t == 2) choice_.armColor = c;
            if (t == 0 || t == 3) choice_.legColor = c;
            rebuildMakeDna();
        };
        auto shuffle = [&](int t, int d) { // ColorShop.__swap*Color: the next / previous list colour
            int at = 0;
            for (u8 i = 0; i < n; i++)
                if (world::matColor(choice_.gender, i) == partColor(t)) at = i;
            colorTarget_ = t;
            paint(t, world::matColor(choice_.gender, u8((at + d + n) % n)));
        };
        static const char* const kNames[4] = {"Toon Color", "Head", "Body", "Legs"}; // ColorShopToon / Head / Body / Legs
        for (int t = 0; t < 4; t++) {
            const int r = shuffleRow(t, kNames[t], colorTarget_ == t, n > 1);
            if (r == 2) colorTarget_ = t;
            else if (r) shuffle(t, r);
        }
        for (u8 i = 0; i < n; i++) {
            const u8 c = world::matColor(choice_.gender, i);
            const Button b{kSwatchX + (i % kSwatchCols) * kSwatchStep, kSwatchY + (i / kSwatchCols) * kSwatchStep,
                           kSwatchStep - 3, kSwatchStep - 3};
            gGui.index++;
            if (gGui.drawing) {
                const float* rgb = world::bodyColor(c);
                const bool sel = partColor(colorTarget_) == c;
                const bool focused = gGui.cursorShown && gGui.index - 1 == gGui.cursor;
                if (focused || sel)
                    ui::theme::roundRect(b.x - 3, b.y - 3, b.w + 6, b.h + 6, 7, focused ? ui::theme::kRed : ui::theme::kInk);
                ui::theme::roundRect(b.x, b.y, b.w, b.h, 5, ui::theme::kPaperEdge);
                ui::theme::roundRect(b.x + 1, b.y + 1, b.w - 2, b.h - 2, 4, C2D_Color32f(rgb[0], rgb[1], rgb[2], 1.0f));
            } else if (tapped(b) || (gGui.activate && gGui.cursorShown && gGui.index - 1 == gGui.cursor)) {
                paint(colorTarget_, c);
            }
        }
        break;
    }
    case MakeStep::Clothes: {
        const u8 shirts = world::matShirtCount(choice_.gender);
        const u8 bottoms = world::matBottomCount(choice_.gender);
        if (TT_BUTTON((Button{kShuffleLX, kShirtY - 24, 60, 48}), "<", false, shirts > 1)) {
            choice_.shirt = static_cast<u8>((choice_.shirt + shirts - 1) % shirts);
            rebuildMakeDna();
        }
        if (TT_BUTTON((Button{kShuffleRX, kShirtY - 24, 60, 48}), ">", false, shirts > 1)) {
            choice_.shirt = static_cast<u8>((choice_.shirt + 1) % shirts);
            rebuildMakeDna();
        }
        if (TT_BUTTON((Button{kShuffleLX, kBottomY - 24, 60, 48}), "<", false, bottoms > 1)) {
            choice_.bottom = static_cast<u8>((choice_.bottom + bottoms - 1) % bottoms);
            rebuildMakeDna();
        }
        if (TT_BUTTON((Button{kShuffleRX, kBottomY - 24, 60, 48}), ">", false, bottoms > 1)) {
            choice_.bottom = static_cast<u8>((choice_.bottom + 1) % bottoms);
            rebuildMakeDna();
        }
        break;
    }
    case MakeStep::Name: {
        const bool male = choice_.gender == world::Gender::Male;
        // Toggle buttons:
        // Upstream's Pick-A-Name check boxes: Title, First and Last (one box for the two last-name
        // columns, which are always used together).
        constexpr Button kTitleBox{6, 56, 72, 22}, kFirstBox{82, 56, 72, 22}, kLastBox{158, 56, 156, 22};
        if (TT_BUTTON(kTitleBox, "   Title", titleActive_, true)) {
            titleActive_ = !titleActive_;
            nameIsTyped_ = false;
            updateConstructedName();
        }
        checkBox(kTitleBox, titleActive_);
        if (TT_BUTTON(kFirstBox, "   First", firstActive_, true)) {
            if (!firstActive_ || lastActive_) {
                firstActive_ = !firstActive_;
                nameIsTyped_ = false;
                updateConstructedName();
            }
        }
        checkBox(kFirstBox, firstActive_);
        if (TT_BUTTON(kLastBox, "   Last name", lastActive_, true)) {
            if (!lastActive_ || firstActive_) {
                lastActive_ = !lastActive_;
                nameIsTyped_ = false;
                updateConstructedName();
            }
        }
        checkBox(kLastBox, lastActive_);
        // Cycle buttons:
        const int numTitles = getTitleCount(male);
        if (TT_BUTTON((Button{6, 106, 34, 26}), "<", false, numTitles > 1)) {
            titleIndex_ = (titleIndex_ + numTitles - 1) % numTitles;
            titleActive_ = true;
            nameIsTyped_ = false;
            updateConstructedName();
        }
        if (TT_BUTTON((Button{44, 106, 34, 26}), ">", false, numTitles > 1)) {
            titleIndex_ = (titleIndex_ + 1) % numTitles;
            titleActive_ = true;
            nameIsTyped_ = false;
            updateConstructedName();
        }
        const int numFirst = getFirstCount(male);
        if (TT_BUTTON((Button{82, 106, 34, 26}), "<", false, numFirst > 1)) {
            firstIndex_ = (firstIndex_ + numFirst - 1) % numFirst;
            firstActive_ = true;
            nameIsTyped_ = false;
            updateConstructedName();
        }
        if (TT_BUTTON((Button{120, 106, 34, 26}), ">", false, numFirst > 1)) {
            firstIndex_ = (firstIndex_ + 1) % numFirst;
            firstActive_ = true;
            nameIsTyped_ = false;
            updateConstructedName();
        }
        const int numPrefix = getPrefixCount();
        if (TT_BUTTON((Button{158, 106, 36, 26}), "<", false, numPrefix > 1)) {
            prefixIndex_ = (prefixIndex_ + numPrefix - 1) % numPrefix;
            lastActive_ = true;
            nameIsTyped_ = false;
            updateConstructedName();
        }
        if (TT_BUTTON((Button{198, 106, 36, 26}), ">", false, numPrefix > 1)) {
            prefixIndex_ = (prefixIndex_ + 1) % numPrefix;
            lastActive_ = true;
            nameIsTyped_ = false;
            updateConstructedName();
        }
        const int numSuffix = getSuffixCount();
        if (TT_BUTTON((Button{238, 106, 36, 26}), "<", false, numSuffix > 1)) {
            suffixIndex_ = (suffixIndex_ + numSuffix - 1) % numSuffix;
            lastActive_ = true;
            nameIsTyped_ = false;
            updateConstructedName();
        }
        if (TT_BUTTON((Button{278, 106, 36, 26}), ">", false, numSuffix > 1)) {
            suffixIndex_ = (suffixIndex_ + 1) % numSuffix;
            lastActive_ = true;
            nameIsTyped_ = false;
            updateConstructedName();
        }
        // Action buttons:
        if (TT_BUTTON((Button{16, 148, 90, 32}), "Shuffle", false, true)) {
            randomizePatternName();
        }
        if (TT_BUTTON((Button{116, 148, 100, 32}), "Type Name", nameIsTyped_, true)) {
            char inputBuf[64] = {};
            bool gotInput = false;
            const char* testName = testTypedName();
            if (testName) {
                snprintf(inputBuf, sizeof(inputBuf), "%s", testName);
                setTestTypedName(nullptr);
                gotInput = true;
            } else {
                SwkbdState swkbd;
                swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, sizeof(inputBuf) - 1);
                swkbdSetHintText(&swkbd, "Enter your Toon's name");
                SwkbdButton btn = swkbdInputText(&swkbd, inputBuf, sizeof(inputBuf));
                if (btn == SWKBD_BUTTON_RIGHT) gotInput = true;
            }
            if (gotInput) {
                char badChar = 0;
                NameCheckResult res = checkName(inputBuf, &badChar);
                if (res == NameCheckResult::Ok) {
                    nameError_[0] = '\0';
                    nameIsTyped_ = true;
                    snprintf(typedName_, sizeof(typedName_), "%s", inputBuf);
                    updateConstructedName();
                } else {
                    getNameCheckErrorMessage(res, badChar, nameError_, sizeof(nameError_));
                }
            }
        }
        if (nameIsTyped_) {
            if (TT_BUTTON((Button{226, 148, 85, 32}), "Pick Name", false, true)) {
                nameIsTyped_ = false;
                nameError_[0] = '\0';
                updateConstructedName();
            }
        }
        break;
    }
    default:
        break;
    }
}

namespace {

// Make-A-Toon's shops in bake_mat_room.py's order, and the room each of our steps stands in: the
// Species step is upstream's BodyShop (Choose Your Type), the boy / girl step its GenderShop.
constexpr const char* kRoomShops[] = {"gender", "body", "color", "clothes", "name"};
constexpr int kRoomGender = 0, kRoomBody = 1, kRoomColor = 2, kRoomClothes = 3, kRoomName = 4;
constexpr float kRoomAnimSecs = ui::kMatRoomFrames / ui::kMatRoomFps; // 21 frames at 24 fps
constexpr const char* kCrashSfx[5] = {
    "sdmc:/3ds/toontown3ds/data/audio/mat_crash_boing.tsnd", "sdmc:/3ds/toontown3ds/data/audio/mat_crash_glass_boing.tsnd",
    "sdmc:/3ds/toontown3ds/data/audio/mat_crash_wood.tsnd", "sdmc:/3ds/toontown3ds/data/audio/mat_crash_wood_boing.tsnd",
    "sdmc:/3ds/toontown3ds/data/audio/mat_crash_wood_glass.tsnd"};

// A joint track at time t (linear between its 24 fps frames, held at the last one).
ui::MatRoomKey roomKey(const ui::MatRoomKey* track, float t) {
    float f = t * ui::kMatRoomFps;
    if (f <= 0.0f) return track[0];
    if (f >= ui::kMatRoomFrames - 1) return track[ui::kMatRoomFrames - 1];
    const int i = int(f);
    f -= float(i);
    return ui::MatRoomKey{track[i].z + (track[i + 1].z - track[i].z) * f,
                          track[i].scaleZ + (track[i + 1].scaleZ - track[i].scaleZ) * f};
}

void drawRoomPart(render::Renderer& renderer, render::Model& model, const ui::MatRoomKey& key) {
    if (!model.loaded() || key.scaleZ < 0.01f) return; // squished flat: nothing to draw
    C3D_Mtx m;
    Mtx_Identity(&m);
    Mtx_Translate(&m, 0.0f, 0.0f, key.z, true);
    Mtx_Scale(&m, 1.0f, 1.0f, key.scaleZ);
    renderer.drawModel(model, m);
}

} // namespace

void FrontEnd::releaseRoom() {
    roomCommon_.unload();
    for (RoomShop& r : roomShops_) {
        r.walls.unload();
        r.props.unload();
        r.shop = -1;
    }
    roomAnimT_ = 10.0f;
}

// MakeAToon.playRandomCrashSound, as each dropRoom starts.
void FrontEnd::playRoomCrash() {
    if (!crashTried_) {
        crashTried_ = true;
        for (int i = 0; i < 5; i++) crashSfx_[i] = audio::loadSfx(kCrashSfx[i]);
    }
    const int id = crashSfx_[osGetTime() % 5];
    if (id >= 0) audio::playSfx(id);
}

bool FrontEnd::ensureRoom() {
    if (roomFailed_) return false;
    int shop = kRoomName;
    if (!renameAvId_) {
        switch (step_) {
        case MakeStep::Species: shop = kRoomBody; break;
        case MakeStep::Body: shop = kRoomGender; break;
        case MakeStep::Color: shop = kRoomColor; break;
        case MakeStep::Clothes: shop = kRoomClothes; break;
        default: shop = kRoomName; break;
        }
    }
    RoomShop& cur = roomShops_[roomCur_];
    RoomShop& other = roomShops_[1 - roomCur_];
    if (cur.shop == shop && roomCommon_.loaded()) {
        if (roomAnimT_ >= kRoomAnimSecs && other.shop >= 0) { // the old shop has squished away
            other.walls.unload();
            other.props.unload();
            other.shop = -1;
        }
        return true;
    }
    // We draw after C3D_FrameBegin: the previous frame no longer reads what is replaced here.
    const u64 t0 = osGetTime();
    char path[64];
    bool ok = roomCommon_.loaded() || roomCommon_.load("romfs:/baked/mat_room/mat_room_common.ttm");
    const bool first = cur.shop < 0; // MakeAToon's first shop is posed in place, not dropped
    RoomShop& slot = first ? cur : other;
    slot.walls.unload();
    slot.props.unload();
    slot.shop = -1;
    snprintf(path, sizeof path, "romfs:/baked/mat_room/mat_room_%s_walls.ttm", kRoomShops[shop]);
    ok = ok && slot.walls.load(path);
    snprintf(path, sizeof path, "romfs:/baked/mat_room/mat_room_%s_props.ttm", kRoomShops[shop]);
    ok = ok && slot.props.load(path);
    if (!ok) {
        roomFailed_ = true;
        releaseRoom();
        TT_LOG("frontend: no Make-A-Toon room (%s); the plain preview instead", path);
        return false;
    }
    slot.shop = shop;
    roomCur_ = first ? roomCur_ : 1 - roomCur_;
    roomAnimT_ = first ? kRoomAnimSecs : 0.0f;
    if (!first) playRoomCrash();
    TT_LOG("frontend: mat room %s %s (%lu linear bytes, %u tris, %u ms)", kRoomShops[shop], first ? "posed" : "drops in",
           (unsigned long)(slot.walls.linearBytes() + slot.props.linearBytes()),
           unsigned(slot.walls.numTriangles() + slot.props.numTriangles()), unsigned(osGetTime() - t0));
    return true;
}

// MakeAToon.enter: camera.setPosHpr(-5.7, -12.3501, 2.15, -24.8499, 2.73, 0) with
// MakeAToonCameraFov (52 degrees across a 4:3 window, so 40.2 degrees vertically; the wider top
// screen shows a little more of the room at the sides). The Toon stands at toonPosition
// (-1.62, -3.49, 0) facing H 180, or at (1.5, -4, 0) facing H 120 in the Name shop.
void FrontEnd::drawRoom(render::Renderer& renderer) {
    constexpr float kPi = 3.14159265f;
    constexpr float kH = -24.8499f * kPi / 180.0f, kP = 2.73f * kPi / 180.0f;
    const C3D_FVec eye = FVec3_New(-5.7f, -12.3501f, 2.15f);
    const C3D_FVec at = FVec3_New(eye.x - sinf(kH) * cosf(kP) * 10.0f, eye.y + cosf(kH) * cosf(kP) * 10.0f,
                                  eye.z + sinf(kP) * 10.0f);
    renderer.setFovY(40.2f);
    renderer.beginWorld(renderer.topLeft(), eye, at, 0.0f, 0);
    C3D_Mtx m;
    Mtx_Identity(&m);
    renderer.drawModel(roomCommon_, m);
    // dropRoom: the new shop's walls and props on droppingJoint; squishRoom / squishProp: the old
    // shop's on scalingJoint / propJoint, until it is flat.
    RoomShop& cur = roomShops_[roomCur_];
    const ui::MatRoomKey drop = roomKey(ui::kMatRoomDrop, roomAnimT_);
    drawRoomPart(renderer, cur.walls, drop);
    drawRoomPart(renderer, cur.props, drop);
    RoomShop& old = roomShops_[1 - roomCur_];
    if (old.shop >= 0 && roomAnimT_ < kRoomAnimSecs) {
        drawRoomPart(renderer, old.walls, roomKey(ui::kMatRoomSquish, roomAnimT_));
        drawRoomPart(renderer, old.props, roomKey(ui::kMatPropSquish, roomAnimT_));
    }
    const bool name = cur.shop == kRoomName;
    Mtx_Identity(&m);
    Mtx_Translate(&m, name ? 1.5f : -1.62f, name ? -4.0f : -3.49f, 0.0f, true);
    Mtx_RotateZ(&m, ((name ? 120.0f : 180.0f) + makeTurn_) * kPi / 180.0f, true);
    preview_.draw(renderer, m);
    renderer.endWorld();
    renderer.setFovY(0.0f);
}

// progress-background's `bg` card (2.67 x 2 units, facing -Y) stood up 40 units down the view
// direction and scaled to overfill the frustum, so the radial blue fills the screen behind the Toon.
void FrontEnd::drawLoadingBg(render::Renderer& renderer, const C3D_FVec& eye, const C3D_FVec& at) {
    if (!loadingBg_.loaded()) return;
    constexpr float kDist = 40.0f, kPi = 3.14159265f;
    const float fx = at.x - eye.x, fy = at.y - eye.y, fz = at.z - eye.z;
    const float len = sqrtf(fx * fx + fy * fy + fz * fz);
    if (len <= 0.0f) return;
    const float halfH = tanf(render::Renderer::kFovYDeg * 0.5f * kPi / 180.0f) * kDist * 1.08f;
    const float halfW = halfH * (400.0f / 240.0f);
    C3D_Mtx m;
    Mtx_Identity(&m);
    Mtx_Translate(&m, eye.x + fx / len * kDist, eye.y + fy / len * kDist, eye.z + fz / len * kDist, true);
    Mtx_RotateZ(&m, atan2f(-fx, fy), true);                  // face the camera's heading
    Mtx_RotateX(&m, atan2f(fz, sqrtf(fx * fx + fy * fy)), true); // and its pitch
    Mtx_Scale(&m, halfW / 1.33333f, 1.0f, halfH);
    renderer.drawModel(loadingBg_, m);
}

// ToontownLoadingScreen's banner (toon_council's scroll at 0.4) with the TOON TIP in its brown
// (0.4, 0.3, 0.2) and the title label in navy (0, 0, 0.5) at (-1.06, -0.77). The scroll is drawn
// 1.31x upstream's size (its baked 181 x 125 px cell, 1:1) so the tip is readable at 400 x 240.
void FrontEnd::drawLoadingScroll(const char* label) {
    if (!loadingGui_.loaded() && !loadingGuiTried_) {
        loadingGuiTried_ = true;
        if (!loadingGui_.load("romfs:/baked/loading_gui.ttm")) TT_LOG("frontend: no loading_gui.ttm");
    }
    const ui::LoadingArtRect& r = ui::kLoadingArtRects[int(ui::LoadingArt::Scroll)];
    const float x = 200.0f - r.w * 0.5f, y = 80.0f;
    if (C3D_Tex* tex = loadingGui_.loaded() ? loadingGui_.texture(0) : nullptr) {
        const float u0 = r.x / ui::kLoadingAtlasW, u1 = (r.x + r.w) / ui::kLoadingAtlasW;
        const float v0 = 1.0f - r.y / ui::kLoadingAtlasH, v1 = 1.0f - (r.y + r.h) / ui::kLoadingAtlasH;
        Tex3DS_SubTexture sub{uint16_t(r.w + 0.5f), uint16_t(r.h + 0.5f), u0, v0, u1, v1};
        C2D_DrawImageAt(C2D_Image{tex, &sub}, x, y, 0.5f, nullptr, 1.0f, 1.0f);
    } else {
        ui::theme::paper(x + 6, y + 6, r.w - 12, r.h - 12, 6.0f);
    }
    const u32 brown = C2D_Color32(0x66, 0x4D, 0x33, 0xFF);
    char tip[192];
    snprintf(tip, sizeof tip, "%s\n%s", kTipTitle, kLoadingTips[tip_ % kLoadingTipCount]);
    C2D_Text t;
    textCache_.parse(&t, textBuf_, tip, 4096);
    C2D_DrawText(&t, C2D_WithColor | C2D_WordWrap, x + 22, y + 22, 0.5f, 0.44f, 0.44f, brown, r.w - 40);
    if (label) {
        C2D_Text l;
        textCache_.parse(&l, textBuf_, label, 4096);
        C2D_DrawText(&l, C2D_WithColor, 73, 212, 0.5f, 0.55f, 0.55f, C2D_Color32(0, 0, 0x80, 0xFF));
    }
}

void FrontEnd::drawTop(render::Renderer& renderer) {
    const bool room = screen_ == Screen::Make && ensureRoom();
    if (!room && roomCommon_.loaded()) releaseRoom(); // left Make-A-Toon: the previous frame is complete
    // The title logo covers the top third: aim higher there so the toon stands below it.
    const bool loading = screen_ == Screen::Connecting || screen_ == Screen::Entering;
    const bool logo = (screen_ == Screen::Title || loading) && logo_.loaded() && logo_.texture(0);
    if (room) {
        drawRoom(renderer);
    } else {
        // 3D preview: the toon on its own, slowly turning, in front of the loading screen's card.
        // The connecting / heading-to screens are ToontownLoadingScreen itself: no Toon, a tip.
        const float h = previewLook_.valid() ? previewLook_.nametagHeight : 4.0f;
        const float aimZ = logo ? h * 0.66f : h * 0.5f;
        const C3D_FVec eye = FVec3_New(0.0f, -h * 2.0f, aimZ + h * 0.1f), at = FVec3_New(0.0f, 0.0f, aimZ);
        renderer.beginWorld(renderer.topLeft(), eye, at, 0.0f, 0);
        drawLoadingBg(renderer, eye, at);
        if (!loading) {
            C3D_Mtx m;
            Mtx_Identity(&m);
            Mtx_RotateZ(&m, (previewSpin_ + 200.0f) * 3.14159265f / 180.0f, true);
            preview_.draw(renderer, m);
        }
        renderer.endWorld();
    }

    C2D_Prepare();
    // Persistent glyph cache: unchanged strings require no layout work.
    C2D_SceneBegin(renderer.topLeft());
    const u32 kShadow = C2D_Color32(0x10, 0x20, 0x50, 0xA0);
    const char* title = nullptr;
    u32 titleColor = kAccent;
    switch (screen_) {
    case Screen::Title: title = logo ? nullptr : "TOONTOWN 3DS"; break;
    case Screen::Connecting: title = "Connecting..."; break; // CRConnecting
    case Screen::Pick: title = "Pick A Toon To Play"; break; // AvatarChooserPickAToon
    case Screen::Make: // MakeAToon's guiTopBar: each shop's title in its text_fg
        if (renameAvId_) {
            title = "Choose Your Name"; // NameToonTitle
            titleColor = C2D_Color32(0x00, 0xFA, 0x80, 0xFF);
            break;
        }
        switch (step_) {
        case MakeStep::Species: title = "Choose Your Type"; titleColor = C2D_Color32(0x00, 0xFA, 0x80, 0xFF); break;
        case MakeStep::Body: title = "Choose Boy or Girl"; titleColor = C2D_Color32(0xFF, 0xEA, 0x33, 0xFF); break;
        case MakeStep::Color: title = "Choose Your Color"; titleColor = C2D_Color32(0x00, 0xFF, 0xFF, 0xFF); break;
        case MakeStep::Clothes: title = "Choose Your Clothes"; titleColor = C2D_Color32(0xFF, 0xEA, 0x33, 0xFF); break;
        default: title = "Choose Your Name"; titleColor = C2D_Color32(0x00, 0xFA, 0x80, 0xFF); break;
        }
        break;
    case Screen::TutorialChoice: title = "The Toontorial"; break;
    case Screen::Entering: title = "Heading to Toontown Central..."; break; // HeadingToHood
    case Screen::Error: title = "Uh-oh!"; break;
    }
    float tw, th;
    if (loading) drawLoadingScroll(title);
    if (logo) {
        // progress-background's logo card: 1.708 x 0.6 units, 0.05 below the top edge (2 units tall).
        Tex3DS_SubTexture sub{256, 128, 0.0f, 1.0f, 1.0f, 0.0f};
        C2D_Image img{logo_.texture(0), &sub};
        const float lw = 205.0f, lh = 72.0f;
        C2D_DrawImageAt(img, (400.0f - lw) * 0.5f, 6.0f, 0.5f, nullptr, lw / 256.0f, lh / 128.0f);
    } else {
        C2D_Text t;
        textCache_.parse(&t, textBuf_, title, 4096);
        C2D_TextGetDimensions(&t, 0.9f, 0.9f, &tw, &th);
        // Over the Make-A-Toon room the title needs a darker, tighter shadow to read on the walls.
        const u32 shadow = room ? C2D_Color32(0, 0, 0, 0xE0) : kShadow;
        C2D_DrawText(&t, C2D_WithColor, (400.0f - tw) * 0.5f + 2, 10, 0.5f, 0.9f, 0.9f, shadow);
        C2D_DrawText(&t, C2D_WithColor, (400.0f - tw) * 0.5f, 8, 0.5f, 0.9f, 0.9f, titleColor);
    }
    if (screen_ == Screen::Pick && net_) {
        const net::PotentialAvatar* pa = net_->avatarInSlot(static_cast<uint8_t>(pickSlot_));
        if (pa) {
            C2D_Text n;
            textCache_.parse(&n, textBuf_, pa->name, 4096);
            C2D_TextGetDimensions(&n, 0.7f, 0.7f, &tw, &th);
            C2D_DrawText(&n, C2D_WithColor, (400.0f - tw) * 0.5f, 200, 0.5f, 0.7f, 0.7f, kWhite);

            const char* statusStr = nullptr;
            u32 statusCol = kDim;
            if (pa->nameState == static_cast<uint8_t>(net::NameState::Open)) {
                statusStr = "Tap Name Your Toon! (or press X) to name it.";
                statusCol = kAccent;
            } else if (pa->nameState == static_cast<uint8_t>(net::NameState::Pending)) {
                statusStr = "Name pending approval";
                statusCol = kDim;
            } else if (pa->nameState == static_cast<uint8_t>(net::NameState::Approved)) {
                statusStr = "Name approved! Tap your Toon again to OK it.";
                statusCol = C2D_Color32(0x80, 0xFF, 0x80, 0xFF);
            } else if (pa->nameState == static_cast<uint8_t>(net::NameState::Rejected)) {
                statusStr = "Name rejected. Tap your Toon to pick another.";
                statusCol = kRed;
            } else {
                statusStr = "Tap again or press A to play. Y deletes.";
                statusCol = kDim;
            }
            if (statusStr) {
                C2D_Text st;
                textCache_.parse(&st, textBuf_, statusStr, 4096);
                C2D_TextGetDimensions(&st, 0.5f, 0.5f, &tw, &th);
                C2D_DrawText(&st, C2D_WithColor, (400.0f - tw) * 0.5f, 220, 0.5f, 0.5f, 0.5f, statusCol);
            }
        }
    } else if (screen_ == Screen::Make) {
        shadowText(200, 212, 0.7f, kWhite, 300, currentName_);
        if (room) {
            // MakeAToon's rotateLeftButton / rotateRightButton (tt_t_gui_mat_arrowRotateUp, the left
            // one mirrored) beside the Toon's feet; the top screen is not touchable, so they name L / R.
            constexpr float kD = 30.0f, kY = 172.0f;
            const float cx = roomShops_[roomCur_].shop == kRoomName ? 290.0f : 200.0f; // the Toon's feet on screen
            for (int side = 0; side < 2; ++side) {
                const float x = side ? cx + 46 : cx - 46 - kD;
                const bool held = side ? (hidKeysHeld() & KEY_R) : (hidKeysHeld() & KEY_L);
                const float grow = held ? 3.0f : 0.0f;
                ui::drawMatArt(ui::MatArt::RotateUp, x - grow, kY - grow, kD + grow * 2, kD + grow * 2, 0.5f, side == 0);
                shadowText(x + kD * 0.5f, kY + kD + 1, 0.42f, kWhite, 30, side ? "R" : "L");
            }
        }
    }
}

namespace {

// AvatarChoice's parts on the bottom screen (geometry from bake_pick_a_toon.py). The trashcan is
// drawn 1.3x upstream's size (hit box 4 px larger again) so a fingertip can hit it.
constexpr float kSquareHit = 92.0f;
constexpr float kTrashW = ui::pat::kTrash[0].pxW * 1.3f, kTrashH = ui::pat::kTrash[0].pxH * 1.3f;
constexpr float kLogoutX = 254, kLogoutY = 3, kLogoutW = 62, kLogoutH = 22;

FrontEnd::Button squareRect(int i) {
    return {ui::pat::kSlots[i].cx - kSquareHit * 0.5f, ui::pat::kSlots[i].cy - kSquareHit * 0.5f, kSquareHit, kSquareHit};
}
FrontEnd::Button trashRect(int i) {
    return {ui::pat::kSlots[i].delX - kTrashW * 0.5f - 2, ui::pat::kSlots[i].delY - kTrashH * 0.5f - 2, kTrashW + 4, kTrashH + 4};
}
// nameYourToonButton at (-0.2, -0.3) in the panel.
FrontEnd::Button namePillRect(int i) { return {ui::pat::kSlots[i].cx - 49, ui::pat::kSlots[i].cy + 22, 64, 22}; }

} // namespace

void FrontEnd::startMakeAToon() {
    choice_ = world::MakeAToonChoice{};
    step_ = MakeStep::Species;
    rebuildMakeDna();
    randomizePatternName();
    go(Screen::Make);
}

// AvatarChoice: a tap on an empty square makes a Toon there (MODE_CREATE); a tap on a Toon picks
// it for the top screen's preview and a second tap plays it (upstream plays on the first click;
// the 3DS shows the whole Toon up top first). The trashcan deletes, "Name Your Toon!" names an
// open-named Toon, Logout (or B) goes back to the title. Pad: the D-Pad moves between squares,
// A plays / makes, Y deletes, X names.
bool FrontEnd::pickInput(u32 down, bool back) {
    if (TT_BUTTON((Button{kLogoutX, kLogoutY, kLogoutW, kLogoutH}), "Logout", false, true) || back) {
        net_->disconnect();
        go(Screen::Title);
        return true;
    }
    if (down & (KEY_DUP | KEY_DDOWN | KEY_DLEFT | KEY_DRIGHT)) {
        int col = pickSlot_ % 3, row = pickSlot_ / 3;
        if (down & KEY_DLEFT) col = (col + 2) % 3;
        if (down & KEY_DRIGHT) col = (col + 1) % 3;
        if (down & (KEY_DUP | KEY_DDOWN)) row ^= 1;
        pickSlot_ = row * 3 + col;
        updatePickPreview();
        TT_LOG("padnav: pick focus %d", pickSlot_);
    }
    int tappedSlot = -1;
    bool trash = false, name = false;
    for (int i = 0; i < net::kMaxAvatars; i++) {
        const net::PotentialAvatar* pa = net_->avatarInSlot(static_cast<uint8_t>(i));
        if (pa && tapped(trashRect(i))) {
            tappedSlot = i;
            trash = true;
            break;
        }
        if (pa && pa->nameState == static_cast<uint8_t>(net::NameState::Open) && tapped(namePillRect(i))) {
            tappedSlot = i;
            name = true;
            break;
        }
        if (tapped(squareRect(i))) {
            tappedSlot = i;
            break;
        }
    }
    bool activate = (down & KEY_A) != 0;
    if (tappedSlot >= 0) {
        const bool second = tappedSlot == pickSlot_;
        if (!second) {
            pickSlot_ = tappedSlot;
            updatePickPreview();
        }
        // A Toon's first tap only picks it; an empty square, a trashcan or a name pill acts at once.
        activate = !trash && !name && (second || !net_->avatarInSlot(static_cast<uint8_t>(tappedSlot)));
    }
    const net::PotentialAvatar* sel = net_->avatarInSlot(static_cast<uint8_t>(pickSlot_));
    if (sel && (trash || (down & KEY_Y))) {
        TT_LOG("padnav: pick activate delete %d", pickSlot_);
        confirmDelete_ = true;
        return false;
    }
    const bool nameOpen = sel && sel->nameState == static_cast<uint8_t>(net::NameState::Open);
    if (nameOpen && (name || (down & KEY_X))) {
        TT_LOG("padnav: pick activate name %d", pickSlot_);
        world::ToonDna dna;
        choice_ = world::MakeAToonChoice{};
        if (sel->hasDna && world::parseNetString(sel->dna, net::kDnaSize, &dna)) {
            choice_.gender = dna.gender;
            makeDna_ = dna;
        }
        renameAvId_ = sel->id;
        nameError_[0] = 0;
        randomizePatternName();
        step_ = MakeStep::Name;
        go(Screen::Make);
        return true;
    }
    if (!activate) return false;
    TT_LOG("padnav: pick activate slot %d", pickSlot_);
    if (!sel) {
        startMakeAToon();
        return true;
    }
    if (sel->nameState == static_cast<uint8_t>(net::NameState::Approved) ||
        sel->nameState == static_cast<uint8_t>(net::NameState::Rejected)) {
        net_->sendAcknowledgeAvatarName(sel->id);
        return false;
    }
    if (net_->chooseAvatar(sel->id)) {
        go(Screen::Entering);
        return true;
    }
    return false;
}

void FrontEnd::shadowText(float cx, float y, float scale, u32 color, float maxW, const char* str) {
    C2D_Text t;
    textCache_.parse(&t, textBuf_, str, 4096);
    ui::theme::textCentered(t, cx + 1.2f, y + 1.2f, scale, C2D_Color32(0, 0, 0, 0xFF), maxW, 0.55f);
    ui::theme::textCentered(t, cx, y, scale, color, maxW, 0.56f);
}

// AvatarChooser on the bottom screen: tt_t_gui_pat_background with the six coloured squares
// (composited at rest by bake_pick_a_toon.py), and in each square the Toon's laff-meter head and
// name (AvatarChoice's ToonHead and nameText), its status, trashcan and Name Your Toon! pill, or
// "Make A Toon" (AvatarChoiceMakeAToon, text_fg 0, 1, 0.8) when the slot is empty.
void FrontEnd::drawPickBottom() {
    namespace th = ui::theme;
    if (!pickArt_.loaded() && !pickArtTried_) {
        pickArtTried_ = true; // one attempt per visit; a missing file logs once
        if (!pickArt_.load("romfs:/baked/pick_a_toon.ttm")) TT_LOG("frontend: no pick_a_toon.ttm");
    }
    C3D_Tex* bg = pickArt_.loaded() ? pickArt_.texture(0) : nullptr;
    C3D_Tex* trash = pickArt_.loaded() ? pickArt_.texture(1) : nullptr;
    if (bg) {
        Tex3DS_SubTexture sub{ui::pat::kScreenW, ui::pat::kScreenH, 0.0f, 1.0f, float(ui::pat::kScreenW) / ui::pat::kTexW,
                              1.0f - float(ui::pat::kScreenH) / ui::pat::kTexH};
        C2D_DrawImageAt(C2D_Image{bg, &sub}, 0, 0, 0.3f);
    } else {
        th::sky();
    }
    if (net_) {
        C2D_Text d;
        textCache_.parse(&d, textBuf_, net_->districtName(), 4096);
        th::textFit(d, 9, 7, 0.45f, C2D_Color32(0, 0, 0, 0xFF), 200, 0.55f);
        th::textFit(d, 8, 6, 0.45f, kWhite, 200, 0.56f);
    }
    const u32 kMake = C2D_Color32(0x00, 0xFF, 0xCC, 0xFF), kPlay = C2D_Color32(0xFF, 0xE6, 0x1A, 0xFF);
    if (confirmDelete_) return; // the dialog covers the chooser
    for (int i = 0; i < net::kMaxAvatars; i++) {
        const ui::pat::Slot& sl = ui::pat::kSlots[i];
        const bool selected = i == pickSlot_;
        if (selected) { // our pick: a gold ring around the square
            th::roundRing(sl.cx - ui::pat::kSquareHalf - 3, sl.cy - ui::pat::kSquareHalf - 3, ui::pat::kSquareHalf * 2 + 6,
                          ui::pat::kSquareHalf * 2 + 6, 14, 3, th::kYellow, 0.5f);
        }
        const net::PotentialAvatar* pa = net_ ? net_->avatarInSlot(static_cast<uint8_t>(i)) : nullptr;
        if (!pa) {
            shadowText(sl.cx, sl.cy - 17, 0.62f, kMake, 84, "Make A\nToon");
            continue;
        }
        world::ToonDna dna;
        ui::LaffFace face;
        face.hp = face.maxHp = 1; // the full-laff smile
        if (pa->hasDna && world::parseNetString(pa->dna, net::kDnaSize, &dna)) {
            const float* c = world::bodyColor(dna.headColor);
            for (int k = 0; k < 3; k++) face.rgb[k] = c[k];
            face.species = static_cast<int>(world::species(dna));
        }
        ui::drawLaffMeter(face, sl.cx - 22, sl.cy - 27, 44, nullptr, nullptr, 0.45f);
        shadowText(sl.nameX, sl.nameY - 8, 0.5f, kWhite, 88, pa->name);
        const char* status = nullptr;
        // statusText (0, -0.24): two short lines left of the trashcan, as TTLocalizer words them.
        if (pa->nameState == static_cast<uint8_t>(net::NameState::Pending)) status = "Under\nReview";
        else if (pa->nameState == static_cast<uint8_t>(net::NameState::Approved)) status = "Name\nApproved!";
        else if (pa->nameState == static_cast<uint8_t>(net::NameState::Rejected)) status = "Name\nRejected";
        if (pa->nameState == static_cast<uint8_t>(net::NameState::Open)) {
            const Button nb = namePillRect(i);
            th::pill(nb.x, nb.y, nb.w, nb.h, th::Pill::Yellow, false, 0.55f);
            C2D_Text nt;
            textCache_.parse(&nt, textBuf_, "Name Your Toon!", 4096);
            th::textCentered(nt, nb.x + nb.w * 0.5f, nb.y + 4, 0.4f, th::kInk, nb.w - 10, 0.57f);
        } else if (status) {
            shadowText(sl.cx - 12, sl.cy + 18, 0.4f, kWhite, 48, status);
        } else if (selected) {
            shadowText(sl.cx - 12, sl.cy + 18, 0.4f, kPlay, 48, "Play\nThis Toon"); // AvatarChoicePlayThisToon
        }
        if (trash) { // DELETE_POSITIONS: TrashCan_CLSD, TrashCan_OPEN on our pick
            const ui::pat::TrashRect& r = ui::pat::kTrash[selected ? 1 : 0];
            Tex3DS_SubTexture sub{uint16_t(r.w + 0.5f), uint16_t(r.h + 0.5f), r.x / ui::pat::kTrashAtlasW,
                                  1.0f - r.y / ui::pat::kTrashAtlasH, (r.x + r.w) / ui::pat::kTrashAtlasW,
                                  1.0f - (r.y + r.h) / ui::pat::kTrashAtlasH};
            C2D_DrawImageAt(C2D_Image{trash, &sub}, sl.delX - kTrashW * 0.5f, sl.delY - kTrashH * 0.5f, 0.58f,
                            nullptr, kTrashW / r.w, kTrashH / r.h);
        }
    }
}

void FrontEnd::shuffleFrame(float cy, const char* name, const char* count) {
    if (!gGui.drawing) return;
    const float h = kShuffleW / ui::matArtAspect(ui::MatArt::ShuffleFrame);
    const float x = (kW - kShuffleW) * 0.5f, y = cy - h * 0.5f;
    if (!ui::drawMatArt(ui::MatArt::ShuffleFrame, x, y, kShuffleW, h, 0.5f)) ui::theme::paper(x, y, kShuffleW, h, 8.0f);
    textCentered(Button{x + 24, y + 1, kShuffleW - 48, h}, 0.62f, kWhite, name);
    textCentered(Button{x, y + h - 2, kShuffleW, 18}, 0.45f, kBSoft, count);
}

void FrontEnd::drawMakeBottom() {
    static const char* const kStepNames[] = {"Species", "Body", "Color", "Clothes", "Name Shop"};
    if (renameAvId_) {
        text(10, 6, 0.5f, kBSoft, "Name Shop: name your Toon!");
    } else {
        text(10, 6, 0.5f, kBSoft, "Step %d of %d: %s", static_cast<int>(step_) + 1,
             static_cast<int>(MakeStep::Count), kStepNames[static_cast<int>(step_)]);
    }
    switch (step_) {
    case MakeStep::Body: { // what each frame is on now, level with it
        char head[32];
        snprintf(head, sizeof head, "%u of %u", unsigned(choice_.headVariant + 1),
                 unsigned(world::headVariantCount(choice_.species)));
        const char* values[3] = {head, kSizeLabel[choice_.torsoSize % 3], kSizeLabel[choice_.legSize % 3]};
        for (int i = 0; i < 3; i++)
            textCentered(Button{kSwatchX, kColorFrames[i + 1].cy - 9, 138, 18}, 0.52f, kBInk, values[i]);
        break;
    }
    case MakeStep::Color: {
        static const char* const kFor[4] = {"Tap a color: all", "Tap a color: head", "Tap a color: body", "Tap a color: legs"};
        textCentered(Button{kSwatchX - 2, 24, kSwatchCols * kSwatchStep, 16}, 0.42f, kBSoft, kFor[colorTarget_ & 3]);
        break;
    }
    case MakeStep::Clothes: {
        // ClothesGUI's topFrame / bottomFrame: the shuffle frame with its name in white, the gold
        // arrows at its ends; which one of how many sits under it.
        char count[24];
        snprintf(count, sizeof count, "%u of %u", unsigned(choice_.shirt + 1), unsigned(world::matShirtCount(choice_.gender)));
        shuffleFrame(kShirtY, "Shirts", count); // TTLocalizer.ClothesShopShirt
        snprintf(count, sizeof count, "%u of %u", unsigned(choice_.bottom + 1),
                 unsigned(world::matBottomCount(choice_.gender)));
        shuffleFrame(kBottomY, choice_.gender == world::Gender::Male ? "Shorts" : "Bottoms", count);
        break;
    }
    case MakeStep::Name: {
        const bool male = choice_.gender == world::Gender::Male;
        textCentered(Button{10, 24, 300, 22}, 0.65f, nameIsTyped_ ? kBHi : kBInk, currentName_);
        if (nameError_[0]) {
            textCentered(Button{10, 44, 300, 14}, 0.38f, kBErr, nameError_);
        }
        const char* titleStr = getNameString(getTitleId(male, titleIndex_));
        const char* firstStr = getNameString(getFirstId(male, firstIndex_));
        const char* prefixStr = getNameString(getPrefixId(prefixIndex_));
        const char* suffixStr = getNameString(getSuffixId(suffixIndex_));
        textCentered(Button{6, 82, 72, 20}, 0.42f, (!nameIsTyped_ && titleActive_) ? kBInk : kBSoft, titleStr ? titleStr : "");
        textCentered(Button{82, 82, 72, 20}, 0.42f, (!nameIsTyped_ && firstActive_) ? kBInk : kBSoft, firstStr ? firstStr : "");
        textCentered(Button{158, 82, 76, 20}, 0.42f, (!nameIsTyped_ && lastActive_) ? kBInk : kBSoft, prefixStr ? prefixStr : "");
        textCentered(Button{238, 82, 76, 20}, 0.42f, (!nameIsTyped_ && lastActive_) ? kBInk : kBSoft, suffixStr ? suffixStr : "");
        break;
    }
    default:
        break;
    }
}

void FrontEnd::drawBottom(C3D_RenderTarget* bottom) {
    C2D_TargetClear(bottom, kBg);
    C2D_SceneBegin(bottom);
    // Waiting / error screens are a dialog in the middle of the sky (like TTDialog over the loading
    // screen), not a page-sized card with one line in its corner.
    const bool dialog = screen_ == Screen::Connecting || screen_ == Screen::Entering || screen_ == Screen::Error;
    if (screen_ != Screen::Pick) { // Pick-A-Toon has its own full-screen background
        ui::theme::sky();
        if (dialog) {
            ui::theme::paper(24, 44, kW - 48, 132, 10.0f, ui::theme::kPaper,
                             screen_ == Screen::Error ? ui::theme::kRed : ui::theme::kPaperEdge);
        } else {
            ui::theme::paper(3, 3, kW - 6, 194, 8.0f); // the screen's content card; buttons sit below it
        }
    }
    gGui.drawing = true;
    gGui.index = 0;
    gGui.activate = false;
    switch (screen_) {
    case Screen::Title:
        text(10, 8, 0.5f, kBSoft, "A or tap to play. Toons and progress live on the server.");
        TT_BUTTON((Button{40, 70, 240, 48}), "Play online", true, true);
        TT_BUTTON((Button{40, 130, 240, 40}), "Play offline", false, true);
        text(12, 176, 0.42f, kBSoft, "%s", serverLine_[0] ? serverLine_ : "Server: sdmc:/3ds/toontown3ds/server.txt");
        break;
    case Screen::Connecting: {
        const bool more = net_ && (net_->lastError()[0] || net_->retryInMs()); // else the status sits mid-dialog
        dialogText(more ? 66 : 100, 0.62f, kBInk, net_ ? net_->progressText() : "Starting...");
        if (net_ && net_->lastError()[0]) dialogText(100, 0.46f, kBErr, net_->lastError());
        if (net_ && net_->retryInMs()) {
            char retry[40];
            snprintf(retry, sizeof retry, "Retrying in %lu s", (unsigned long)(net_->retryInMs() / 1000 + 1));
            dialogText(150, 0.46f, kBSoft, retry);
        }
        TT_BUTTON((Button{105, 196, 110, 34}), "Back", false, true);
        break;
    }
    case Screen::Pick: {
        drawPickBottom();
        // Button order matches update(): Logout with the chooser, Cancel / Delete in the dialog.
        if (!confirmDelete_) TT_BUTTON((Button{kLogoutX, kLogoutY, kLogoutW, kLogoutH}), "Logout", false, true);
        if (confirmDelete_) {
            // AvatarChoice.__handleDelete's TTGlobalDialog over the chooser.
            const net::PotentialAvatar* sel = net_ ? net_->avatarInSlot(static_cast<uint8_t>(pickSlot_)) : nullptr;
            char msg[96];
            snprintf(msg, sizeof msg, "This will delete %s forever.", sel ? sel->name : "this Toon"); // AvatarChoiceDeleteConfirm
            ui::theme::paper(18, 43, 284, 154, 8.0f, ui::theme::kPaper, ui::theme::kRed);
            textCentered(Button{20, 55, 280, 20}, 0.65f, kBInk, "Delete Toon?"); // AvatarChoiceDeletePasswordTitle
            textCentered(Button{24, 82, 272, 20}, 0.5f, kBHi, msg);
            textCentered(Button{20, 105, 280, 16}, 0.45f, kBErr, "This cannot be undone!");
            TT_BUTTON((Button{35, 140, 110, 34}), "Cancel", false, true);
            TT_BUTTON((Button{175, 140, 110, 34}), "Delete", true, true);
        }
        break;
    }
    case Screen::Make:
        drawMakeBottom();
        makeStepInput(0);
        TT_BUTTON((Button{10, 200, 110, 34}), "Back", false, true);
        TT_BUTTON((Button{200, 200, 110, 34}), step_ != MakeStep::Name ? "Next" : renameAvId_ ? "Done" : "Create!", true,
                  true);
        break;
    case Screen::TutorialChoice:
        textCentered(Button{18, 38, 284, 62}, 0.62f, kBInk, "Play the Toontorial?");
        textCentered(Button{18, 84, 284, 16}, 0.45f, kBSoft, "Learn the basics with Tutorial Tom,");
        textCentered(Button{18, 100, 284, 16}, 0.45f, kBSoft, "then head to Toontown Central.");
        TT_BUTTON((Button{20, 128, 130, 44}), "Play it", true, true);
        TT_BUTTON((Button{170, 128, 130, 44}), "Skip", false, true);
        textCentered(Button{18, 176, 284, 16}, 0.4f, kBSoft, "B also skips the Toontorial.");
        break;
    case Screen::Entering:
        dialogText(net_ && net_->lastError()[0] ? 80 : 100, 0.62f, kBInk, net_ ? net_->progressText() : "");
        if (net_ && net_->lastError()[0]) dialogText(116, 0.46f, kBErr, net_->lastError());
        break;
    case Screen::Error:
        dialogText(62, 0.52f, kBErr, error_[0] ? error_ : "Something went wrong.");
        TT_BUTTON((Button{100, 190, 120, 38}), "OK", true, true);
        break;
    }
}

} // namespace tt::game
