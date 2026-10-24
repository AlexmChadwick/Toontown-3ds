#include "net/quest_net.h"

namespace tt::net {

bool parseQuests(DatagramReader& r, OwnQuests& out) {
    const uint16_t bytes = r.u16();
    if (!r.ok() || bytes % 4 != 0 || bytes > r.remaining()) {
        r.fail();
        return false;
    }
    const int words = bytes / 4;
    int n = 0;
    QuestEntry list[kMaxQuests];
    for (int i = 0; i + 5 <= words; i += 5) {
        QuestEntry e;
        e.questId = r.u32();
        e.fromNpc = r.u32();
        e.toNpc = r.u32();
        e.reward = r.u32();
        e.progress = r.u32();
        if (n < kMaxQuests) list[n++] = e;
    }
    for (int i = words - words % 5; i < words; i++) r.u32(); // a partial trailing group is ignored
    if (!r.ok()) return false;
    for (int i = 0; i < n; i++) out.quests[i] = list[i];
    out.count = n;
    out.version++;
    return true;
}

bool parseQuestHistory(DatagramReader& r, OwnQuests& out) {
    const uint16_t bytes = r.u16();
    if (!r.ok() || bytes % 2 != 0 || bytes > r.remaining()) {
        r.fail();
        return false;
    }
    r.skip(bytes);
    out.historyCount = uint16_t(bytes / 2);
    out.version++;
    return r.ok();
}

bool parseRewardHistory(DatagramReader& r, OwnQuests& out) {
    out.rewardTier = r.u8();
    const uint16_t bytes = r.u16();
    if (!r.ok() || bytes % 2 != 0 || bytes > r.remaining()) {
        r.fail();
        return false;
    }
    r.skip(bytes);
    out.rewardHistoryCount = uint16_t(bytes / 2);
    out.version++;
    return r.ok();
}

bool parseNpcMovie(DatagramReader& r, NpcMovie& out) {
    out.mode = r.u8();
    out.npcId = r.u32();
    out.avId = r.u32();
    const uint16_t bytes = r.u16();
    if (!r.ok() || bytes % 2 != 0 || bytes > r.remaining()) {
        r.fail();
        return false;
    }
    const int n = bytes / 2;
    out.numValues = 0;
    for (int i = 0; i < n; i++) {
        const uint16_t v = r.u16();
        if (out.numValues < kMaxMovieValues) out.values[out.numValues++] = v;
    }
    out.timestamp = r.i16();
    return r.ok();
}

} // namespace tt::net
