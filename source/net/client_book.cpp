// Shtiker Book field unpacking implementation (docs/design/shtiker_book.md).
#include "net/book_net.h"
#include "net/dc_generated.h"

namespace tt::net {

namespace {

// Upstream GolfGlobals.TrophyRequirements thresholds for history[0..9]
constexpr uint16_t kGolfReqs[10][3] = {
    { 4, 40, 400 },   // CoursesCompleted
    { 1, 10, 100 },   // CoursesUnderPar
    { 1, 10, 100 },   // HoleInOneShots
    { 2, 20, 200 },   // EagleOrBetterShots
    { 3, 30, 300 },   // BirdieOrBetterShots
    { 4, 40, 400 },   // ParOrBetterShots
    { 6, 60, 600 },   // MultiPlayerCoursesCompleted
    { 1, 10, 100 },   // CourseZeroWins
    { 1, 10, 100 },   // CourseOneWins
    { 1, 10, 100 },   // CourseTwoWins
};

} // namespace

void OwnBookStats::initDefaults() {
    trackProgressId = -1;
    trackProgress = 0;
    for (int i = 0; i < kNumCogs; i++) {
        cogStatus[i] = kCogUnseen;
        cogCount[i] = 0;
    }
    for (int i = 0; i < kNumDepts; i++) {
        cogRadar[i] = 0;
        buildingRadar[i] = 0;
        cogParts[i] = 0;
        cogMerits[i] = 0;
        cogTypes[i] = 0;
        cogLevels[i] = 0;
    }
    numFishCollected = 0;
    numFishingTrophies = 0;
    for (auto& f : fishCollection) f = {};
    memset(fishingTrophies, 0, sizeof(fishingTrophies));
    memset(kartingTrophies, 0, sizeof(kartingTrophies));
    tickets = 0;
    memset(kartingHistory, 0, sizeof(kartingHistory));
    memset(golfHistory, 0, sizeof(golfHistory));
    memset(golfHoleBest, 0, sizeof(golfHoleBest));
    memset(golfCourseBest, 0, sizeof(golfCourseBest));
    numSosCards = 0;
    for (auto& s : sosCards) s = {};
    version = 1;
}

int calcGolfTrophies(const uint16_t history[kGolfHistory], bool trophiesOut[kGolfTrophies]) {
    int won = 0;
    for (int cat = 0; cat < 10; cat++) {
        for (int t = 0; t < 3; t++) {
            const int idx = cat * 3 + t;
            const bool got = history[cat] >= kGolfReqs[cat][t];
            if (trophiesOut) trophiesOut[idx] = got;
            if (got) won++;
        }
    }
    return won;
}

int calcGolfCups(const uint16_t history[kGolfHistory], bool cupsOut[kGolfCups]) {
    const int won = calcGolfTrophies(history, nullptr);
    int cups = 0;
    for (int c = 0; c < kGolfCups; c++) {
        const bool got = won >= (c + 1) * 10;
        if (cupsOut) cupsOut[c] = got;
        if (got) cups++;
    }
    return cups;
}

bool parseOwnBookField(DatagramReader& r, uint16_t fieldId, OwnBookStats& out) {
    if (fieldId == dc::DistributedToon::k_setTrackProgress) {
        out.trackProgressId = r.i8();
        out.trackProgress = r.u32();
        out.version++;
        return r.ok();
    }
    if (fieldId == dc::DistributedToon::k_setCogStatus) {
        const uint16_t bytes = r.u16();
        const uint32_t n = bytes / 4;
        for (uint32_t i = 0; i < n && r.ok(); i++) {
            const uint32_t val = r.u32();
            if (i < kNumCogs) out.cogStatus[i] = val;
        }
        r.skip(bytes % 4);
        out.version++;
        return r.ok();
    }
    if (fieldId == dc::DistributedToon::k_setCogCount) {
        const uint16_t bytes = r.u16();
        const uint32_t n = bytes / 4;
        for (uint32_t i = 0; i < n && r.ok(); i++) {
            const uint32_t val = r.u32();
            if (i < kNumCogs) out.cogCount[i] = val;
        }
        r.skip(bytes % 4);
        out.version++;
        return r.ok();
    }
    if (fieldId == dc::DistributedToon::k_setCogRadar) {
        const Bytes b = r.blob();
        if (!r.ok()) return false;
        for (uint32_t i = 0; i < b.len && i < kNumDepts; i++) out.cogRadar[i] = b.data[i];
        out.version++;
        return true;
    }
    if (fieldId == dc::DistributedToon::k_setBuildingRadar) {
        const Bytes b = r.blob();
        if (!r.ok()) return false;
        for (uint32_t i = 0; i < b.len && i < kNumDepts; i++) out.buildingRadar[i] = b.data[i];
        out.version++;
        return true;
    }
    if (fieldId == dc::DistributedToon::k_setCogParts) {
        const uint16_t bytes = r.u16();
        const uint32_t n = bytes / 4;
        for (uint32_t i = 0; i < n && r.ok(); i++) {
            const uint32_t val = r.u32();
            if (i < kNumDepts) out.cogParts[i] = val;
        }
        r.skip(bytes % 4);
        out.version++;
        return r.ok();
    }
    if (fieldId == dc::DistributedToon::k_setCogMerits) {
        const uint16_t bytes = r.u16();
        const uint32_t n = bytes / 2;
        for (uint32_t i = 0; i < n && r.ok(); i++) {
            const uint16_t val = r.u16();
            if (i < kNumDepts) out.cogMerits[i] = val;
        }
        r.skip(bytes % 2);
        out.version++;
        return r.ok();
    }
    if (fieldId == dc::DistributedToon::k_setCogTypes) {
        const Bytes b = r.blob();
        if (!r.ok()) return false;
        for (uint32_t i = 0; i < b.len && i < kNumDepts; i++) out.cogTypes[i] = b.data[i];
        out.version++;
        return true;
    }
    if (fieldId == dc::DistributedToon::k_setCogLevels) {
        const Bytes b = r.blob();
        if (!r.ok()) return false;
        for (uint32_t i = 0; i < b.len && i < kNumDepts; i++) out.cogLevels[i] = b.data[i];
        out.version++;
        return true;
    }
    if (fieldId == dc::DistributedToon::k_setFishCollection) {
        const Bytes g = r.blob();
        const Bytes sp = r.blob();
        const Bytes w = r.blob();
        if (!r.ok()) return false;
        uint32_t n = g.len < sp.len ? g.len : sp.len;
        if (n > w.len / 2) n = w.len / 2;
        if (n > uint32_t(kMaxFishCollection)) n = kMaxFishCollection;
        out.numFishCollected = uint8_t(n);
        for (uint32_t i = 0; i < n; i++) {
            out.fishCollection[i].genus = g.data[i];
            out.fishCollection[i].species = sp.data[i];
            out.fishCollection[i].maxWeight = uint16_t(w.data[2 * i] | (w.data[2 * i + 1] << 8));
        }
        out.version++;
        return true;
    }
    if (fieldId == dc::DistributedToon::k_setFishingTrophies) {
        const Bytes tr = r.blob();
        if (!r.ok()) return false;
        out.numFishingTrophies = tr.len < 10 ? uint8_t(tr.len) : 10;
        for (uint32_t i = 0; i < out.numFishingTrophies; i++) out.fishingTrophies[i] = tr.data[i];
        out.version++;
        return true;
    }
    if (fieldId == dc::DistributedToon::k_setKartingTrophies) {
        for (int i = 0; i < kKartingTrophies && r.ok(); i++) out.kartingTrophies[i] = r.u8();
        out.version++;
        return r.ok();
    }
    if (fieldId == dc::DistributedToon::k_setKartingHistory) {
        for (int i = 0; i < kKartingHistory && r.ok(); i++) out.kartingHistory[i] = r.u8();
        out.version++;
        return r.ok();
    }
    if (fieldId == dc::DistributedToon::k_setTickets) {
        out.tickets = r.u32();
        out.version++;
        return r.ok();
    }
    if (fieldId == dc::DistributedToon::k_setGolfHistory) {
        for (int i = 0; i < kGolfHistory && r.ok(); i++) out.golfHistory[i] = r.u16();
        out.version++;
        return r.ok();
    }
    if (fieldId == dc::DistributedToon::k_setPackedGolfHoleBest) {
        for (int i = 0; i < 18 && r.ok(); i++) out.golfHoleBest[i] = r.u8();
        out.version++;
        return r.ok();
    }
    if (fieldId == dc::DistributedToon::k_setGolfCourseBest) {
        for (int i = 0; i < 3 && r.ok(); i++) out.golfCourseBest[i] = r.u8();
        out.version++;
        return r.ok();
    }
    if (fieldId == dc::DistributedToon::k_setNPCFriendsDict) {
        const uint16_t bytes = r.u16();
        uint32_t n = bytes / 5;
        if (n > uint32_t(kMaxSosCards)) n = kMaxSosCards;
        out.numSosCards = uint8_t(n);
        for (uint32_t i = 0; i < n && r.ok(); i++) {
            out.sosCards[i].npcId = static_cast<uint16_t>(r.u32());
            out.sosCards[i].count = r.u8();
        }
        if (bytes > n * 5) r.skip(bytes - n * 5);
        out.version++;
        return r.ok();
    }
    return false; // Not a book field
}

} // namespace tt::net
