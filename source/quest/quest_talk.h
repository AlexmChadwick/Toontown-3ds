// The conversation with a quest-giving NPC toon (DistributedNPCToon.setMovie and friends,
// docs/design/quests.md section 4). Pure state machine, no 3DS headers: tools/quest_test drives it
// on the host. The game feeds it movies from tt::net::Client, reads the page text / choices for the
// UI and sends whatever popAction() returns.
//
//   begin(npc)      walked into the NPC's sphere     -> AvatarEnter, phase Waiting (toon frozen)
//   onMovie(...)    the AI answered
//       assign / complete / incomplete / cancel      -> phase Pages: the NPC's lines, one page per tap
//       quest choice                                 -> phase QuestChoice (20 s, then cancel)
//       track choice                                 -> phase TrackChoice (20 s, then cancel)
//       reject / tier not done                       -> one line over the NPC, toon is free at once
//       assign 101 / 110 (quest_assign_* QuestScript) -> phase Script: takeScript() names it
//   advance()       past the last page               -> MovieDone, phase Idle
//   scriptDone()    the QuestScript finished            -> MovieDone, phase Idle
//   chooseQuest(i)                                   -> ChooseQuest(id or 0), phase Waiting
//   chooseTrack(i)                                   -> ChooseTrack(track or -1), phase Waiting
#pragma once

#include <cstdint>

#include "net/quest_net.h"
#include "quest/quest_text.h"

namespace tt::quest {

struct TalkAction {
    enum Kind : uint8_t { AvatarEnter, ChooseQuest, ChooseTrack, MovieDone } kind;
    uint32_t npcDoId;
    int value; // quest id / track
};

struct Choice {
    uint16_t questId, rewardId, toNpc;
};

class Talk {
public:
    enum class Phase : uint8_t { Idle, Waiting, Pages, QuestChoice, TrackChoice, Script };

    static constexpr int kMaxPages = 16;
    static constexpr int kPageChars = 200; // a page fits a speech balloon and the bottom panel
    static constexpr int kMaxChoices = 3;
    static constexpr int kMaxTracks = 7;
    static constexpr float kChoiceTimeoutSec = 20.0f; // DistributedNPCToon.ChoiceTimeout
    static constexpr float kWaitTimeoutSec = 6.0f;    // the AI never answered (busy NPC)
    static constexpr float kSpeechHoldSec = 10.0f;    // CFTimeout

    // Our avatar (setMovie.avId is matched against it) and name (_avName_ in the dialog).
    void setLocal(uint32_t avId, const char* name);
    void setSeed(uint32_t seed) { rng_ = seed ? seed : 1; }

    bool begin(uint32_t npcDoId);
    void onMovie(const net::NpcMovie& m);
    void onFree(uint32_t npcDoId);
    void onNpcGone(uint32_t npcDoId);
    void reset(); // zone change / disconnect: drop everything without sending anything
    void update(float dt);

    // UI side.
    void advance();
    void chooseQuest(int index); // -1 = cancel
    void chooseTrack(int index); // -1 = cancel
    // DistributedNPCToon.setMovie: an assign movie whose quest_assign_<id> script is defined plays
    // that QuestScript instead of the dialog pages.  takeScript() returns the quest id once (0 =
    // none); the line being said is fed back for the panel, and scriptDone() ends the movie.
    uint16_t takeScript();
    void setScriptLine(const char* text);
    const char* scriptLine() const { return scriptLine_; }
    int scriptSerial() const { return scriptSerial_; } // bumps on every new line
    void scriptAdvance() { scriptAdvance_ = phase_ == Phase::Script; } // the player dismissed the line
    bool takeScriptAdvance() {
        const bool a = scriptAdvance_;
        scriptAdvance_ = false;
        return a;
    }
    // The QuestScript's WAIT_EVENT (nullptr = none): Harry's lesson waits for the player to open
    // the Shticker Book, which then owns the bottom screen until he says to close it.
    void setScriptWait(const char* event) { scriptWait_ = event; }
    const char* scriptWait() const { return phase_ == Phase::Script ? scriptWait_ : nullptr; }
    void setScriptCard(int card) { scriptCard_ = card; } // ui::TutorialCard, -1 = none
    int scriptCard() const { return phase_ == Phase::Script ? scriptCard_ : -1; }
    void scriptDone();
    bool locked() const { return phase_ != Phase::Idle; }
    Phase phase() const { return phase_; }
    uint32_t npcDoId() const { return npcDoId_; }
    uint16_t npcId() const { return npcId_; }
    const char* npcName() const;
    // DistributedNPCToonBase.setName of the NPC in this talk: wins over the quest tables' name.
    void setServerName(const char* name);
    int numPages() const { return numPages_; }
    int page() const { return page_; }
    const char* pageText() const { return page_ < numPages_ ? pages_[page_] : ""; }
    bool onLastPage() const { return page_ + 1 >= numPages_; }
    int numChoices() const { return numChoices_; }
    const Choice& choice(int i) const { return choices_[i]; }
    int numTracks() const { return numTracks_; }
    int track(int i) const { return tracks_[i]; }
    float choiceTimeLeft() const { return timer_ > 0.0f ? timer_ : 0.0f; }
    // What the NPC is saying right now (balloon over its head); nullptr when silent.
    const char* speech(uint32_t* npcDoIdOut) const;
    bool popAction(TalkAction& out);

private:
    void setPhase(Phase p, float timer);
    void buildPages(const char* text);
    void say(const char* text, bool toPages);
    void post(TalkAction::Kind kind, int value);
    unsigned pick() {
        rng_ ^= rng_ << 13;
        rng_ ^= rng_ >> 17;
        rng_ ^= rng_ << 5;
        return rng_;
    }

    uint32_t avId_ = 0;
    char avName_[32] = {};
    Phase phase_ = Phase::Idle;
    uint32_t npcDoId_ = 0;
    uint16_t npcId_ = 0;
    float timer_ = 0.0f;
    uint32_t rng_ = 2463534242u;

    char text_[kMaxText] = {};
    const char* pages_[kMaxPages] = {};
    int numPages_ = 0, page_ = 0;
    Choice choices_[kMaxChoices] = {};
    int numChoices_ = 0;
    int8_t tracks_[kMaxTracks] = {};
    int numTracks_ = 0;
    uint16_t scriptQuest_ = 0;
    bool scriptPending_ = false;
    char scriptLine_[kPageChars * 2] = {};
    char serverName_[32] = {};
    int scriptSerial_ = 0;
    int scriptCard_ = -1;
    const char* scriptWait_ = nullptr;
    bool scriptAdvance_ = false;

    // A one-off line (reject, tier not done) shown without freezing the toon.
    char line_[kPageChars + 1] = {};
    uint32_t lineNpc_ = 0;
    float lineAge_ = 99.0f;

    static constexpr int kMaxActions = 8;
    TalkAction actions_[kMaxActions] = {};
    int actionHead_ = 0, actionCount_ = 0;
};

} // namespace tt::quest
