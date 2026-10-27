#include "game/front_end.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "core/log.h"
#include "game/name_check.h"
#include "game/name_tables.h"

namespace tt::game {

namespace {

static char sTestTypedName[64] = {};
static bool sHasTestTypedName = false;

constexpr float kW = 320.0f;
const u32 kBg = C2D_Color32(0x1E, 0x2A, 0x44, 0xFF);
const u32 kPanel = C2D_Color32(0x2C, 0x3C, 0x60, 0xFF);
const u32 kAccent = C2D_Color32(0xF5, 0xB8, 0x1A, 0xFF);
const u32 kWhite = C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF);
const u32 kDim = C2D_Color32(0xA8, 0xB4, 0xCC, 0xFF);
const u32 kDisabled = C2D_Color32(0x40, 0x48, 0x5C, 0xFF);
const u32 kRed = C2D_Color32(0xFF, 0x80, 0x80, 0xFF);

const char* const kSpeciesLabel[9] = {"Dog", "Cat", "Horse", "Mouse", "Rabbit", "Duck", "Monkey", "Bear", "Pig"};
const char* const kSizeLabel[3] = {"Short", "Medium", "Tall"};

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
    C2D_DrawText(&t, C2D_WithColor, b.x + (b.w - w) * 0.5f, b.y + (b.h - h) * 0.5f, 0.5f, scale, scale, color);
}

void FrontEnd::button(const Button& b, const char* label, bool selected, bool enabled) {
    const bool focused = gGui.cursorShown && gGui.index - 1 == gGui.cursor;
    if (focused) C2D_DrawRectSolid(b.x - 2, b.y - 2, 0.5f, b.w + 4, b.h + 4, kWhite);
    C2D_DrawRectSolid(b.x, b.y, 0.5f, b.w, b.h, !enabled ? kDisabled : selected ? kAccent : kPanel);
    textCentered(b, 0.55f, selected ? kBg : enabled ? kWhite : kDim, label);
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
    // A tap fires when the stylus lifts, at the last touched point.
    touchReleased_ = touching_ && !(held & KEY_TOUCH);
    if (held & KEY_TOUCH) lastTouch_ = touch;
    touching_ = held & KEY_TOUCH;

    if (down & (KEY_DUP | KEY_DDOWN | KEY_DLEFT | KEY_DRIGHT)) {
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
        if (TT_BUTTON((Button{10, 200, 110, 34}), "Back", false, true) || back) {
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
        for (int s = 0; s < net::kMaxAvatars; s++) {
            const net::PotentialAvatar* pa = net_->avatarInSlot(static_cast<uint8_t>(s));
            const Button b{10.0f + (s % 2) * 155.0f, 28.0f + (s / 2) * 54.0f, 145.0f, 48.0f};
            if (TT_BUTTON(b, pa ? pa->name : "+ Make a Toon", s == pickSlot_, true)) {
                pickSlot_ = s;
                updatePickPreview();
            }
        }
        const net::PotentialAvatar* sel = net_->avatarInSlot(static_cast<uint8_t>(pickSlot_));
        if (sel) {
            if (TT_BUTTON((Button{10, 200, 80, 34}), "Back", false, true) || back) {
                net_->disconnect();
                go(Screen::Title);
                break;
            }
            if (TT_BUTTON((Button{95, 200, 75, 34}), "Delete", false, true)) {
                confirmDelete_ = true;
                break;
            }
            if (sel->nameState == static_cast<uint8_t>(net::NameState::Approved) ||
                sel->nameState == static_cast<uint8_t>(net::NameState::Rejected)) {
                if (TT_BUTTON((Button{175, 200, 135, 34}), "Acknowledge", true, true)) {
                    net_->sendAcknowledgeAvatarName(sel->id);
                }
            } else {
                if (TT_BUTTON((Button{175, 200, 135, 34}), "Play", true, true)) {
                    if (net_->chooseAvatar(sel->id)) go(Screen::Entering);
                }
            }
        } else {
            if (TT_BUTTON((Button{10, 200, 110, 34}), "Back", false, true) || back) {
                net_->disconnect();
                go(Screen::Title);
                break;
            }
            if (TT_BUTTON((Button{200, 200, 110, 34}), "Make", true, true)) {
                choice_ = world::MakeAToonChoice{};
                step_ = MakeStep::Species;
                rebuildMakeDna();
                randomizePatternName();
                go(Screen::Make);
            }
        }
        break;
    }

    case Screen::Make:
        makeStepInput(down);
        if (screen_ != Screen::Make) break;
        if (TT_BUTTON((Button{10, 200, 110, 34}), "Back", false, true) || back) {
            if (step_ == MakeStep::Species) {
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
                snprintf(error_, sizeof(error_), "That toon could not be created. Try different choices.");
                go(Screen::Error);
            }
        }
        if (TT_BUTTON((Button{170, 128, 130, 44}), "Skip", false, true) || back) {
            if (createToon(net::Client::TutorialChoice::Skip)) go(Screen::Entering);
            else {
                snprintf(error_, sizeof(error_), "That toon could not be created. Try different choices.");
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
        const u8 heads = world::headVariantCount(choice_.species);
        if (TT_BUTTON((Button{10, 28, 70, 34}), "Head", false, heads > 1)) {
            choice_.headVariant = static_cast<u8>((choice_.headVariant + 1) % heads);
            rebuildMakeDna();
        }
        for (int i = 0; i < 3; i++) {
            if (TT_BUTTON((Button{90.0f + i * 75.0f, 70, 70, 34}), kSizeLabel[i], choice_.torsoSize == i, true)) {
                choice_.torsoSize = static_cast<u8>(i);
                rebuildMakeDna();
            }
        }
        for (int i = 0; i < 3; i++) {
            if (TT_BUTTON((Button{90.0f + i * 75.0f, 112, 70, 34}), kSizeLabel[i], choice_.legSize == i, true)) {
                choice_.legSize = static_cast<u8>(i);
                rebuildMakeDna();
            }
        }
        for (int g = 0; g < 2; g++) {
            const Gender gender = g == 0 ? Gender::Male : Gender::Female;
            if (TT_BUTTON((Button{90.0f + g * 112.0f, 154, 106, 34}), g == 0 ? "Boy" : "Girl",
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
        static const char* const kTargets[4] = {"All", "Head", "Arms", "Legs"};
        for (int i = 0; i < 4; i++) {
            if (TT_BUTTON((Button{10.0f + i * 76.0f, 26, 70, 28}), kTargets[i], colorTarget_ == i, true)) {
                colorTarget_ = i;
            }
        }
        const u8 n = world::matColorCount(choice_.gender);
        for (u8 i = 0; i < n; i++) {
            const u8 c = world::matColor(choice_.gender, i);
            const Button b{12.0f + (i % 9) * 33.0f, 62.0f + (i / 9) * 33.0f, 29.0f, 29.0f};
            gGui.index++;
            if (gGui.drawing) {
                const float* rgb = world::bodyColor(c);
                const bool sel = choice_.headColor == c && (colorTarget_ == 0 || colorTarget_ == 1);
                const bool focused = gGui.cursorShown && gGui.index - 1 == gGui.cursor;
                if (focused || sel) C2D_DrawRectSolid(b.x - 2, b.y - 2, 0.5f, b.w + 4, b.h + 4, focused ? kAccent : kWhite);
                C2D_DrawRectSolid(b.x, b.y, 0.5f, b.w, b.h, C2D_Color32f(rgb[0], rgb[1], rgb[2], 1.0f));
            } else if (tapped(b) || (gGui.activate && gGui.cursorShown && gGui.index - 1 == gGui.cursor)) {
                if (colorTarget_ == 0 || colorTarget_ == 1) choice_.headColor = c;
                if (colorTarget_ == 0 || colorTarget_ == 2) choice_.armColor = c;
                if (colorTarget_ == 0 || colorTarget_ == 3) choice_.legColor = c;
                rebuildMakeDna();
            }
        }
        break;
    }
    case MakeStep::Clothes: {
        const u8 shirts = world::matShirtCount(choice_.gender);
        const u8 bottoms = world::matBottomCount(choice_.gender);
        if (TT_BUTTON((Button{10, 50, 60, 40}), "<", false, shirts > 1)) {
            choice_.shirt = static_cast<u8>((choice_.shirt + shirts - 1) % shirts);
            rebuildMakeDna();
        }
        if (TT_BUTTON((Button{250, 50, 60, 40}), ">", false, shirts > 1)) {
            choice_.shirt = static_cast<u8>((choice_.shirt + 1) % shirts);
            rebuildMakeDna();
        }
        if (TT_BUTTON((Button{10, 120, 60, 40}), "<", false, bottoms > 1)) {
            choice_.bottom = static_cast<u8>((choice_.bottom + bottoms - 1) % bottoms);
            rebuildMakeDna();
        }
        if (TT_BUTTON((Button{250, 120, 60, 40}), ">", false, bottoms > 1)) {
            choice_.bottom = static_cast<u8>((choice_.bottom + 1) % bottoms);
            rebuildMakeDna();
        }
        break;
    }
    case MakeStep::Name: {
        const bool male = choice_.gender == world::Gender::Male;
        // Toggle buttons:
        if (TT_BUTTON((Button{6, 56, 72, 22}), titleActive_ ? "Title [X]" : "Title [ ]", titleActive_, true)) {
            titleActive_ = !titleActive_;
            nameIsTyped_ = false;
            updateConstructedName();
        }
        if (TT_BUTTON((Button{82, 56, 72, 22}), firstActive_ ? "First [X]" : "First [ ]", firstActive_, true)) {
            if (!firstActive_ || lastActive_) {
                firstActive_ = !firstActive_;
                nameIsTyped_ = false;
                updateConstructedName();
            }
        }
        if (TT_BUTTON((Button{158, 56, 76, 22}), lastActive_ ? "Prefix [X]" : "Prefix [ ]", lastActive_, true)) {
            if (!lastActive_ || firstActive_) {
                lastActive_ = !lastActive_;
                nameIsTyped_ = false;
                updateConstructedName();
            }
        }
        if (TT_BUTTON((Button{238, 56, 76, 22}), lastActive_ ? "Suffix [X]" : "Suffix [ ]", lastActive_, true)) {
            if (!lastActive_ || firstActive_) {
                lastActive_ = !lastActive_;
                nameIsTyped_ = false;
                updateConstructedName();
            }
        }
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
                swkbdSetHintText(&swkbd, "Enter your toon's name");
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

void FrontEnd::drawTop(render::Renderer& renderer) {
    // 3D preview: the toon on its own, slowly turning.
    const float h = previewLook_.valid() ? previewLook_.nametagHeight : 4.0f;
    // The title logo covers the top third: aim higher there so the toon stands below it.
    const bool logo = screen_ == Screen::Title && logo_.loaded() && logo_.texture(0);
    const float aimZ = logo ? h * 0.66f : h * 0.5f;
    renderer.beginWorld(renderer.topLeft(), FVec3_New(0.0f, -h * 2.0f, aimZ + h * 0.1f), FVec3_New(0.0f, 0.0f, aimZ),
                        0.0f, 0);
    C3D_Mtx m;
    Mtx_Identity(&m);
    Mtx_RotateZ(&m, (previewSpin_ + 200.0f) * 3.14159265f / 180.0f, true);
    preview_.draw(renderer, m);
    renderer.endWorld();

    C2D_Prepare();
    // Persistent glyph cache: unchanged strings require no layout work.
    C2D_SceneBegin(renderer.topLeft());
    const u32 kShadow = C2D_Color32(0x10, 0x20, 0x50, 0xA0);
    const char* title = nullptr;
    switch (screen_) {
    case Screen::Title: title = logo ? nullptr : "TOONTOWN 3DS"; break;
    case Screen::Connecting: title = "Connecting..."; break;
    case Screen::Pick: title = "Pick a Toon"; break;
    case Screen::Make: title = "Make a Toon"; break;
    case Screen::TutorialChoice: title = "The Toontorial"; break;
    case Screen::Entering: title = "Heading to Toontown Central"; break;
    case Screen::Error: title = "Uh-oh!"; break;
    }
    float tw, th;
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
        C2D_DrawText(&t, C2D_WithColor, (400.0f - tw) * 0.5f + 2, 10, 0.5f, 0.9f, 0.9f, kShadow);
        C2D_DrawText(&t, C2D_WithColor, (400.0f - tw) * 0.5f, 8, 0.5f, 0.9f, 0.9f, kAccent);
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
                statusStr = "Name: Open (Tap to choose name)";
                statusCol = kAccent;
            } else if (pa->nameState == static_cast<uint8_t>(net::NameState::Pending)) {
                statusStr = "Name: Under Review";
                statusCol = kDim;
            } else if (pa->nameState == static_cast<uint8_t>(net::NameState::Approved)) {
                statusStr = "Name Approved! Press Acknowledge.";
                statusCol = C2D_Color32(0x80, 0xFF, 0x80, 0xFF);
            } else if (pa->nameState == static_cast<uint8_t>(net::NameState::Rejected)) {
                statusStr = "Name Rejected. Press Acknowledge.";
                statusCol = kRed;
            }
            if (statusStr) {
                C2D_Text st;
                textCache_.parse(&st, textBuf_, statusStr, 4096);
                C2D_TextGetDimensions(&st, 0.5f, 0.5f, &tw, &th);
                C2D_DrawText(&st, C2D_WithColor, (400.0f - tw) * 0.5f, 220, 0.5f, 0.5f, 0.5f, statusCol);
            }
        }
    } else if (screen_ == Screen::Make) {
        C2D_Text n;
        textCache_.parse(&n, textBuf_, currentName_, 4096);
        C2D_TextGetDimensions(&n, 0.7f, 0.7f, &tw, &th);
        C2D_DrawText(&n, C2D_WithColor, (400.0f - tw) * 0.5f, 210, 0.5f, 0.7f, 0.7f, kWhite);
    }
}

void FrontEnd::drawPickBottom() {
    text(10, 6, 0.5f, kDim, "%s", net_ ? net_->districtName() : "");
}

void FrontEnd::drawMakeBottom() {
    static const char* const kStepNames[] = {"Species", "Body", "Colour", "Clothes", "Name Shop"};
    text(10, 6, 0.5f, kDim, "Step %d of %d: %s", static_cast<int>(step_) + 1, static_cast<int>(MakeStep::Count),
         kStepNames[static_cast<int>(step_)]);
    switch (step_) {
    case MakeStep::Body:
        text(90, 36, 0.5f, kWhite, "%s head %u of %u", kSpeciesLabel[static_cast<int>(choice_.species)],
             unsigned(choice_.headVariant + 1), unsigned(world::headVariantCount(choice_.species)));
        text(10, 79, 0.5f, kWhite, "Torso");
        text(10, 121, 0.5f, kWhite, "Legs");
        text(10, 163, 0.5f, kWhite, "Gender");
        break;
    case MakeStep::Clothes:
        text(90, 60, 0.55f, kWhite, "Shirt %u of %u", unsigned(choice_.shirt + 1),
             unsigned(world::matShirtCount(choice_.gender)));
        text(90, 130, 0.55f, kWhite, "%s %u of %u", choice_.gender == world::Gender::Male ? "Shorts" : "Bottoms",
             unsigned(choice_.bottom + 1), unsigned(world::matBottomCount(choice_.gender)));
        break;
    case MakeStep::Name: {
        const bool male = choice_.gender == world::Gender::Male;
        textCentered(Button{10, 24, 300, 22}, 0.65f, nameIsTyped_ ? kAccent : kWhite, currentName_);
        if (nameError_[0]) {
            textCentered(Button{10, 44, 300, 14}, 0.38f, kRed, nameError_);
        }
        const char* titleStr = getNameString(getTitleId(male, titleIndex_));
        const char* firstStr = getNameString(getFirstId(male, firstIndex_));
        const char* prefixStr = getNameString(getPrefixId(prefixIndex_));
        const char* suffixStr = getNameString(getSuffixId(suffixIndex_));
        textCentered(Button{6, 82, 72, 20}, 0.42f, (!nameIsTyped_ && titleActive_) ? kWhite : kDim, titleStr ? titleStr : "");
        textCentered(Button{82, 82, 72, 20}, 0.42f, (!nameIsTyped_ && firstActive_) ? kWhite : kDim, firstStr ? firstStr : "");
        textCentered(Button{158, 82, 76, 20}, 0.42f, (!nameIsTyped_ && lastActive_) ? kWhite : kDim, prefixStr ? prefixStr : "");
        textCentered(Button{238, 82, 76, 20}, 0.42f, (!nameIsTyped_ && lastActive_) ? kWhite : kDim, suffixStr ? suffixStr : "");
        break;
    }
    default:
        break;
    }
}

void FrontEnd::drawBottom(C3D_RenderTarget* bottom) {
    C2D_TargetClear(bottom, kBg);
    C2D_SceneBegin(bottom);
    gGui.drawing = true;
    gGui.index = 0;
    gGui.activate = false;
    switch (screen_) {
    case Screen::Title:
        text(10, 8, 0.5f, kDim, "A or tap to play. Toons and progress live on the server.");
        TT_BUTTON((Button{40, 70, 240, 48}), "Play online", true, true);
        TT_BUTTON((Button{40, 130, 240, 40}), "Play offline", false, true);
        text(10, 190, 0.45f, kDim, "%s", serverLine_[0] ? serverLine_ : "Server: sdmc:/3ds/toontown3ds/server.txt");
        break;
    case Screen::Connecting:
        text(16, 40, 0.6f, kWhite, "%s", net_ ? net_->progressText() : "Starting...");
        if (net_ && net_->lastError()[0]) text(16, 90, 0.5f, kRed, "%s", net_->lastError());
        if (net_ && net_->retryInMs()) text(16, 150, 0.5f, kDim, "Retrying in %lu s", (unsigned long)(net_->retryInMs() / 1000 + 1));
        TT_BUTTON((Button{10, 200, 110, 34}), "Back", false, true);
        break;
    case Screen::Pick: {
        drawPickBottom();
        if (confirmDelete_) {
            const net::PotentialAvatar* sel = net_ ? net_->avatarInSlot(static_cast<uint8_t>(pickSlot_)) : nullptr;
            C2D_DrawRectSolid(18, 43, 0.5f, 284, 154, kAccent);
            C2D_DrawRectSolid(20, 45, 0.5f, 280, 150, kPanel);
            textCentered(Button{20, 55, 280, 20}, 0.65f, kWhite, "Delete this Toon?");
            textCentered(Button{20, 80, 280, 20}, 0.55f, kAccent, sel ? sel->name : "");
            textCentered(Button{20, 105, 280, 16}, 0.45f, kRed, "This cannot be undone!");
            TT_BUTTON((Button{35, 140, 110, 34}), "Cancel", false, true);
            TT_BUTTON((Button{175, 140, 110, 34}), "Delete", true, true);
            break;
        }
        for (int s = 0; s < net::kMaxAvatars; s++) {
            const net::PotentialAvatar* pa = net_ ? net_->avatarInSlot(static_cast<uint8_t>(s)) : nullptr;
            const Button b{10.0f + (s % 2) * 155.0f, 28.0f + (s / 2) * 54.0f, 145.0f, 48.0f};
            TT_BUTTON(b, pa ? pa->name : "+ Make a Toon", s == pickSlot_, true);
            if (pa) {
                const char* badge = nullptr;
                u32 col = kDim;
                if (pa->nameState == static_cast<uint8_t>(net::NameState::Open)) {
                    badge = "[Name Me]";
                    col = kAccent;
                } else if (pa->nameState == static_cast<uint8_t>(net::NameState::Pending)) {
                    badge = "[Pending]";
                    col = kDim;
                } else if (pa->nameState == static_cast<uint8_t>(net::NameState::Approved)) {
                    badge = "[Approved!]";
                    col = C2D_Color32(0x80, 0xFF, 0x80, 0xFF);
                } else if (pa->nameState == static_cast<uint8_t>(net::NameState::Rejected)) {
                    badge = "[Rejected]";
                    col = kRed;
                }
                if (badge) {
                    text(b.x + 6, b.y + 32, 0.38f, col, "%s", badge);
                }
            }
        }
        const net::PotentialAvatar* sel = net_ ? net_->avatarInSlot(static_cast<uint8_t>(pickSlot_)) : nullptr;
        if (sel) {
            TT_BUTTON((Button{10, 200, 80, 34}), "Back", false, true);
            TT_BUTTON((Button{95, 200, 75, 34}), "Delete", false, true);
            if (sel->nameState == static_cast<uint8_t>(net::NameState::Approved) ||
                sel->nameState == static_cast<uint8_t>(net::NameState::Rejected)) {
                TT_BUTTON((Button{175, 200, 135, 34}), "Acknowledge", true, true);
            } else {
                TT_BUTTON((Button{175, 200, 135, 34}), "Play", true, true);
            }
        } else {
            TT_BUTTON((Button{10, 200, 110, 34}), "Back", false, true);
            TT_BUTTON((Button{200, 200, 110, 34}), "Make", true, true);
        }
        break;
    }
    case Screen::Make:
        drawMakeBottom();
        makeStepInput(0);
        TT_BUTTON((Button{10, 200, 110, 34}), "Back", false, true);
        TT_BUTTON((Button{200, 200, 110, 34}), step_ == MakeStep::Name ? "Create!" : "Next", true, true);
        break;
    case Screen::TutorialChoice:
        textCentered(Button{18, 38, 284, 62}, 0.62f, kWhite, "Play the Toontorial?");
        textCentered(Button{18, 86, 284, 30}, 0.42f, kDim,
                     "Learn the basics with Tutorial Tom, then reach Toontown Central.");
        TT_BUTTON((Button{20, 128, 130, 44}), "Play it", true, true);
        TT_BUTTON((Button{170, 128, 130, 44}), "Skip", false, true);
        textCentered(Button{18, 185, 284, 20}, 0.38f, kDim, "B also skips the Toontorial.");
        break;
    case Screen::Entering:
        text(16, 60, 0.6f, kWhite, "%s", net_ ? net_->progressText() : "");
        if (net_ && net_->lastError()[0]) text(16, 110, 0.5f, kRed, "%s", net_->lastError());
        break;
    case Screen::Error:
        text(16, 40, 0.55f, kRed, "%s", error_[0] ? error_ : "Something went wrong.");
        TT_BUTTON((Button{100, 190, 120, 38}), "OK", true, true);
        break;
    }
}

} // namespace tt::game
