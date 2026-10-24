// Host test for Shtiker Book math, tables, quest descriptions, and trophies.
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdint>

#include "net/book_net.h"
#include "ui/disguise_tables.h"
#include "ui/sos_tables.h"
#include "quest/quest_text.h"
#include "quest/quest_data.h"

using namespace tt;
using namespace tt::ui;

// Exp thresholds table from ToontownBattleGlobals.py
constexpr uint16_t kLevels[7][7] = {
    {0, 20, 200, 800, 2000, 6000, 10000}, // Heal
    {0, 20, 100, 800, 2000, 6000, 10000}, // Trap
    {0, 20, 100, 800, 2000, 6000, 10000}, // Lure
    {0, 40, 200, 1000, 2500, 7500, 10000},// Sound
    {0, 10, 50, 400, 2000, 6000, 10000},  // Throw
    {0, 10, 50, 400, 2000, 6000, 10000},  // Squirt
    {0, 20, 100, 500, 2000, 6000, 10000}, // Drop
};

void testExpMath() {
    printf("Testing exp math thresholds...\n");

    auto getThresholds = [](int track, uint16_t exp, uint16_t& cur, uint16_t& next) {
        if (exp >= 10500) { cur = 10500; next = 10500; return; }
        if (exp >= 10000) { cur = 10000; next = 10500; return; }
        for (int l = 6; l >= 0; l--) {
            if (exp >= kLevels[track][l]) {
                cur = kLevels[track][l];
                next = (l < 6) ? kLevels[track][l + 1] : 10000;
                return;
            }
        }
        cur = 0;
        next = kLevels[track][1];
    };

    uint16_t cur = 0, next = 0;
    // Throw (track 4): levels are 0, 10, 50, 400, 2000, 6000, 10000
    getThresholds(4, 0, cur, next);
    assert(cur == 0 && next == 10);

    getThresholds(4, 9, cur, next);
    assert(cur == 0 && next == 10);

    getThresholds(4, 10, cur, next);
    assert(cur == 10 && next == 50);

    getThresholds(4, 399, cur, next);
    assert(cur == 50 && next == 400);

    getThresholds(4, 9999, cur, next);
    assert(cur == 6000 && next == 10000);

    getThresholds(4, 10000, cur, next);
    assert(cur == 10000 && next == 10500);

    getThresholds(4, 10500, cur, next);
    assert(cur == 10500 && next == 10500);

    printf("  Exp math tests passed!\n");
}

void testDisguiseTables() {
    printf("Testing disguise tables and merits...\n");

    // Parts per suit: Corporate/Bossbot=17, Legal/Lawbot=14, Money/Cashbot=12, Sales/Sellbot=10
    assert(kPartsPerSuit[0] == 17);
    assert(kPartsPerSuit[1] == 14);
    assert(kPartsPerSuit[2] == 12);
    assert(kPartsPerSuit[3] == 10);

    assert(kPartsBitmasks[0] == 131071);
    assert(kPartsBitmasks[1] == 130175);
    assert(kPartsBitmasks[2] == 56447);
    assert(kPartsBitmasks[3] == 56411);

    // Cog base levels
    assert(kCogBaseLevel[0] == 0);
    assert(kCogBaseLevel[7] == 7);
    assert(kCogBaseLevel[8] == 0);
    assert(kCogBaseLevel[15] == 7);

    // Merits per level helper
    uint16_t meritsNeeded = getNeededMerits(0, 0, 1);
    assert(meritsNeeded > 0);
    assert(isReadyForPromotion(0, 0, 1, meritsNeeded) == true);
    assert(isReadyForPromotion(0, 0, 1, meritsNeeded - 1) == false);

    printf("  Disguise tables passed!\n");
}

void testGolfTrophies() {
    printf("Testing golf trophies & cups calculation...\n");

    uint16_t history[net::kGolfHistory] = {};
    bool trophies[net::kGolfTrophies] = {};
    bool cups[net::kGolfCups] = {};

    int numTrophies = net::calcGolfTrophies(history, trophies);
    int numCups = net::calcGolfCups(history, cups);
    assert(numTrophies == 0);
    assert(numCups == 0);

    // Meet threshold 0 for all 10 categories to earn 10 trophies:
    history[0] = 4; // CoursesCompleted >= 4
    history[1] = 1; // CoursesUnderPar >= 1
    history[2] = 1; // HoleInOneShots >= 1
    history[3] = 2; // EagleOrBetterShots >= 2
    history[4] = 3; // BirdieOrBetterShots >= 3
    history[5] = 4; // ParOrBetterShots >= 4
    history[6] = 6; // MultiPlayerCoursesCompleted >= 6
    history[7] = 1; // CourseZeroWins >= 1
    history[8] = 1; // CourseOneWins >= 1
    history[9] = 1; // CourseTwoWins >= 1
    // Total: exactly 10 trophies!

    numTrophies = net::calcGolfTrophies(history, trophies);
    numCups = net::calcGolfCups(history, cups);
    assert(numTrophies == 10);
    assert(numCups == 1);
    assert(cups[0] == true && cups[1] == false && cups[2] == false);

    // Set all history to maximum threshold (600+)
    for (int i = 0; i < 10; i++) history[i] = 1000;
    numTrophies = net::calcGolfTrophies(history, trophies);
    numCups = net::calcGolfCups(history, cups);
    assert(numTrophies == 30);
    assert(numCups == 3);
    assert(cups[0] == true && cups[1] == true && cups[2] == true);

    printf("  Golf trophies & cups passed!\n");
}

void testSOSTables() {
    printf("Testing SOS friends table...\n");
    assert(kNumSosCards == 41);

    for (int i = 0; i < kNumSosCards; i++) {
        assert(kSosCards[i].npcId > 0);
        assert(kSosCards[i].name != nullptr);
        assert(strlen(kSosCards[i].name) > 0);
        assert(kSosCards[i].stars <= 5);
        assert(kSosCards[i].track <= 9);
    }

    printf("  SOS tables passed!\n");
}

void testQuestText() {
    printf("Testing quest text generation...\n");

    const quest::QuestDef* q101 = quest::findQuest(101);
    assert(q101 != nullptr);

    char headline[128] = {};
    char objective[128] = {};
    char progress[128] = {};

    quest::headline(*q101, false, headline, sizeof(headline));
    quest::objective(*q101, objective, sizeof(objective));
    quest::progressString(*q101, 2, progress, sizeof(progress));

    assert(strlen(headline) > 0);
    assert(strlen(objective) > 0);
    assert(strlen(progress) > 0);

    // Test complete headline
    quest::headline(*q101, true, headline, sizeof(headline));
    assert(strcmp(headline, "COMPLETE") == 0);

    printf("  Quest text generation passed! (Headline: %s, Obj: %s, Prog: %s)\n", headline, objective, progress);
}

int main() {
    printf("=== Shtiker Book Host Tests ===\n");
    testExpMath();
    testDisguiseTables();
    testGolfTrophies();
    testSOSTables();
    testQuestText();
    printf("=== All tests passed successfully! ===\n");
    return 0;
}
