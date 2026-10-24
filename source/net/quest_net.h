// ToonTask wire formats (docs/design/quests.md section 3): our own quest list as the AI sends it,
// and DistributedNPCToon.setMovie. Pure parsing over DatagramReader, host-tested by
// tools/quest_test. tt::net::Client owns the instances (the "quests" section of client.h).
#pragma once

#include <cstdint>

#include "net/datagram.h"

namespace tt::net {

// DistributedToon.setQuests(uint32[]): groups of five, DistributedToon.setQuests upstream.
struct QuestEntry {
    uint32_t questId = 0;
    uint32_t fromNpc = 0;   // npcId, Quests.Any (1) or ToonHQ (1000)
    uint32_t toNpc = 0;
    uint32_t reward = 0;    // reward id, or Quests.NA (2): "the final reward of the chain"
    uint32_t progress = 0;  // kind-specific counter (RecoverItem packs a miss count in bits 16+)
};
constexpr int kMaxQuests = 8; // questCarryLimit is at most 4; room for a bad update

struct OwnQuests {
    QuestEntry quests[kMaxQuests];
    int count = 0;
    uint8_t carryLimit = 1;       // setQuestCarryLimit
    uint8_t rewardTier = 0;       // setRewardHistory (tier)
    uint16_t historyCount = 0;    // setQuestHistory: quests finished so far
    uint16_t rewardHistoryCount = 0;
    uint32_t version = 0;         // bumped on every change; UI re-renders text when it moves
};

// DistributedNPCToon / DistributedNPCSpecialQuestGiver setMovie(uint8 mode, uint32 npcId,
// uint32 avId, uint16[] quests, int16 timestamp). NPCToons.QUEST_MOVIE_*.
enum NpcMovieMode : uint8_t {
    kMovieClear = 0,
    kMovieReject = 1,
    kMovieComplete = 2,
    kMovieIncomplete = 3,
    kMovieAssign = 4,
    kMovieBusy = 5,
    kMovieQuestChoice = 6,
    kMovieQuestChoiceCancel = 7,
    kMovieTrackChoice = 8,
    kMovieTrackChoiceCancel = 9,
    kMovieTimeout = 10,
    kMovieTierNotDone = 11,
};
constexpr int kMaxMovieValues = 24; // 3 quests x 3 ids is the largest the AI sends
struct NpcMovie {
    uint32_t npcDoId = 0;   // the object the field arrived on
    uint8_t mode = 0;
    uint32_t npcId = 0;     // NPCToons id (2001, ...), not a doId
    uint32_t avId = 0;      // who the movie is for
    uint16_t values[kMaxMovieValues] = {};
    int numValues = 0;
    int16_t timestamp = 0;
};

// Each parser consumes exactly the field's payload; false (and a failed reader) on a short or
// malformed one. Quests beyond kMaxQuests are dropped.
bool parseQuests(DatagramReader& r, OwnQuests& out);        // setQuests
bool parseQuestHistory(DatagramReader& r, OwnQuests& out);  // setQuestHistory(uint16[])
bool parseRewardHistory(DatagramReader& r, OwnQuests& out); // setRewardHistory(uint8, uint16[])
bool parseNpcMovie(DatagramReader& r, NpcMovie& out);       // setMovie

} // namespace tt::net
