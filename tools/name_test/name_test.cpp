#include <cassert>
#include <cstdio>
#include <cstring>

#include "game/name_check.h"
#include "game/name_tables.h"

using namespace tt::game;

void testNameTables() {
    printf("== testNameTables\n");

    const NameMasterEntry* all = getAllNameEntries();
    assert(all != nullptr);
    assert(kTotalNameMasterEntries == 435);

    // Check specific known entries from NameMasterEnglish.txt
    assert(strcmp(getNameString(0), "Baron") == 0);
    assert(getNameCategory(0) == kCatBoyTitle);

    assert(strcmp(getNameString(7), "Lady") == 0);
    assert(getNameCategory(7) == kCatGirlTitle);

    assert(strcmp(getNameString(13), "Captain") == 0);
    assert(getNameCategory(13) == kCatNeutralTitle);

    assert(strcmp(getNameString(242), "Mc") == 0);
    assert(getNameCategory(242) == kCatCapPrefix);
    assert(isCapPrefix(242));

    assert(strcmp(getNameString(243), "Mac") == 0);
    assert(getNameCategory(243) == kCatCapPrefix);
    assert(isCapPrefix(243));

    assert(!isCapPrefix(0));
    assert(!isCapPrefix(244));

    // Check list counts
    assert(getTitleCount(true) == 27);    // 7 boy + 20 neutral
    assert(getTitleCount(false) == 26);   // 6 girl + 20 neutral
    assert(getFirstCount(true) == 171);   // 65 boy + 106 neutral
    assert(getFirstCount(false) == 144);  // 38 girl + 106 neutral
    assert(getPrefixCount() == 96);       // 2 cap + 94 lastPrefix
    assert(getSuffixCount() == 97);       // 97 lastSuffix

    // Check pattern building
    char buf[128];

    // Doctor (19), Buzz (44), Boingen (251), nerd (391)
    buildPatternName(19, 1, 44, 1, 251, 1, 391, 0, buf, sizeof(buf));
    assert(strcmp(buf, "Doctor Buzz Boingennerd") == 0);

    // Cap prefix: Buzz (44), Mc (242), nerd (391) -> McNerd
    int16_t mcId = 242;
    int16_t nerdId = 391; // "nerd"
    buildPatternName(-1, 1, 44, 1, mcId, 1, nerdId, 1, buf, sizeof(buf));
    assert(strcmp(buf, "Buzz McNerd") == 0);

    // Title only
    buildPatternName(13, 1, -1, 1, -1, 1, -1, 0, buf, sizeof(buf));
    assert(strcmp(buf, "Captain") == 0);

    // First only
    buildPatternName(-1, 1, 44, 1, -1, 1, -1, 0, buf, sizeof(buf));
    assert(strcmp(buf, "Buzz") == 0);

    // Last only
    buildPatternName(-1, 1, -1, 1, 251, 1, 391, 0, buf, sizeof(buf));
    assert(strcmp(buf, "Boingennerd") == 0);

    // Title + Last
    buildPatternName(19, 1, -1, 1, 251, 1, 391, 0, buf, sizeof(buf));
    assert(strcmp(buf, "Doctor Boingennerd") == 0);

    // Random patterns
    for (int i = 0; i < 100; i++) {
        int16_t p[4];
        uint8_t f[4];
        randomPattern(i % 2 == 0, p, f);
        char randBuf[128];
        buildPatternName(p[0], f[0], p[1], f[1], p[2], f[2], p[3], f[3], randBuf, sizeof(randBuf));
        assert(strlen(randBuf) > 0);
    }

    printf("PASS: testNameTables\n");
}

void testNameCheck() {
    printf("== testNameCheck\n");

    char badChar = '\0';

    // Valid names
    assert(checkName("Flippy") == NameCheckResult::Ok);
    assert(checkName("Doctor Flippy") == NameCheckResult::Ok);
    assert(checkName("Flippy McDuck") == NameCheckResult::Ok);
    assert(checkName("Boo-Boo") == NameCheckResult::Ok);
    assert(checkName("Dr. Quack, MD") == NameCheckResult::Ok);
    assert(checkName("J.T.") == NameCheckResult::Ok);
    assert(checkName("Sir Good ol' Boy") == NameCheckResult::Ok);
    assert(checkName("Good-o-Paws") == NameCheckResult::Ok);

    // TooShort & empty
    assert(checkName("") == NameCheckResult::TooShort);
    assert(checkName("a") == NameCheckResult::TooShort);
    assert(checkName("   ") == NameCheckResult::TooShort);

    // NoDigits
    assert(checkName("Toon123") == NameCheckResult::NoDigits);
    assert(checkName("Dog 2") == NameCheckResult::NoDigits);

    // BadCharacter
    badChar = '\0';
    assert(checkName("Toon!", &badChar) == NameCheckResult::BadCharacter && badChar == '!');
    assert(checkName("Flippy@") == NameCheckResult::BadCharacter);
    assert(checkName("Cat#Dog") == NameCheckResult::BadCharacter);

    // NeedLetters
    assert(checkName(". .") == NameCheckResult::NeedLetters);
    assert(checkName("---") == NameCheckResult::DashUsage);

    // NeedVowels
    assert(checkName("Brrr") == NameCheckResult::NeedVowels);
    assert(checkName("Zzzp") == NameCheckResult::NeedVowels);

    // MonoLetter
    assert(checkName("aaaa") == NameCheckResult::Generic);
    assert(checkName("eEe") == NameCheckResult::Generic);
    assert(checkName("ZZZZ") == NameCheckResult::NeedVowels);

    // DashUsage
    assert(checkName("-Flippy") == NameCheckResult::DashUsage);
    assert(checkName("Flippy-") == NameCheckResult::DashUsage);
    assert(checkName("Boo--Boo") == NameCheckResult::DashUsage);
    assert(checkName("Boo- Boo") == NameCheckResult::DashUsage);

    // CommaEdge, CommaAfterWord, CommaUsage
    assert(checkName(",Flippy") == NameCheckResult::CommaEdge);
    assert(checkName("Flippy,") == NameCheckResult::CommaEdge);
    assert(checkName("Dr. ,MD") == NameCheckResult::CommaAfterWord);
    assert(checkName("Dr.,MD") == NameCheckResult::CommaUsage);

    // PeriodUsage
    assert(checkName("Dr. Quack") == NameCheckResult::Ok);
    assert(checkName("Dr.abc") == NameCheckResult::PeriodUsage);
    assert(checkName("J..T.") == NameCheckResult::PeriodUsage);
    assert(checkName("A.B.C.") == NameCheckResult::PeriodUsage);

    // Apostrophes
    assert(checkName("D'''og") == NameCheckResult::Apostrophes);
    assert(checkName("D'o'g'gy'") == NameCheckResult::Apostrophes);

    // TooManyWords
    assert(checkName("One Two Three Four Five") == NameCheckResult::TooManyWords);
    assert(checkName("One Two Three Four") == NameCheckResult::Ok);

    // AllCaps
    assert(checkName("FLIPPY") == NameCheckResult::AllCaps);
    assert(checkName("TOON") == NameCheckResult::AllCaps);

    // MixedCase
    assert(checkName("MCBat") == NameCheckResult::MixedCase);
    assert(checkName("FLIppy") == NameCheckResult::MixedCase);
    assert(checkName("McDuck") == NameCheckResult::Ok); // 2 caps is allowed

    // RepeatedChar
    badChar = '\0';
    assert(checkName("Fliiippy", &badChar) == NameCheckResult::RepeatedChar && badChar == 'i');
    assert(checkName("Toooon", &badChar) == NameCheckResult::RepeatedChar && badChar == 'o');

    // Error messages
    char errMsg[128];
    getNameCheckErrorMessage(NameCheckResult::TooShort, '\0', errMsg, sizeof(errMsg));
    assert(strcmp(errMsg, "That name is too short.") == 0);

    getNameCheckErrorMessage(NameCheckResult::NoDigits, '\0', errMsg, sizeof(errMsg));
    assert(strcmp(errMsg, "Your name cannot contain numbers.") == 0);

    printf("PASS: testNameCheck\n");
}

int main() {
    testNameTables();
    testNameCheck();
    printf("ALL NAME TESTS PASSED\n");
    return 0;
}
