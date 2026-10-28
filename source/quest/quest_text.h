// ToonTask text: how Quests.py turns a quest id + progress into the strings the player reads
// (docs/design/quests.md section 5). Pure C++ with no 3DS headers, so tools/quest_test builds it
// on the host and compares every string against Python's own formatting.
//
// Every writer fills a caller buffer (always NUL terminated, truncated at cap - 1) and returns the
// string length. Nothing allocates.
#pragma once

#include <cstdint>

#include "quest/quest_data.h"

namespace tt::quest {

// Quests.py dialog statuses (QuestDict dialog dict keys), plus the NPC lines that are not part of
// DefaultDialog.
enum Status : int {
    kGreeting = 0,
    kQuest = 1,
    kIncomplete = 2,
    kIncompleteProgress = 3,
    kIncompleteWrongNpc = 4,
    kComplete = 5,
    kLeaving = 6,
    kReject = 100,      // chooseQuestDialogReject
    kTierNotDone = 101, // chooseQuestDialogTierNotDone
    kVisitDefault = 102 // VisitQuest.getDefaultQuestDialog
};

constexpr int kMaxText = 1536; // longest generated dialog plus _where_ expansion

// ---- table lookups (binary search; nullptr when unknown) ----
const QuestDef* findQuest(uint16_t id);
const RewardDef* findReward(uint16_t id);
const NpcDef* findNpc(uint16_t id);
const char* str(uint16_t index);
const char* npcName(uint16_t npcId); // nullptr for ids not in NPCToonDict (HQ officer included: 1000)

// ---- printf-style templates ----
// Upstream templates use Python's %(name)s / %(name)d / %s / %d. One Arg per value; positional
// conversions consume the args in order.
struct Arg {
    const char* key; // nullptr for positional
    const char* s;   // string value, or nullptr when numeric
    long n;
};
inline Arg argS(const char* key, const char* s) { return {key, s ? s : "", 0}; }
inline Arg argN(const char* key, long n) { return {key, nullptr, n}; }
int format(char* out, int cap, const char* tmpl, const Arg* args, int numArgs);

// ---- quest strings (Quest class methods) ----
bool isComplete(const QuestDef& q, uint32_t progress); // getCompletionStatus(...) == COMPLETE, no NPC
int numQuestItems(const QuestDef& q);                  // getNumQuestItems(), -1 for "no counter"
int locationName(const QuestDef& q, char* out, int cap);      // LocationBasedQuest.getLocationName
int objective(const QuestDef& q, char* out, int cap);         // getObjectiveStrings()[0]
int questString(const QuestDef& q, char* out, int cap);       // getString()
int chooseString(const QuestDef& q, char* out, int cap);      // getChooseString() / getPosterString()
int headline(const QuestDef& q, bool complete, char* out, int cap); // getHeadlineString / "COMPLETE"
int progressString(const QuestDef& q, uint32_t progress, char* out, int cap); // getProgressString
int defaultQuestDialog(const QuestDef& q, unsigned pick, char* out, int cap); // getDefaultQuestDialog
// Destination of an NPC for the poster ("Building\nStreet\nNeighborhood").
int npcDestination(uint16_t npcId, char* out, int cap);
int rewardText(uint16_t rewardId, bool poster, char* out, int cap); // Reward.getString / getPosterString
// Where a quest sends the toon next, as the quest poster prints it: the destination NPC's place,
// or the quest's cog location. Empty when there is nothing to say.
int posterInfo(const QuestDef& q, uint16_t toNpc, bool complete, char* out, int cap);

// ---- NPC dialog ----
// chooseQuestDialog(questId, status): the quest's own line for `status`, else the default.
// `pick` selects among alternatives (upstream: random.choice). Not yet name-filled.
int chooseDialog(uint16_t questId, int status, unsigned pick, char* out, int cap);
int defaultDialog(int status, unsigned pick, char* out, int cap);
// fillInQuestNames: _avName_, _toNpcName_, _where_, _buildingName_, _streetDesc_. avName may be null
// (kept as is), toNpc 0 = nothing to fill.
int fillInNames(const char* text, const char* avName, uint16_t fromNpc, uint16_t toNpc, char* out, int cap);

// ---- Quests.getNpcLocationDialog pieces (exposed for tests) ----
int branchZone(int32_t zone);   // ZoneUtil.getCanonicalBranchZone
int hoodOfZone(int32_t zone);   // ZoneUtil.getCanonicalHoodId
// Where a zone is, in the localizer's words: an interior's building title ("Loony Labs"), else
// the street ("Silly Street"), else the neighborhood ("Toontown Central"). nullptr when the zone
// is not a static place (dynamic facility / minigame zones, unknown ids).
const char* placeTitle(int32_t zone);

} // namespace tt::quest
