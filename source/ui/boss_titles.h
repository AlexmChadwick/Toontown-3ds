// Player-facing titles for the boss Cogs' AI states (DistributedBossCog FSM names) on the
// bottom-screen info card; the raw state names ("PrepareBattleThree") read as debug text.
#pragma once

#include <cstring>

namespace tt::ui {

inline const char* bossPhaseTitle(const char* phase) {
    if (!phase || !phase[0]) return "Boss battle";
    static const struct {
        const char* state;
        const char* title;
    } kTitles[] = {
        {"Off", "Boss battle"},
        {"WaitForToons", "Waiting for Toons"},
        {"Elevator", "Going up!"},
        {"Introduction", "Here comes the boss!"},
        {"BattleOne", "Round 1"},
        {"RollToBattleTwo", "On to round 2"},
        {"PrepareBattleTwo", "Get ready for round 2"},
        {"BattleTwo", "Round 2"},
        {"RollToBattleThree", "On to round 3"},
        {"PrepareBattleThree", "Get ready for round 3"},
        {"BattleThree", "Round 3"},
        {"PrepareBattleFour", "Get ready for round 4"},
        {"BattleFour", "Round 4"},
        {"NearVictory", "Almost there!"},
        {"Victory", "Victory!"},
        {"Reward", "Your reward"},
        {"Epilogue", "Toons saved!"},
        {"Frolic", "Victory lap"},
        {"Defeat", "Defeated"},
    };
    for (const auto& t : kTitles)
        if (!std::strcmp(phase, t.state)) return t.title;
    return "Boss battle";
}

} // namespace tt::ui
