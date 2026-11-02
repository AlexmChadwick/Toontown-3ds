// Toontorial QuestScripts adapter.  It preserves QuestParser ordering and blocking semantics
// while presenting the script on the 3DS' existing speech/toast UI (docs/design/tutorial.md).
#pragma once

#include "quest/script_player.h"

namespace tt::world {

class TutorialFlow final : public quest::ScriptStage {
public:
    void start(quest::ScriptId id, bool femaleToon);
    void update(float dt);
    void advance();
    bool postEvent(const char* name);
    // Drops the movie. A movie still running is cut short first: its lerps end and its
    // UPON_TIMEOUT commands run (QuestParser NPCMoviePlayer.timeout via cleanupMovie).
    void stop();

    bool active() const { return player_.active(); }
    quest::ScriptId scriptId() const { return player_.id(); }
    const char* waitingEvent() const { return player_.waitingEvent(); }
    bool arrows() const { return arrows_; }
    // The speaker's current balloon page (setPageChat pages split at \x07).
    const char* text() const { return numPages_ ? pages_[page_] : ""; }
    // The current line was given "CFReversed" (Tom's 101 lines, Flippy's last four).
    bool reversed() const { return reversed_; }
    // A LOCAL_CHAT_PERSIST line the movie ended on (tutorial_mickey's "Come here!"): it stays up
    // over its speaker after the script until the player talks to him (dropPersist).
    bool persisting() const { return persist_ && numPages_ && !player_.active(); }
    void dropPersist();
    // The tutorial_gui card the script has parented to the camera (quest_assign_101's Cogs,
    // ToonBuilding, CogBuilding, Squirt1, Squirt2), else ScriptVar::None.
    quest::ScriptVar card() const { return card_; }

    // tutorial_mickey's staging: LOAD_CLASSIC_CHAR (Mickey, or Minnie for a girl Toon), its
    // REPARENTTO render / hidden, the LOOP_ANIM / PLAY_ANIM clips of it and of the local Toon,
    // POS/LERP of the local Toon (until FREE_LOCALTOON) and the camera parented to render.
    struct ActorAnim {
        char name[24] = {};
        bool loop = true;
        float rate = 1.0f;
        float start = 0.0f; // script time when the clip started
    };
    bool charShown() const { return charLoaded_ && charShown_; }
    bool femaleChar() const { return player_.femaleToon(); }
    const ActorAnim& charAnim() const { return charAnim_; }
    // LOCK_LOCALTOON's scripted moves: the Toon follows the script's pose every frame.
    bool toonPosed() const { return toonPosed_; }
    // Place the Toon once at its scripted pose, then it is free: FREE_LOCALTOON after a locked
    // run, or a POS / HPR of a Toon the script never locked (quest_assign_101's last pose before
    // FINISH_QUEST_MOVIE, tutorial_blocker's spot in front of Flippy).
    bool takeToonPlace() {
        const bool r = toonPlace_;
        toonPlace_ = false;
        return r;
    }
    bool toonHidden() const { return toonHidden_; } // HIDE localToon (tutorial_blocker)
    const ActorAnim& toonAnim() const { return toonAnim_; }
    bool cameraStaged() const { return cameraParent_ != quest::ScriptVar::None; }
    // The node the script parented the camera to (render, npc or localToon); the camera's
    // transform is xform(Camera) in that node's frame.
    quest::ScriptVar cameraParent() const { return cameraParent_; }
    // WRTREPARENTTO camera <node> ran: xform(Camera) is still in the old parent's frame
    // (*from); the caller re-expresses that world pose in the new parent's frame
    // (setCameraLocal) before the next frame is drawn.
    bool takeCameraWrt(quest::ScriptVar* from) {
        const bool r = cameraWrt_;
        cameraWrt_ = false;
        if (r && from) *from = cameraWrtFrom_;
        return r;
    }
    void setCameraLocal(const quest::ScriptXform& x) { xforms_[static_cast<int>(quest::ScriptVar::Camera)] = x; }
    // The script's NPC (tutorial_blocker's Flippy, parented to render): its pose as the script
    // moves it (seedNpc gives its pose when the movie starts), its LOOP_ANIM clip, and whether
    // REPARENTTO npc hidden ran.
    void seedNpc(const quest::ScriptXform& x) { xforms_[static_cast<int>(quest::ScriptVar::Npc)] = x; }
    bool npcPosed() const { return npcPosed_; }
    const ActorAnim& npcAnim() const { return npcAnim_; }
    bool npcHidden() const { return npcHidden_; }
    const quest::ScriptXform& xform(quest::ScriptVar var) const { return xforms_[static_cast<int>(var)]; }
    quest::ScriptVar speaker() const { return speaker_; } // who says text()
    float time() const { return time_; }
    bool takeFinished();

    // PlayGame.enterTutorialHood obscures the book button, the laff meter and both chat buttons
    // for the whole Toontorial; the scripts reveal them one lesson at a time (OBSCURE_* 0, SHOW)
    // and exitTutorialHood clears it all. Call every frame with net.inTutorial().
    void setInTutorialHood(bool in);
    bool bookButtonShown() const { return !hood_ || bookShown_; }
    bool laffShown() const { return !hood_ || laffShown_; }
    // quest_assign_101's Laff meter lesson: LAFFMETER hp max drains the meter 15 -> 0 -> 15 while
    // POS / SCALE / LERP laffMeter fly it from its corner (scale 0.075) to the middle (0.2) and back.
    // False when no lesson runs; *grow is 0 in the corner .. 1 at the middle.
    bool laffDemo(int* hp, int* maxHp, float* grow) const {
        if (!laffValue_ && !laffPosed_) return false;
        if (hp) *hp = laffValue_ ? laffHp_ : -1;
        if (maxHp) *maxHp = laffMax_;
        const float s = xforms_[static_cast<int>(quest::ScriptVar::LaffMeter)].scale[0];
        const float g = laffPosed_ ? (s - 0.075f) / (0.2f - 0.075f) : 0.0f;
        if (grow) *grow = g < 0.0f ? 0.0f : g > 1.0f ? 1.0f : g;
        return true;
    }
    bool speedChatShown() const { return !hood_ || scShown_; }
    // Harry's book lesson: SHOW_BOOK opens the book on the map page with its close button off
    // until ENABLE_CLOSE_BOOK; HIDE / SHOW bookNextArrow / bookPrevArrow gate the page turns.
    bool bookScripted() const { return bookScripted_; }
    bool bookCloseEnabled() const { return !bookScripted_ || closeEnabled_; }
    bool bookNextShown() const { return !bookScripted_ || nextShown_; }
    bool bookPrevShown() const { return !bookScripted_ || prevShown_; }
    // SHOW_BOOK (+1: open on the map page) / HIDE_BOOK (-1: close), once.
    int takeBookRequest() {
        const int r = bookRequest_;
        bookRequest_ = 0;
        return r;
    }

    bool getXform(quest::ScriptVar var, quest::ScriptXform* out) override;
    void setXform(quest::ScriptVar var, const quest::ScriptXform& x) override;
    void setColorScale(quest::ScriptVar var, const float rgba[4]) override;
    void run(const quest::ScriptCmd& cmd, const char* text) override;

private:
    static constexpr int kVars = static_cast<int>(quest::ScriptVar::Count);
    static constexpr int kMaxPages = 8;
    void setLine(const char* text);
    void clearLine();
    void endMovie(); // the movie is over: UPON_TIMEOUT cleanups, the Toon and camera handed back

    quest::ScriptPlayer player_;
    quest::ScriptXform xforms_[kVars] = {};
    float colors_[kVars][4] = {};
    char line_[256] = {};
    const char* pages_[kMaxPages] = {};
    int numPages_ = 0, page_ = 0;
    bool persist_ = false;
    bool reversed_ = false; // the current line's chat flag CFReversed
    bool arrows_ = false;
    quest::ScriptVar card_ = quest::ScriptVar::None;
    quest::ScriptVar speaker_ = quest::ScriptVar::None;
    quest::ScriptVar cameraParent_ = quest::ScriptVar::None;
    quest::ScriptVar cameraWrtFrom_ = quest::ScriptVar::None;
    bool charLoaded_ = false, charShown_ = false, toonPosed_ = false, toonPlace_ = false;
    bool toonLocked_ = false, toonHidden_ = false, cameraWrt_ = false;
    bool npcPosed_ = false, npcHidden_ = false;
    ActorAnim charAnim_, toonAnim_, npcAnim_;
    float time_ = 0.0f;
    bool finished_ = false;
    bool ended_ = true; // endMovie ran for the current movie
    bool hood_ = false, bookShown_ = true, laffShown_ = true, scShown_ = true;
    bool bookScripted_ = false, closeEnabled_ = true, nextShown_ = true, prevShown_ = true;
    int bookRequest_ = 0;
    bool laffValue_ = false, laffPosed_ = false;
    int laffHp_ = 15, laffMax_ = 15;
};

} // namespace tt::world
