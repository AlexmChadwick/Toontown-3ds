#include "quest/quest_text.h"

#include <cstdio>
#include <cstring>

namespace tt::quest {

namespace {

constexpr int kTmp = 640;
const char* const kOtherQuest = "Finish this ToonTask"; // QuestType::Other: no renderer yet
const char* const kOtherHeadline = "TASK";
const char* const kDefaultBuilding = "Toon Building"; // NPCToons.getBuildingTitle default

// Bounded string builder.
struct Buf {
    char* p;
    int cap;
    int len = 0;
    Buf(char* out, int c) : p(out), cap(c) {
        if (cap > 0) p[0] = 0;
    }
    void put(const char* s, int n) {
        for (int i = 0; i < n && len < cap - 1; i++) p[len++] = s[i];
        if (cap > 0) p[len] = 0;
    }
    void put(const char* s) { put(s, int(strlen(s))); }
    void put(char c) { put(&c, 1); }
};

int copyStr(char* out, int cap, const char* s) {
    Buf b(out, cap);
    b.put(s);
    return b.len;
}

template <typename T>
const T* bsearchId(const T* table, uint32_t n, uint16_t id) {
    uint32_t lo = 0, hi = n;
    while (lo < hi) {
        const uint32_t mid = (lo + hi) / 2;
        if (table[mid].id < id) lo = mid + 1;
        else hi = mid;
    }
    return lo < n && table[lo].id == id ? &table[lo] : nullptr;
}

const PlaceName* findPlace(const PlaceName* table, uint32_t n, int id) {
    if (id < 0 || id > 0xFFFF) return nullptr;
    return bsearchId(table, n, uint16_t(id));
}

const ItemDef* findItem(int id) { return id < 0 || id > 0xFFFF ? nullptr : bsearchId(kItems, kNumItems, uint16_t(id)); }

bool isLocationBased(QuestType t) {
    switch (t) {
    case QuestType::Cog:
    case QuestType::CogNewbie:
    case QuestType::CogLevel:
    case QuestType::CogTrack:
    case QuestType::Building:
    case QuestType::BuildingNewbie:
    case QuestType::RecoverItem:
        return true;
    default:
        return false;
    }
}

// CogQuest.getCogNameString (CogLevelQuest passes anyType: its getCogType() is Any).
const char* cogName(const QuestDef& q, bool anyType) {
    const int idx = anyType ? -1 : q.p[2];
    const bool one = q.p[1] == 1;
    if (idx < 0 || idx >= int(kNumCogs)) return str(one ? kS_Cog : kS_Cogs);
    return str(one ? kCogs[idx].singular : kCogs[idx].plural);
}

const char* trackOf(const uint16_t* names, int idx) { return idx >= 0 && idx < 4 ? str(names[idx]) : ""; }

// NewbieQuest.getString
int newbieString(int laff, const char* objective, char* out, int cap) {
    if (laff <= kNewbieHp) {
        const Arg one[] = {{nullptr, objective, 0}};
        return format(out, cap, str(kS_QuestsCogNewNewbieQuestObjective), one, 1);
    }
    const Arg a[] = {argN("laffPoints", laff), argS("objective", objective)};
    return format(out, cap, str(kS_QuestsCogOldNewbieQuestObjective), a, 2);
}

void capwords(const char* in, char* out, int cap) {
    Buf b(out, cap);
    bool newWord = true, any = false;
    for (const char* c = in; *c; c++) {
        if (*c == ' ' || *c == '\n' || *c == '\t') {
            newWord = true;
            continue;
        }
        if (newWord && any) b.put(' ');
        char ch = *c;
        if (newWord) {
            if (ch >= 'a' && ch <= 'z') ch = char(ch - 32);
        } else if (ch >= 'A' && ch <= 'Z') {
            ch = char(ch + 32);
        }
        b.put(ch);
        newWord = false;
        any = true;
    }
}

// ---- NPC places (Quests.getNpcInfo) ----
struct NpcInfo {
    const char* name;
    const char* hood;
    const char* buildingArticle;
    const char* buildingName;
    const char* toStreet;
    const char* streetName;
    int branch;
    bool inPlayground;
};

const ZoneTitle* findTitle(int32_t zone) {
    if (zone < 0 || zone > 0xFFFF) return nullptr;
    const uint16_t z = uint16_t(zone);
    uint32_t lo = 0, hi = kNumZoneTitles;
    while (lo < hi) {
        const uint32_t mid = (lo + hi) / 2;
        if (kZoneTitles[mid].zone < z) lo = mid + 1;
        else hi = mid;
    }
    return lo < kNumZoneTitles && kZoneTitles[lo].zone == z ? &kZoneTitles[lo] : nullptr;
}

bool npcInfo(uint16_t npcId, NpcInfo* info) {
    const NpcDef* n = findNpc(npcId);
    if (!n || n->zone <= 0) return false;
    const PlaceName* hood = findPlace(kHoodNames, kNumHoodNames, hoodOfZone(n->zone));
    const int branch = branchZone(n->zone);
    const PlaceName* street = findPlace(kStreetNames, kNumStreetNames, branch);
    const ZoneTitle* title = findTitle(n->zone);
    info->name = str(n->name);
    info->hood = hood ? str(hood->name) : "";
    info->buildingArticle = title ? str(title->article) : "";
    info->buildingName = title ? str(title->title) : kDefaultBuilding;
    info->toStreet = street ? str(street->to) : "";
    info->streetName = street ? str(street->name) : "";
    info->branch = branch;
    info->inPlayground = branch % 1000 == 0;
    return true;
}

// Quests.getNpcLocationDialog -> where paragraph / building name / street description.
bool npcLocation(uint16_t fromNpc, uint16_t toNpc, char* where, int whereCap, char* building, int buildingCap,
                 char* street, int streetCap) {
    NpcInfo to;
    if (!toNpc || !npcInfo(toNpc, &to)) return false;
    int fromBranch = -1;
    if (fromNpc) {
        const NpcDef* f = findNpc(fromNpc);
        if (f && f->zone > 0) fromBranch = branchZone(f->zone);
    }
    if (fromBranch == to.branch) {
        format(street, streetCap, str(to.inPlayground ? kS_QuestsStreetLocationThisPlayground
                                                       : kS_QuestsStreetLocationThisStreet), nullptr, 0);
    } else if (to.inPlayground) {
        const Arg a[] = {{nullptr, to.hood, 0}};
        format(street, streetCap, str(kS_QuestsStreetLocationNamedPlayground), a, 1);
    } else {
        const Arg a[] = {argS("toStreetName", to.streetName), argS("toHoodName", to.hood)};
        format(street, streetCap, str(kS_QuestsStreetLocationNamedStreet), a, 2);
    }
    snprintf(building, size_t(buildingCap), "%s", to.buildingName);
    char bl[kTmp / 2];
    const Arg ba[] = {{nullptr, to.name, 0}};
    format(bl, sizeof(bl), str(kS_QuestsLocationBuilding), ba, 1);
    const Arg wa[] = {argS("building", bl), argS("buildingName", to.buildingName),
                      argS("buildingVerb", str(kS_QuestsLocationBuildingVerb)), argS("street", street)};
    format(where, whereCap, str(kS_QuestsLocationParagraph), wa, 4);
    return true;
}

void replaceAll(const char* in, const char* token, const char* with, char* out, int cap) {
    Buf b(out, cap);
    const size_t tl = strlen(token);
    for (const char* c = in; *c;) {
        if (!strncmp(c, token, tl)) {
            b.put(with);
            c += tl;
        } else {
            b.put(*c++);
        }
    }
}

} // namespace

// ============================================================ lookups
const QuestDef* findQuest(uint16_t id) { return bsearchId(kQuests, kNumQuests, id); }
const RewardDef* findReward(uint16_t id) { return bsearchId(kRewards, kNumRewards, id); }
const NpcDef* findNpc(uint16_t id) { return bsearchId(kNpcs, kNumNpcs, id); }
const char* str(uint16_t index) { return index < kNumStrings ? kStrings[index] : ""; }
const char* npcName(uint16_t npcId) {
    const NpcDef* n = findNpc(npcId);
    return n ? str(n->name) : nullptr;
}

int branchZone(int32_t zone) {
    if (zone < 0) return 0;
    int b = zone - zone % 100;
    if (zone % 1000 >= 500) b -= 500;
    return b;
}
int hoodOfZone(int32_t zone) { return zone < 0 ? 0 : zone - zone % 1000; }

int placeTitle(int32_t zone, char* out, int cap) {
    const PlaceName* hood = findPlace(kHoodNames, kNumHoodNames, hoodOfZone(zone));
    if (!hood) return copyStr(out, cap, "");
    const int branch = branchZone(zone);
    const PlaceName* street = branch % 1000 ? findPlace(kStreetNames, kNumStreetNames, branch) : nullptr;
    if (!street) return copyStr(out, cap, str(hood->name));
    const int n = snprintf(out, size_t(cap), "%s, %s", str(street->name), str(hood->name));
    return n < 0 ? 0 : (n < cap ? n : cap - 1);
}

// ============================================================ format
int format(char* out, int cap, const char* tmpl, const Arg* args, int numArgs) {
    Buf b(out, cap);
    int nextPositional = 0;
    for (const char* c = tmpl; *c; c++) {
        if (*c != '%') {
            b.put(*c);
            continue;
        }
        c++;
        if (*c == '%') {
            b.put('%');
            continue;
        }
        const Arg* a = nullptr;
        if (*c == '(') {
            const char* close = strchr(c, ')');
            if (!close) break;
            const size_t kl = size_t(close - c - 1);
            for (int i = 0; i < numArgs; i++) {
                if (args[i].key && strlen(args[i].key) == kl && !strncmp(args[i].key, c + 1, kl)) a = &args[i];
            }
            c = close + 1; // the conversion letter
        } else {
            while (*c == '-' || (*c >= '0' && *c <= '9')) c++; // widths are not used upstream
            if (nextPositional < numArgs) a = &args[nextPositional++];
        }
        if (!*c) break;
        if (a) {
            if (a->s) {
                b.put(a->s);
            } else {
                char num[24];
                snprintf(num, sizeof(num), "%ld", a->n);
                b.put(num);
            }
        }
    }
    return b.len;
}

// ============================================================ quest strings
bool isComplete(const QuestDef& q, uint32_t progress) {
    switch (q.type) {
    case QuestType::Cog:
    case QuestType::CogNewbie:
    case QuestType::CogLevel:
    case QuestType::CogTrack:
    case QuestType::Building:
    case QuestType::BuildingNewbie:
        return progress >= uint32_t(q.p[1]);
    case QuestType::RecoverItem:
        return (progress & 0xFFFFu) >= uint32_t(q.p[1]);
    case QuestType::Friend:
    case QuestType::Trolley:
    case QuestType::Phone:
    case QuestType::Mailbox:
        return progress >= 1;
    default:
        return false; // Visit / Deliver*: complete only at the NPC; TrackChoice, Other: unknown
    }
}

int numQuestItems(const QuestDef& q) {
    switch (q.type) {
    case QuestType::Cog:
    case QuestType::CogNewbie:
    case QuestType::CogLevel:
    case QuestType::CogTrack:
    case QuestType::Building:
    case QuestType::BuildingNewbie:
    case QuestType::RecoverItem:
        return q.p[1];
    case QuestType::DeliverGag:
        return q.p[0];
    default:
        return -1;
    }
}

int locationName(const QuestDef& q, char* out, int cap) {
    Buf none(out, cap);
    const int loc = q.p[0];
    if (!isLocationBased(q.type) || loc == kAnywhere) return 0;
    const PlaceName* pl = findPlace(kHoodNames, kNumHoodNames, loc);
    if (!pl) pl = findPlace(kStreetNames, kNumStreetNames, loc);
    if (!pl) return 0;
    char locStr[kTmp / 2];
    snprintf(locStr, sizeof(locStr), "%s%s", str(pl->name), str(kS_QuestsLocationArticle));
    const Arg a[] = {argS("inPhrase", str(pl->in)), argS("location", locStr)};
    return format(out, cap, str(kS_QuestInLocationString), a, 2);
}

namespace {

// RecoverItemQuest.getObjectiveStrings: [item, holder]
void recoverStrings(const QuestDef& q, char* item, int itemCap, char* holder, int holderCap) {
    const ItemDef* it = findItem(q.p[2]);
    const int num = q.p[1];
    if (it) {
        if (num == 1) {
            snprintf(item, size_t(itemCap), "%s%s", str(it->article), str(it->name));
        } else {
            const Arg a[] = {argN("num", num), argS("name", str(it->plural))};
            format(item, itemCap, str(kS_QuestsItemNameAndNum), a, 2);
        }
    } else {
        item[0] = 0;
    }
    switch (q.p[4]) {
    case 0: snprintf(holder, size_t(holderCap), "%s", str(kS_TheCogs)); break;
    case 1: snprintf(holder, size_t(holderCap), "%s", str(kS_AFish)); break;
    case 2:
        snprintf(holder, size_t(holderCap), "%s",
                 q.p[5] >= 0 && q.p[5] < int(kNumCogs) ? str(kCogs[q.p[5]].plural) : "");
        break;
    case 3: {
        const Arg a[] = {argS("level", str(kS_Level)), argN("holder", q.p[5]), argS("cogs", str(kS_Cogs))};
        format(holder, holderCap, str(kS_QuestsRecoverItemQuestHolderString), a, 3);
        break;
    }
    default: snprintf(holder, size_t(holderCap), "%s", trackOf(kA_CogTrackP, q.p[5])); break;
    }
}

} // namespace

int objective(const QuestDef& q, char* out, int cap) {
    Buf none(out, cap);
    switch (q.type) {
    case QuestType::Cog:
    case QuestType::CogNewbie: {
        const char* name = cogName(q, false);
        if (q.p[1] == 1) return copyStr(out, cap, name);
        const Arg a[] = {argN("numCogs", q.p[1]), argS("cogName", name)};
        return format(out, cap, str(kS_QuestsCogQuestDefeatDesc), a, 2);
    }
    case QuestType::CogLevel: {
        const Arg a[] = {argN("count", q.p[1]), argN("level", q.p[2]), argS("name", cogName(q, true))};
        return format(out, cap, str(q.p[1] == 1 ? kS_QuestsCogLevelQuestDesc : kS_QuestsCogLevelQuestDescC), a, 3);
    }
    case QuestType::CogTrack: {
        if (q.p[1] == 1) return copyStr(out, cap, trackOf(kA_CogTrackS, q.p[2]));
        const Arg a[] = {argN("numCogs", q.p[1]), argS("trackName", trackOf(kA_CogTrackP, q.p[2]))};
        return format(out, cap, str(kS_QuestsCogTrackDefeatDesc), a, 2);
    }
    case QuestType::Building:
    case QuestType::BuildingNewbie: {
        const int floorIdx = q.p[3] - 1;
        const char* floors = floorIdx >= 0 && floorIdx < 5 ? str(kA_BuildingFloors[floorIdx]) : "";
        const char* type = q.p[2] < 0 ? str(kS_Cog) : trackOf(kA_CogTrackName, q.p[2]);
        const bool noFloors = floors[0] == 0;
        const uint16_t t = q.p[1] == 1 ? (noFloors ? kS_QuestsBuildingQuestDesc : kS_QuestsBuildingQuestDescF)
                                       : (noFloors ? kS_QuestsBuildingQuestDescC : kS_QuestsBuildingQuestDescCF);
        const Arg a[] = {argN("count", q.p[1]), argS("floors", floors), argS("type", type)};
        return format(out, cap, str(t), a, 3);
    }
    case QuestType::DeliverItem: {
        const ItemDef* it = findItem(q.p[0]);
        if (!it) return 0;
        char tmp[kTmp / 4];
        snprintf(tmp, sizeof(tmp), "%s%s", str(it->article), str(it->name));
        return copyStr(out, cap, tmp);
    }
    case QuestType::RecoverItem: {
        char item[kTmp / 4], holder[kTmp / 4];
        recoverStrings(q, item, sizeof(item), holder, sizeof(holder));
        return copyStr(out, cap, item);
    }
    case QuestType::DeliverGag: {
        const int track = q.p[1], level = q.p[2];
        if (track < 0 || track >= 7 || level < 0 || level >= 7) return 0;
        const GagName& g = kGags[track * 7 + level];
        if (q.p[0] == 1) return copyStr(out, cap, str(g.singular));
        const Arg a[] = {argN("num", q.p[0]), argS("name", str(g.plural))};
        return format(out, cap, str(kS_QuestsItemNameAndNum), a, 2);
    }
    case QuestType::TrackChoice:
        return copyStr(out, cap, q.p[0] >= 0 && q.p[0] < 7 ? str(kA_TrackNameCap[q.p[0]]) : "");
    case QuestType::Friend: return copyStr(out, cap, str(kS_QuestsFriendQuestString));
    case QuestType::Trolley: return copyStr(out, cap, str(kS_QuestsTrolleyQuestString));
    case QuestType::Phone: return copyStr(out, cap, str(kS_QuestsPhoneQuestString));
    case QuestType::Mailbox: return copyStr(out, cap, str(kS_QuestsMailboxQuestString));
    case QuestType::Visit:
    case QuestType::Other:
        break;
    }
    return 0;
}

int questString(const QuestDef& q, char* out, int cap) {
    char obj[kTmp / 2];
    objective(q, obj, sizeof(obj));
    const Arg one[] = {{nullptr, obj, 0}};
    switch (q.type) {
    case QuestType::Cog: return format(out, cap, str(kS_QuestsCogQuestDefeat), one, 1);
    case QuestType::CogNewbie: return newbieString(q.p[3], obj, out, cap);
    case QuestType::CogLevel: return format(out, cap, str(kS_QuestsCogLevelQuestDefeat), one, 1);
    case QuestType::CogTrack: return format(out, cap, str(kS_QuestsCogTrackQuestDefeat), one, 1);
    case QuestType::Building: return format(out, cap, str(kS_QuestsBuildingQuestString), one, 1);
    case QuestType::BuildingNewbie: return newbieString(q.p[4], obj, out, cap);
    case QuestType::Visit: return copyStr(out, cap, str(kS_QuestsVisitQuestStringShort));
    case QuestType::DeliverItem: return format(out, cap, str(kS_QuestsDeliverItemQuestString), one, 1);
    case QuestType::RecoverItem: {
        char item[kTmp / 4], holder[kTmp / 4];
        recoverStrings(q, item, sizeof(item), holder, sizeof(holder));
        const Arg a[] = {argS("item", item), argS("holder", holder)};
        return format(out, cap, str(kS_QuestsRecoverItemQuestString), a, 2);
    }
    case QuestType::DeliverGag: return format(out, cap, str(kS_QuestsDeliverGagQuestString), one, 1);
    case QuestType::TrackChoice: {
        const Arg a[] = {argS("trackA", q.p[0] >= 0 && q.p[0] < 7 ? str(kA_TrackNameCap[q.p[0]]) : ""),
                         argS("trackB", q.p[1] >= 0 && q.p[1] < 7 ? str(kA_TrackNameCap[q.p[1]]) : "")};
        return format(out, cap, str(kS_QuestsTrackChoiceQuestString), a, 2);
    }
    case QuestType::Friend:
    case QuestType::Trolley: // upstream TrolleyQuest.getString returns the friend string
        return copyStr(out, cap, str(kS_QuestsFriendQuestString));
    case QuestType::Phone: return copyStr(out, cap, str(kS_QuestsPhoneQuestString));
    case QuestType::Mailbox: return copyStr(out, cap, str(kS_QuestsMailboxQuestString));
    case QuestType::Other: break;
    }
    return copyStr(out, cap, kOtherQuest);
}

int chooseString(const QuestDef& q, char* out, int cap) {
    if (q.type == QuestType::Visit) {
        return copyStr(out, cap, str(kS_QuestsVisitQuestStringLong));
    }
    char s[kTmp];
    questString(q, s, sizeof(s));
    if (!isLocationBased(q.type)) return copyStr(out, cap, s);
    char loc[kTmp / 2];
    locationName(q, loc, sizeof(loc));
    const Arg a[] = {argS("string", s), argS("location", loc)};
    return format(out, cap, str(kS_QuestsLocationString), a, 2);
}

int headline(const QuestDef& q, bool complete, char* out, int cap) {
    const char* h = kOtherHeadline;
    if (complete) {
        h = str(kS_QuestPosterComplete);
    } else {
        switch (q.type) {
        case QuestType::Cog: h = str(kS_QuestsCogQuestHeadline); break;
        case QuestType::CogNewbie:
        case QuestType::BuildingNewbie: h = str(kS_QuestsNewbieQuestHeadline); break;
        case QuestType::CogLevel: h = str(kS_QuestsCogLevelQuestHeadline); break;
        case QuestType::CogTrack: h = str(kS_QuestsCogTrackQuestHeadline); break;
        case QuestType::Building: h = str(kS_QuestsBuildingQuestHeadline); break;
        case QuestType::Visit: h = str(kS_QuestsVisitQuestHeadline); break;
        case QuestType::DeliverItem: h = str(kS_QuestsDeliverItemQuestHeadline); break;
        case QuestType::RecoverItem: h = str(kS_QuestsRecoverItemQuestHeadline); break;
        case QuestType::DeliverGag: h = str(kS_QuestsDeliverGagQuestHeadline); break;
        case QuestType::TrackChoice: h = str(kS_QuestsTrackChoiceQuestHeadline); break;
        case QuestType::Friend: h = str(kS_QuestsFriendQuestHeadline); break;
        case QuestType::Trolley: h = str(kS_QuestsTrolleyQuestHeadline); break;
        case QuestType::Phone: h = str(kS_QuestsPhoneQuestHeadline); break;
        case QuestType::Mailbox: h = str(kS_QuestsMailboxQuestHeadline); break;
        case QuestType::Other: break;
        }
    }
    return copyStr(out, cap, h);
}

int progressString(const QuestDef& q, uint32_t progress, char* out, int cap) {
    Buf none(out, cap);
    if (isComplete(q, progress)) return copyStr(out, cap, str(kS_QuestsCompleteString));
    const int n = q.p[1];
    uint16_t tmpl = 0;
    const char* nkey = "numCogs";
    long shown = long(progress);
    switch (q.type) {
    case QuestType::Cog:
    case QuestType::CogNewbie: tmpl = kS_QuestsCogQuestProgress; break;
    case QuestType::CogLevel: tmpl = kS_QuestsCogLevelQuestProgress; break;
    case QuestType::CogTrack: tmpl = kS_QuestsCogTrackQuestProgress; break;
    case QuestType::Building:
    case QuestType::BuildingNewbie:
        tmpl = kS_QuestsBuildingQuestProgressString;
        nkey = "num";
        break;
    case QuestType::RecoverItem:
        tmpl = kS_QuestsRecoverItemQuestProgress;
        nkey = "numItems";
        shown = long(progress & 0xFFFFu);
        break;
    case QuestType::DeliverGag: {
        if (q.p[0] == 1) return 0;
        const Arg a[] = {argN("progress", long(progress)), argN("numGags", q.p[0])};
        return format(out, cap, str(kS_QuestsDeliverGagQuestProgress), a, 2);
    }
    case QuestType::TrackChoice:
        return copyStr(out, cap, str(kS_QuestsNotChosenString));
    default:
        return 0; // Visit / DeliverItem progress strings are empty; Friend... are empty until done
    }
    if (n == 1) return 0;
    const Arg a[] = {argN("progress", shown), argN(nkey, n)};
    return format(out, cap, str(tmpl), a, 2);
}

int defaultQuestDialog(const QuestDef& q, unsigned pick, char* out, int cap) {
    if (q.type == QuestType::Visit) return defaultDialog(kVisitDefault, pick, out, cap);
    char tmp[kTmp];
    if (q.type == QuestType::DeliverItem || q.type == QuestType::DeliverGag) {
        char obj[kTmp / 2];
        objective(q, obj, sizeof(obj));
        const Arg one[] = {{nullptr, obj, 0}};
        format(tmp, sizeof(tmp),
               str(q.type == QuestType::DeliverItem ? kS_QuestsDeliverItemQuestStringLong
                                                    : kS_QuestsDeliverGagQuestStringLong),
               one, 1);
        if (q.type == QuestType::DeliverGag) {
            Buf b(out, cap);
            b.put(tmp);
            b.put('\a');
            b.put(str(kS_QuestsDeliverGagQuestInstructions));
            return b.len;
        }
        return copyStr(out, cap, tmp);
    }
    // LocationBasedQuest: (QuestsLocationString + Period) % {...}; Quest: getString() + Period
    chooseString(q, tmp, sizeof(tmp));
    Buf b(out, cap);
    b.put(tmp);
    b.put(str(kS_Period));
    return b.len;
}

int npcDestination(uint16_t npcId, char* out, int cap) {
    const char *b, *s, *h;
    NpcInfo info;
    if (npcId == kToonHQ) {
        b = str(kS_QuestPosterHQBuildingName);
        s = str(kS_QuestPosterHQStreetName);
        h = str(kS_QuestPosterHQLocationName);
    } else if (npcId == kToonTailor) {
        b = str(kS_QuestPosterTailorBuildingName);
        s = str(kS_QuestPosterTailorStreetName);
        h = str(kS_QuestPosterTailorLocationName);
    } else if (npcInfo(npcId, &info)) {
        b = info.buildingName;
        s = info.streetName;
        h = info.hood;
    } else {
        Buf none(out, cap);
        return 0;
    }
    const Arg a[] = {{nullptr, b, 0}, {nullptr, s, 0}, {nullptr, h, 0}};
    return format(out, cap, str(kS_QuestPageDestination), a, 3);
}

int rewardText(uint16_t rewardId, bool poster, char* out, int cap) {
    Buf none(out, cap);
    const RewardDef* r = findReward(rewardId);
    if (!r) return 0;
    return copyStr(out, cap, str(poster ? r->poster : r->text));
}

int posterInfo(const QuestDef& q, uint16_t toNpc, bool complete, char* out, int cap) {
    Buf none(out, cap);
    const bool npcBased = q.type == QuestType::Visit || q.type == QuestType::DeliverItem ||
                          q.type == QuestType::DeliverGag || q.type == QuestType::TrackChoice;
    if (complete || npcBased) return npcDestination(toNpc, out, cap);
    char loc[kTmp / 2];
    locationName(q, loc, sizeof(loc));
    char cw[kTmp / 2];
    capwords(loc, cw, sizeof(cw));
    if (!cw[0] && isLocationBased(q.type)) {
        return copyStr(out, cap, str(kS_QuestPosterAnywhere));
    }
    return copyStr(out, cap, cw);
}

// ============================================================ dialog
namespace {

const DialogRef* findDialog(uint16_t questId, int status) {
    uint32_t lo = 0, hi = kNumDialogs;
    while (lo < hi) {
        const uint32_t mid = (lo + hi) / 2;
        const DialogRef& d = kDialogs[mid];
        if (d.questId < questId || (d.questId == questId && d.status < status)) lo = mid + 1;
        else hi = mid;
    }
    if (lo < kNumDialogs && kDialogs[lo].questId == questId && kDialogs[lo].status == status) return &kDialogs[lo];
    return nullptr;
}

int copyOption(const DialogRef& d, unsigned pick, char* out, int cap) {
    const unsigned i = d.count ? pick % d.count : 0;
    return copyStr(out, cap, str(kDialogStrings[d.first + i]));
}

} // namespace

int defaultDialog(int status, unsigned pick, char* out, int cap) {
    if (const DialogRef* d = findDialog(0, status)) return copyOption(*d, pick, out, cap);
    Buf none(out, cap);
    return 0;
}

int chooseDialog(uint16_t questId, int status, unsigned pick, char* out, int cap) {
    if (const DialogRef* d = findDialog(questId, status)) return copyOption(*d, pick, out, cap);
    if (status == kQuest) {
        if (const QuestDef* q = findQuest(questId)) return defaultQuestDialog(*q, pick, out, cap);
    }
    return defaultDialog(status, pick, out, cap);
}

int fillInNames(const char* text, const char* avName, uint16_t fromNpc, uint16_t toNpc, char* out, int cap) {
    char a[kMaxText], b[kMaxText];
    snprintf(a, sizeof(a), "%s", text ? text : "");
    if (avName) {
        replaceAll(a, "_avName_", avName, b, sizeof(b));
        memcpy(a, b, sizeof(a));
    }
    if (toNpc) {
        char name[kTmp / 4], where[kTmp], building[kTmp / 4], street[kTmp / 4];
        name[0] = where[0] = building[0] = street[0] = 0;
        if (toNpc == kToonHQ) {
            snprintf(name, sizeof(name), "%s", str(kS_QuestsHQOfficerFillin));
            snprintf(where, sizeof(where), "%s", str(kS_QuestsHQWhereFillin));
            snprintf(building, sizeof(building), "%s", str(kS_QuestsHQBuildingNameFillin));
            snprintf(street, sizeof(street), "%s", str(kS_QuestsHQLocationNameFillin));
        } else if (toNpc == kToonTailor) {
            snprintf(name, sizeof(name), "%s", str(kS_QuestsTailorFillin));
            snprintf(where, sizeof(where), "%s", str(kS_QuestsTailorWhereFillin));
            snprintf(building, sizeof(building), "%s", str(kS_QuestsTailorBuildingNameFillin));
            snprintf(street, sizeof(street), "%s", str(kS_QuestsTailorLocationNameFillin));
        } else {
            const char* n = npcName(toNpc);
            snprintf(name, sizeof(name), "%s", n ? n : "a Toon");
            npcLocation(fromNpc, toNpc, where, sizeof(where), building, sizeof(building), street, sizeof(street));
        }
        replaceAll(a, "_toNpcName_", name, b, sizeof(b));
        replaceAll(b, "_where_", where, a, sizeof(a));
        replaceAll(a, "_buildingName_", building, b, sizeof(b));
        replaceAll(b, "_streetDesc_", street, a, sizeof(a));
    }
    Buf o(out, cap);
    o.put(a);
    return o.len;
}

} // namespace tt::quest
