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
    ended_ = false;
    player_.start(id, femaleToon, this);
    TT_LOG("tutorial: script %s start", quest::scriptDef(id).name);
}

void TutorialFlow::update(float dt) {
    time_ += dt;
    player_.update(dt);
    if (player_.popFinished()) {
        finished_ = true;
        TT_LOG("tutorial: script %s finish", quest::scriptDef(player_.id()).name);
    }
    if (player_.running() && !player_.active() && !ended_) endMovie();
}

void TutorialFlow::endMovie() {
    ended_ = true;
    // DistributedNPCToon.cleanupMovie / the blocker's movie cleanup: the UPON_TIMEOUT commands.
    player_.timeout();
    // finishMovie / FREE_LOCALTOON: the Toon and the camera go back to the player.
    if (toonPosed_) toonPlace_ = true;
    toonPosed_ = toonLocked_ = toonHidden_ = false;
    toonAnim_ = ActorAnim{};
    cameraParent_ = quest::ScriptVar::None;
    cameraWrt_ = false;
    arrows_ = false;
    card_ = quest::ScriptVar::None;
    bookScripted_ = false;
    laffValue_ = laffPosed_ = false; // the meter goes back to the Toon's own Laff, in its corner
}

void TutorialFlow::advance() {
    if (player_.block() != quest::ScriptPlayer::Block::Chat) return;
    // setPageChat: each \x07 page is its own balloon; 'doneChatPage' follows the last one.
    if (page_ + 1 < numPages_) {
        ++page_;
        TT_LOG("tutorial: dialogue page %d/%d", page_ + 1, numPages_);
        return;
    }
    // The confirm chat's last page is dismissed: its balloon goes (LOCAL_CHAT_PERSIST lines
    // stay up while their WAIT_EVENT blocks).
    clearLine();
    player_.chatDone();
}

bool TutorialFlow::postEvent(const char* name) {
    return player_.postEvent(name);
}

void TutorialFlow::stop() {
    if (player_.running() && !ended_) endMovie();
    player_.stop();
    clearLine();
    arrows_ = false;
    card_ = quest::ScriptVar::None;
    speaker_ = quest::ScriptVar::None;
    cameraParent_ = quest::ScriptVar::None;
    charLoaded_ = charShown_ = toonPosed_ = toonPlace_ = toonLocked_ = toonHidden_ = cameraWrt_ = false;
    npcPosed_ = npcHidden_ = false;
    charAnim_ = ActorAnim{};
    toonAnim_ = ActorAnim{};
    npcAnim_ = ActorAnim{};
    time_ = 0.0f;
    finished_ = false;
    ended_ = true;
    bookScripted_ = false;
    closeEnabled_ = nextShown_ = prevShown_ = true;
    bookRequest_ = 0;
    laffValue_ = laffPosed_ = false;
    numEyeCalls_ = 0;
}

int TutorialFlow::takeNpcEyeCalls(EyeCall* out, int maxOut) {
    const int n = numEyeCalls_ < maxOut ? numEyeCalls_ : maxOut;
    for (int i = 0; i < n; ++i) out[i] = eyeCalls_[i];
    numEyeCalls_ = 0;
    return n;
}

void TutorialFlow::dropPersist() {
    if (!persisting()) return;
    clearLine();
}

void TutorialFlow::setLine(const char* text) {
    snprintf(line_, sizeof(line_), "%s", text ? text : "");
    numPages_ = page_ = 0;
    persist_ = false;
    char* p = line_;
    while (numPages_ < kMaxPages) {
        pages_[numPages_++] = p;
        char* end = strchr(p, '\x07');
        if (!end) break;
        *end = 0;
        p = end + 1;
    }
}

void TutorialFlow::clearLine() {
    line_[0] = 0;
    numPages_ = page_ = 0;
    persist_ = false;
}

void TutorialFlow::setInTutorialHood(bool in) {
    if (in == hood_) return;
    hood_ = in;
    // enterTutorialHood: book.obscureButton(1), laffMeter.obscure(1), chatMgr.obscure(1, 1);
    // exitTutorialHood: all back.
    bookShown_ = laffShown_ = scShown_ = !in;
    TT_LOG("tutorial: HUD %s", in ? "obscured for the Toontorial" : "restored");
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
    if (var == quest::ScriptVar::LocalToon) {
        // LOCK_LOCALTOON's scripted moves follow the script every frame; an unlocked Toon (the
        // 'quest' place state) is only put there.
        if (toonLocked_) toonPosed_ = true;
        else toonPlace_ = true;
    }
    if (var == quest::ScriptVar::Npc) npcPosed_ = true;
    if (var == quest::ScriptVar::LaffMeter) laffPosed_ = true;
}

void TutorialFlow::setColorScale(quest::ScriptVar var, const float rgba[4]) {
    if (!rgba) return;
    memcpy(colors_[varIndex(var)], rgba, sizeof(colors_[0]));
}

void TutorialFlow::run(const quest::ScriptCmd& cmd, const char* text) {
    using quest::ScriptOp;
    using quest::ScriptVar;
    switch (cmd.op) {
    case ScriptOp::ChatConfirm:
    case ScriptOp::LocalChatConfirm:
    case ScriptOp::LocalChatPersist:
    case ScriptOp::CCChatConfirm:
    case ScriptOp::CCChatToConfirm:
        // The 3DS moves with the Circle Pad, as its minigame hints say, not the arrow keys.
        if (cmd.key && !strcmp(cmd.key, "QuestScriptTutorialMickey_4")) text = "Come here! Use the Circle Pad to move.";
        setLine(text);
        persist_ = cmd.op == ScriptOp::LocalChatPersist;
        reversed_ = (cmd.flags & quest::kScriptReversed) != 0;
        speaker_ = cmd.var;
        TT_LOG("tutorial: dialogue %s", line_);
        break;
    case ScriptOp::ClearChat:
        if (cmd.var != ScriptVar::LocalToon) clearLine(); // CLEAR_CHAT localToon: our own SpeedChat
        break;
    case ScriptOp::LoadClassicChar:
        charLoaded_ = true;
        break;
    case ScriptOp::UnloadChar:
        if (cmd.var == ScriptVar::ClassicChar) charLoaded_ = charShown_ = false;
        break;
    case ScriptOp::LoopAnim:
    case ScriptOp::PlayAnim: {
        ActorAnim* a = cmd.var == ScriptVar::ClassicChar ? &charAnim_
                       : cmd.var == ScriptVar::LocalToon ? &toonAnim_
                       : cmd.var == ScriptVar::Npc       ? &npcAnim_ : nullptr;
        if (a) {
            snprintf(a->name, sizeof(a->name), "%s", cmd.text ? cmd.text : "neutral");
            a->loop = cmd.op == ScriptOp::LoopAnim;
            a->rate = cmd.numArgs > 0 ? cmd.args[0] : 1.0f;
            a->start = time_;
        }
        break;
    }
    case ScriptOp::LockLocalToon:
        toonLocked_ = true;
        break;
    case ScriptOp::FreeLocalToon:
        // attachCamera, enableAvatarControls: the last scripted pose once, then the player's.
        if (toonPosed_) toonPlace_ = true;
        toonPosed_ = toonLocked_ = false;
        toonAnim_ = ActorAnim{};
        cameraParent_ = ScriptVar::None;
        break;
    case ScriptOp::Show:
    case ScriptOp::Hide: {
        const bool show = cmd.op == ScriptOp::Show;
        if (cmd.var == ScriptVar::LocalToon) toonHidden_ = !show;
        else if (cmd.var == ScriptVar::LaffMeter) laffShown_ = show;
        else if (cmd.var == ScriptVar::BookOpenButton) bookShown_ = show;
        else if (cmd.var == ScriptVar::ChatScButton) scShown_ = show;
        else if (cmd.var == ScriptVar::BookNextArrow) nextShown_ = show;
        else if (cmd.var == ScriptVar::BookPrevArrow) prevShown_ = show;
        break;
    }
    case ScriptOp::ObscureBook:
        bookShown_ = cmd.numArgs > 0 && cmd.args[0] == 0.0f;
        break;
    case ScriptOp::ObscureLaffMeter:
        laffShown_ = cmd.numArgs > 0 && cmd.args[0] == 0.0f;
        break;
    case ScriptOp::ObscureChat: // chatMgr.obscure(normal, speedChat): the 3DS chat is SpeedChat
        scShown_ = cmd.numArgs > 1 && cmd.args[1] == 0.0f;
        break;
    case ScriptOp::ShowBook: // book.setPage(mapPage), enter, disableBookCloseButton
        bookScripted_ = true;
        closeEnabled_ = false;
        bookRequest_ = 1;
        break;
    case ScriptOp::EnableCloseBook:
        closeEnabled_ = true;
        break;
    case ScriptOp::HideBook:
        bookScripted_ = false;
        bookRequest_ = -1;
        break;
    case ScriptOp::ArrowsOn:
        arrows_ = true;
        break;
    case ScriptOp::LaffMeter: // LaffMeter.adjustFace(hp, maxHp): the face follows the lesson, not the Toon
        laffValue_ = true;
        laffHp_ = cmd.numArgs > 0 ? static_cast<int>(cmd.args[0]) : 15;
        laffMax_ = cmd.numArgs > 1 ? static_cast<int>(cmd.args[1]) : 15;
        break;
    case ScriptOp::ArrowsOff:
        arrows_ = false;
        break;
    case ScriptOp::FinishQuestMovie:
        clearLine();
        card_ = ScriptVar::None;
        break;
    case ScriptOp::ReparentTo:
    case ScriptOp::WrtReparentTo:
        if (cmd.var == ScriptVar::ClassicChar) charShown_ = cmd.var2 == ScriptVar::Render;
        if (cmd.var == ScriptVar::Npc) npcHidden_ = cmd.var2 == ScriptVar::Hidden;
        if (cmd.var == ScriptVar::Camera) {
            const bool staged = cmd.var2 == ScriptVar::Render || cmd.var2 == ScriptVar::Npc ||
                                cmd.var2 == ScriptVar::LocalToon;
            if (cmd.op == ScriptOp::ReparentTo || !staged || cameraParent_ == ScriptVar::None) {
                cameraWrt_ = false; // the local transform carries over as it is
            } else if (!cameraWrt_) {
                cameraWrtFrom_ = cameraParent_; // a second WRT before the caller took it: same frame
                cameraWrt_ = true;
            }
            cameraParent_ = staged ? cmd.var2 : ScriptVar::None;
        }
        if (isCard(cmd.var)) {
            if (cmd.var2 == ScriptVar::Camera) card_ = cmd.var;
            else if (card_ == cmd.var) card_ = ScriptVar::None;
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
        // UI-only commands (inventory highlights, music volume, toon heads) retain their order
        // in ScriptPlayer. The compact 3DS HUD has no Panda node equivalent.
        break;
    }
}

} // namespace tt::world
