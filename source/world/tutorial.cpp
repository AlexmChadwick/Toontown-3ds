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
    player_.chatDone();
}

bool TutorialFlow::postEvent(const char* name) {
    return player_.postEvent(name);
}

void TutorialFlow::stop() {
    player_.stop();
    text_[0] = 0;
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
        snprintf(text_, sizeof(text_), "%s", text ? text : "");
        TT_LOG("tutorial: dialogue %s", text_);
        break;
    case quest::ScriptOp::ArrowsOn:
        arrows_ = true;
        break;
    case quest::ScriptOp::ArrowsOff:
        arrows_ = false;
        break;
    case quest::ScriptOp::FinishQuestMovie:
        text_[0] = 0;
        break;
    default:
        // UI-only commands (book obscuring, inventory highlights and camera tracks) retain
        // their order in ScriptPlayer.  The compact 3DS HUD has no Panda node equivalent.
        break;
    }
}

} // namespace tt::world
