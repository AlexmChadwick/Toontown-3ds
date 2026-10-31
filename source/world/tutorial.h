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
    bool finished_ = false;
};

} // namespace tt::world
