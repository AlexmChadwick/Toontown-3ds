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

bool isCard(quest::ScriptVar var) {
    using quest::ScriptVar;
    return var == ScriptVar::Cogs || var == ScriptVar::ToonBuilding || var == ScriptVar::CogBuilding ||
           var == ScriptVar::Squirt1 || var == ScriptVar::Squirt2;
}

} // namespace

void TutorialFlow::start(quest::ScriptId id, bool femaleToon) {
    stop();
    for (int i = 0; i < kVars; ++i) {
        xforms_[i] = quest::ScriptXform{};
        colors_[i][0] = colors_[i][1] = colors_[i][2] = colors_[i][3] = 1.0f;
    }
    // stop() cleared the staging; a script that never loads a character draws none.
    player_.start(id, femaleToon, this);
    TT_LOG("tutorial: script %s start", quest::scriptDef(id).name);
}

void TutorialFlow::update(float dt) {
    time_ += dt;
    player_.update(dt);
    if (player_.popFinished()) {
        finished_ = true;
        // A script that ends locked (no FREE_LOCALTOON) still hands the Toon back.
        if (toonPosed_) toonReleased_ = true;
        toonPosed_ = toonLocked_ = false;
        TT_LOG("tutorial: script %s finish", quest::scriptDef(player_.id()).name);
    }
}

void TutorialFlow::advance() {
    // The confirm chat's page is dismissed: its balloon goes (LOCAL_CHAT_PERSIST lines stay
    // up while their WAIT_EVENT blocks).
    if (player_.block() == quest::ScriptPlayer::Block::Chat) text_[0] = 0;
    player_.chatDone();
}

bool TutorialFlow::postEvent(const char* name) {
    return player_.postEvent(name);
}

void TutorialFlow::stop() {
    player_.stop();
    text_[0] = 0;
    arrows_ = false;
    card_ = quest::ScriptVar::None;
    speaker_ = quest::ScriptVar::None;
    charLoaded_ = charShown_ = toonPosed_ = toonLocked_ = cameraStaged_ = toonReleased_ = false;
    charAnim_ = ActorAnim{};
    toonAnim_ = ActorAnim{};
    time_ = 0.0f;
    finished_ = false;
    numEyeCalls_ = 0;
}

int TutorialFlow::takeNpcEyeCalls(EyeCall* out, int maxOut) {
    const int n = numEyeCalls_ < maxOut ? numEyeCalls_ : maxOut;
    for (int i = 0; i < n; ++i) out[i] = eyeCalls_[i];
    numEyeCalls_ = 0;
    return n;
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
    // LOCK_LOCALTOON's scripted moves hold the Toon until FREE_LOCALTOON. A POS of an unlocked
    // Toon (quest_assign_101's POS localToon 0.776 14.6 0) is a single setPos: place it once.
    if (var == quest::ScriptVar::LocalToon) {
        if (toonLocked_) toonPosed_ = true;
        else toonReleased_ = true;
    }
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
        speaker_ = cmd.var;
        TT_LOG("tutorial: dialogue %s", text_);
        break;
    case quest::ScriptOp::LoadClassicChar:
        charLoaded_ = true;
        break;
    case quest::ScriptOp::UnloadChar:
        if (cmd.var == quest::ScriptVar::ClassicChar) charLoaded_ = charShown_ = false;
        break;
    case quest::ScriptOp::LoopAnim:
    case quest::ScriptOp::PlayAnim: {
        ActorAnim* a = cmd.var == quest::ScriptVar::ClassicChar ? &charAnim_
                       : cmd.var == quest::ScriptVar::LocalToon ? &toonAnim_ : nullptr;
        if (a) {
            snprintf(a->name, sizeof(a->name), "%s", cmd.text ? cmd.text : "neutral");
            a->loop = cmd.op == quest::ScriptOp::LoopAnim;
            a->rate = cmd.numArgs > 0 ? cmd.args[0] : 1.0f;
            a->start = time_;
        }
        break;
    }
    case quest::ScriptOp::LockLocalToon:
        toonLocked_ = true;
        break;
    case quest::ScriptOp::FreeLocalToon:
        toonReleased_ = toonPosed_;
        toonPosed_ = toonLocked_ = false;
        toonAnim_ = ActorAnim{};
        break;
    case quest::ScriptOp::ArrowsOn:
        arrows_ = true;
        break;
    case quest::ScriptOp::ArrowsOff:
        arrows_ = false;
        break;
    case quest::ScriptOp::FinishQuestMovie:
        text_[0] = 0;
        card_ = quest::ScriptVar::None;
        break;
    case quest::ScriptOp::ReparentTo:
        if (cmd.var == quest::ScriptVar::ClassicChar) charShown_ = cmd.var2 == quest::ScriptVar::Render;
        if (cmd.var == quest::ScriptVar::Camera) cameraStaged_ = cmd.var2 == quest::ScriptVar::Render;
        if (isCard(cmd.var)) {
            if (cmd.var2 == quest::ScriptVar::Camera) card_ = cmd.var;
            else if (card_ == cmd.var) card_ = quest::ScriptVar::None;
        }
        break;
    case quest::ScriptOp::Function: {
        // quest_assign_101 / 110: the NPC's ToonHead eye methods.
        static const struct { const char* name; EyeCall call; } kEyeCalls[] = {
            {"normalEyes", EyeCall::Normal}, {"sadEyes", EyeCall::Sad}, {"angryEyes", EyeCall::Angry},
            {"surpriseEyes", EyeCall::Surprise}, {"blinkEyes", EyeCall::Blink}};
        if (cmd.var != quest::ScriptVar::Npc || !cmd.text) break;
        for (const auto& e : kEyeCalls) {
            if (strcmp(cmd.text, e.name) != 0) continue;
            if (numEyeCalls_ < kMaxEyeCalls) eyeCalls_[numEyeCalls_++] = e.call;
            break;
        }
        break;
    }
    default:
        // UI-only commands (book obscuring, inventory highlights and camera tracks) retain
        // their order in ScriptPlayer.  The compact 3DS HUD has no Panda node equivalent.
        break;
    }
}

} // namespace tt::world
