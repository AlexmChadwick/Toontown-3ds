#include "world/tutorial.h"

#include <cstdio>
#include <cstring>

#include "core/log.h"

namespace tt::world {

namespace {

int varIndex(quest::ScriptVar var) {
    const int i = static_cast<int>(var);
    return i >= 0 && i < static_cast<int>(quest::ScriptVar::Count) ? i : 0;
}

} // namespace

void TutorialFlow::start(quest::ScriptId id, bool femaleToon) {
    stop();
    for (int i = 0; i < kVars; ++i) {
        xforms_[i] = quest::ScriptXform{};
        colors_[i][0] = colors_[i][1] = colors_[i][2] = colors_[i][3] = 1.0f;
    }
    player_.start(id, femaleToon, this);
    TT_LOG("tutorial: script %s start", quest::scriptDef(id).name);
}

void TutorialFlow::update(float dt) {
    player_.update(dt);
    if (player_.popFinished()) {
        finished_ = true;
        TT_LOG("tutorial: script %s finish", quest::scriptDef(player_.id()).name);
    }
}

void TutorialFlow::advance() {
    // Panda waits for mouse-driven Shticker Book/SpeedChat messenger events here. On 3DS the
    // same affordances live persistently on the bottom screen, so A activates the highlighted
    // scripted action. postEvent remains available for exact touch/UI and autotest events.
    if (player_.block() == quest::ScriptPlayer::Block::Event) {
        const char* event = player_.waitingEvent();
        if (event) player_.postEvent(event);
        return;
    }
    if (player_.block() != quest::ScriptPlayer::Block::Chat) return;
    if (nextPage_) {
        showPage(nextPage_);
        return;
    }
    text_[0] = 0;
    player_.chatDone();
}

void TutorialFlow::showPage(const char* text) {
    nextPage_ = nullptr;
    if (!text) {
        text_[0] = 0;
        return;
    }
    const char* end = strchr(text, '\a'); // Nametag's stock page separator
    size_t len = end ? static_cast<size_t>(end - text) : strlen(text);
    if (len >= sizeof(text_)) {
        len = sizeof(text_) - 1;
        // Never split a UTF-8 code point. Prefer a word boundary for display-sized pages.
        while (len && (static_cast<unsigned char>(text[len]) & 0xc0) == 0x80) --len;
        size_t word = len;
        while (word && text[word] != ' ' && text[word] != '\n') --word;
        if (word) len = word;
        nextPage_ = text + len;
        while (*nextPage_ == ' ' || *nextPage_ == '\n') ++nextPage_;
    } else if (end) {
        nextPage_ = end + 1;
    }
    memcpy(text_, text, len);
    text_[len] = 0;
    TT_LOG("tutorial: dialogue %s", text_);
}

bool TutorialFlow::postEvent(const char* name) {
    return player_.postEvent(name);
}

void TutorialFlow::stop() {
    player_.stop();
    text_[0] = 0;
    nextPage_ = nullptr;
    arrows_ = false;
    finished_ = false;
}

bool TutorialFlow::takeFinished() {
    const bool finished = finished_;
    finished_ = false;
    return finished;
}

bool TutorialFlow::getXform(quest::ScriptVar var, quest::ScriptXform* out) {
    if (!out) return false;
    *out = xforms_[varIndex(var)];
    return true;
}

void TutorialFlow::setXform(quest::ScriptVar var, const quest::ScriptXform& x) {
    xforms_[varIndex(var)] = x;
}

void TutorialFlow::setColorScale(quest::ScriptVar var, const float rgba[4]) {
    if (!rgba) return;
    memcpy(colors_[varIndex(var)], rgba, sizeof(colors_[0]));
}

void TutorialFlow::run(const quest::ScriptCmd& cmd, const char* text) {
    switch (cmd.op) {
    case quest::ScriptOp::ChatConfirm:
    case quest::ScriptOp::LocalChatConfirm:
    case quest::ScriptOp::LocalChatPersist:
    case quest::ScriptOp::CCChatConfirm:
    case quest::ScriptOp::CCChatToConfirm:
        showPage(text);
        break;
    case quest::ScriptOp::ArrowsOn:
        arrows_ = true;
        break;
    case quest::ScriptOp::ArrowsOff:
        arrows_ = false;
        break;
    case quest::ScriptOp::FinishQuestMovie:
    case quest::ScriptOp::ClearChat:
        text_[0] = 0;
        nextPage_ = nullptr;
        break;
    default:
        // UI-only commands (book obscuring, inventory highlights and camera tracks) retain
        // their order in ScriptPlayer.  The compact 3DS HUD has no Panda node equivalent.
        break;
    }
}

} // namespace tt::world
