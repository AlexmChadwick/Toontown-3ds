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
    void stop();

    bool active() const { return player_.active(); }
    const char* waitingEvent() const { return player_.waitingEvent(); }
    bool arrows() const { return arrows_; }
    const char* text() const { return text_; }
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
    bool toonPosed() const { return toonPosed_; }
    // FREE_LOCALTOON ran: place the Toon once more at its last scripted pose, then hand it back.
    bool takeToonRelease() {
        const bool r = toonReleased_;
        toonReleased_ = false;
        return r;
    }
    const ActorAnim& toonAnim() const { return toonAnim_; }
    bool cameraStaged() const { return cameraStaged_; }
    const quest::ScriptXform& xform(quest::ScriptVar var) const { return xforms_[static_cast<int>(var)]; }
    quest::ScriptVar speaker() const { return speaker_; } // who says text()
    float time() const { return time_; }
    bool takeFinished();

    bool getXform(quest::ScriptVar var, quest::ScriptXform* out) override;
    void setXform(quest::ScriptVar var, const quest::ScriptXform& x) override;
    void setColorScale(quest::ScriptVar var, const float rgba[4]) override;
    void run(const quest::ScriptCmd& cmd, const char* text) override;

private:
    static constexpr int kVars = static_cast<int>(quest::ScriptVar::Count);
    quest::ScriptPlayer player_;
    quest::ScriptXform xforms_[kVars] = {};
    float colors_[kVars][4] = {};
    char text_[256] = {};
    bool arrows_ = false;
    quest::ScriptVar card_ = quest::ScriptVar::None;
    quest::ScriptVar speaker_ = quest::ScriptVar::None;
    bool charLoaded_ = false, charShown_ = false, toonPosed_ = false, toonLocked_ = false, cameraStaged_ = false, toonReleased_ = false;
    ActorAnim charAnim_, toonAnim_;
    float time_ = 0.0f;
    bool finished_ = false;
};

} // namespace tt::world
