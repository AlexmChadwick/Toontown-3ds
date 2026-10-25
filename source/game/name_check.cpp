#include "game/name_check.h"

#include <cctype>
#include <cstdio>
#include <cstring>

namespace tt::game {

namespace {

// Helper to check if a char is a valid ASCII character for names in Toontown.
// Upstream NameCheck: set(".,'-" + string.ascii_letters + string.whitespace)
bool isValidAsciiChar(char c) {
    if (std::isalpha(static_cast<unsigned char>(c))) return true;
    if (std::isspace(static_cast<unsigned char>(c))) return true;
    return c == '.' || c == ',' || c == '\'' || c == '-';
}

bool isVowel(char c) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' || c == 'y';
}

// Extracts word list: split on whitespace, then split each word on '-' (omitting empty)
struct WordList {
    char words[16][64];
    int count = 0;
};

WordList extractWordList(const char* name) {
    WordList wl;
    const size_t len = std::strlen(name);
    size_t i = 0;
    while (i < len && wl.count < 16) {
        while (i < len && std::isspace(static_cast<unsigned char>(name[i]))) {
            i++;
        }
        if (i >= len) break;
        size_t start = i;
        while (i < len && !std::isspace(static_cast<unsigned char>(name[i]))) {
            i++;
        }
        // Sub-split by '-'
        size_t subStart = start;
        for (size_t k = start; k <= i; k++) {
            if (k == i || name[k] == '-') {
                if (k > subStart && wl.count < 16) {
                    size_t wlen = k - subStart;
                    if (wlen >= sizeof(wl.words[0])) wlen = sizeof(wl.words[0]) - 1;
                    std::memcpy(wl.words[wl.count], name + subStart, wlen);
                    wl.words[wl.count][wlen] = '\0';
                    wl.count++;
                }
                subStart = k + 1;
            }
        }
    }
    return wl;
}

} // namespace

NameCheckResult checkName(const char* name, char* badCharOut) {
    if (!name) return NameCheckResult::TooShort;

    const size_t len = std::strlen(name);

    // 1. printableChars
    for (size_t i = 0; i < len; i++) {
        unsigned char uc = static_cast<unsigned char>(name[i]);
        if (uc < 128 && !std::isprint(uc)) {
            return NameCheckResult::Generic;
        }
    }

    // 2. badCharacters
    for (size_t i = 0; i < len; i++) {
        char c = name[i];
        if (!isValidAsciiChar(c)) {
            if (std::isdigit(static_cast<unsigned char>(c))) {
                return NameCheckResult::NoDigits;
            }
            if (badCharOut) *badCharOut = c;
            return NameCheckResult::BadCharacter;
        }
    }

    // 4. longEnough & 5. emptyName
    if (len < 2) {
        return NameCheckResult::TooShort;
    }
    // Check if name is all whitespace
    bool allSpaces = true;
    for (size_t i = 0; i < len; i++) {
        if (!std::isspace(static_cast<unsigned char>(name[i]))) {
            allSpaces = false;
            break;
        }
    }
    if (allSpaces) {
        return NameCheckResult::TooShort;
    }
    if (len > 32) {
        return NameCheckResult::Generic;
    }

    WordList wl = extractWordList(name);

    // 6. hasLetters: every word in wordList must contain at least one letter
    for (int w = 0; w < wl.count; w++) {
        bool hasLetter = false;
        for (size_t i = 0; wl.words[w][i]; i++) {
            if (std::isalpha(static_cast<unsigned char>(wl.words[w][i]))) {
                hasLetter = true;
                break;
            }
        }
        if (!hasLetter) {
            return NameCheckResult::NeedLetters;
        }
    }

    // 7. hasVowels: words without '.' with > 2 letters must contain a vowel
    for (int w = 0; w < wl.count; w++) {
        const char* word = wl.words[w];
        if (std::strchr(word, '.')) continue;
        int letterCount = 0;
        int vowelCount = 0;
        for (size_t i = 0; word[i]; i++) {
            if (std::isalpha(static_cast<unsigned char>(word[i]))) {
                letterCount++;
                if (isVowel(word[i])) vowelCount++;
            }
        }
        if (letterCount > 2 && vowelCount == 0) {
            return NameCheckResult::NeedVowels;
        }
    }

    // 8. monoLetter: words with > 2 letters cannot consist of a single repeated letter
    for (int w = 0; w < wl.count; w++) {
        const char* word = wl.words[w];
        char firstLetter = '\0';
        int letterCount = 0;
        bool allSame = true;
        for (size_t i = 0; word[i]; i++) {
            if (std::isalpha(static_cast<unsigned char>(word[i]))) {
                char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(word[i])));
                if (letterCount == 0) {
                    firstLetter = lower;
                } else if (lower != firstLetter) {
                    allSame = false;
                }
                letterCount++;
            }
        }
        if (letterCount > 2 && allSame) {
            return NameCheckResult::Generic;
        }
    }

    // 9. checkDashes: must connect two letters together (not edge, preceded & followed by letter)
    for (size_t i = 0; i < len; i++) {
        if (name[i] == '-') {
            if (i == 0 || i == len - 1) {
                return NameCheckResult::DashUsage;
            }
            if (!std::isalpha(static_cast<unsigned char>(name[i - 1])) ||
                !std::isalpha(static_cast<unsigned char>(name[i + 1]))) {
                return NameCheckResult::DashUsage;
            }
        }
    }

    // 10. checkCommas: not edge, not preceded by space, must be followed by space
    for (size_t i = 0; i < len; i++) {
        if (name[i] == ',') {
            if (i == 0 || i == len - 1) {
                return NameCheckResult::CommaEdge;
            }
            if (std::isspace(static_cast<unsigned char>(name[i - 1]))) {
                return NameCheckResult::CommaAfterWord;
            }
            if (!std::isspace(static_cast<unsigned char>(name[i + 1]))) {
                return NameCheckResult::CommaUsage;
            }
        }
    }

    // 11. checkPeriods:
    for (int w = 0; w < wl.count; w++) {
        char wordBuf[64];
        std::strncpy(wordBuf, wl.words[w], sizeof(wordBuf) - 1);
        wordBuf[sizeof(wordBuf) - 1] = '\0';
        size_t wlen = std::strlen(wordBuf);
        if (wlen > 0 && wordBuf[wlen - 1] == ',') {
            wordBuf[--wlen] = '\0';
        }
        int numPeriods = 0;
        for (size_t i = 0; i < wlen; i++) {
            if (wordBuf[i] == '.') numPeriods++;
        }
        if (numPeriods == 0) continue;

        if (wordBuf[wlen - 1] != '.') {
            return NameCheckResult::PeriodUsage;
        }
        if (numPeriods > 2) {
            return NameCheckResult::PeriodUsage;
        }
        if (numPeriods == 2) {
            // Must fit J.T. pattern: word[1] == '.' and word[3] == '.'
            if (wlen < 4 || wordBuf[1] != '.' || wordBuf[3] != '.') {
                return NameCheckResult::PeriodUsage;
            }
        }
    }

    // 12. checkApostrophes: max 2 per word, max 3 in entire name
    int totalApos = 0;
    for (int w = 0; w < wl.count; w++) {
        int wordApos = 0;
        for (size_t i = 0; wl.words[w][i]; i++) {
            if (wl.words[w][i] == '\'') wordApos++;
        }
        if (wordApos > 2) {
            return NameCheckResult::Apostrophes;
        }
    }
    for (size_t i = 0; i < len; i++) {
        if (name[i] == '\'') totalApos++;
    }
    if (totalApos > 3) {
        return NameCheckResult::Apostrophes;
    }

    // 13. tooManyWords: max 4 words
    if (wl.count > 4) {
        return NameCheckResult::TooManyWords;
    }

    // 14. allCaps: if > 2 letters, name cannot be all uppercase
    int totalLetters = 0;
    int upperLetters = 0;
    for (size_t i = 0; i < len; i++) {
        if (std::isalpha(static_cast<unsigned char>(name[i]))) {
            totalLetters++;
            if (std::isupper(static_cast<unsigned char>(name[i]))) {
                upperLetters++;
            }
        }
    }
    if (totalLetters > 2 && upperLetters == totalLetters) {
        return NameCheckResult::AllCaps;
    }

    // 15. mixedCase: words > 2 chars cannot have > 2 capital letters
    for (int w = 0; w < wl.count; w++) {
        const char* word = wl.words[w];
        if (std::strlen(word) > 2) {
            int caps = 0;
            for (size_t i = 0; word[i]; i++) {
                if (std::isupper(static_cast<unsigned char>(word[i]))) {
                    caps++;
                }
            }
            if (caps > 2) {
                return NameCheckResult::MixedCase;
            }
        }
    }

    // 16. repeatedChars: character cannot repeat > 2 times consecutively
    int repeatCount = 1;
    char lastChar = '\0';
    for (size_t i = 0; i < len; i++) {
        char c = name[i];
        if (c == lastChar) {
            repeatCount++;
            if (repeatCount > 2) {
                if (badCharOut) *badCharOut = c;
                return NameCheckResult::RepeatedChar;
            }
        } else {
            repeatCount = 1;
            lastChar = c;
        }
    }

    return NameCheckResult::Ok;
}

void getNameCheckErrorMessage(NameCheckResult res, char badChar, char* out, size_t outSize) {
    if (!out || outSize == 0) return;
    switch (res) {
    case NameCheckResult::Ok:
        out[0] = '\0';
        break;
    case NameCheckResult::TooShort:
        std::snprintf(out, outSize, "That name is too short.");
        break;
    case NameCheckResult::NoDigits:
        std::snprintf(out, outSize, "Your name cannot contain numbers.");
        break;
    case NameCheckResult::NeedLetters:
        std::snprintf(out, outSize, "Each word in your name must contain some letters.");
        break;
    case NameCheckResult::NeedVowels:
        std::snprintf(out, outSize, "Each word in your name must contain some vowels.");
        break;
    case NameCheckResult::AllCaps:
        std::snprintf(out, outSize, "Your name cannot be all capital letters.");
        break;
    case NameCheckResult::MixedCase:
        std::snprintf(out, outSize, "That name has too many capital letters.");
        break;
    case NameCheckResult::BadCharacter:
        std::snprintf(out, outSize, "Your name cannot contain the character '%c'", badChar);
        break;
    case NameCheckResult::RepeatedChar:
        std::snprintf(out, outSize, "Your name has too many of the character '%c'", badChar);
        break;
    case NameCheckResult::Generic:
        std::snprintf(out, outSize, "Sorry, that name will not work.");
        break;
    case NameCheckResult::TooManyWords:
        std::snprintf(out, outSize, "Your name cannot be more than four words long.");
        break;
    case NameCheckResult::DashUsage:
        std::snprintf(out, outSize, "Dashes may only be used to connect two words together (like in 'Boo-Boo').");
        break;
    case NameCheckResult::CommaEdge:
        std::snprintf(out, outSize, "Your name may not begin or end with a comma.");
        break;
    case NameCheckResult::CommaAfterWord:
        std::snprintf(out, outSize, "You may not begin a word with a comma.");
        break;
    case NameCheckResult::CommaUsage:
        std::snprintf(out, outSize, "That name does not use commas properly. Commas must be followed by a space.");
        break;
    case NameCheckResult::PeriodUsage:
        std::snprintf(out, outSize, "That name does not use periods properly. Periods are only allowed in words like \"Mr.\", \"Mrs.\", \"J.T.\", etc.");
        break;
    case NameCheckResult::Apostrophes:
        std::snprintf(out, outSize, "That name has too many apostrophes.");
        break;
    }
}

} // namespace tt::game
