#pragma once

#include <cstddef>

namespace tt::game {

// Upstream NameCheck validation results (otp/namepanel/NameCheck.py)
enum class NameCheckResult {
    Ok,
    TooShort,       // NCTooShort = "That name is too short."
    NoDigits,       // NCNoDigits = "Your name cannot contain numbers."
    NeedLetters,    // NCNeedLetters = "Each word in your name must contain some letters."
    NeedVowels,     // NCNeedVowels = "Each word in your name must contain some vowels."
    AllCaps,        // NCAllCaps = "Your name cannot be all capital letters."
    MixedCase,      // NCMixedCase = "That name has too many capital letters."
    BadCharacter,   // NCBadCharacter = "Your name cannot contain the character '%c'"
    RepeatedChar,   // NCRepeatedChar = "Your name has too many of the character '%c'"
    Generic,        // NCGeneric = "Sorry, that name will not work."
    TooManyWords,   // NCTooManyWords = "Your name cannot be more than four words long."
    DashUsage,      // NCDashUsage = "Dashes may only be used to connect two words together..."
    CommaEdge,      // NCCommaEdge = "Your name may not begin or end with a comma."
    CommaAfterWord, // NCCommaAfterWord = "You may not begin a word with a comma."
    CommaUsage,     // NCCommaUsage = "That name does not use commas properly..."
    PeriodUsage,    // NCPeriodUsage = "That name does not use periods properly..."
    Apostrophes,    // NCApostrophes = "That name has too many apostrophes."
};

// Validates a typed toon name against the 16 checks in upstream NameCheck.py.
// If badCharOut is non-null and BadCharacter or RepeatedChar is returned,
// *badCharOut is set to the offending character.
NameCheckResult checkName(const char* name, char* badCharOut = nullptr);

// Formats a human-readable error message matching upstream OTPLocalizerEnglish.py.
void getNameCheckErrorMessage(NameCheckResult res, char badChar, char* out, size_t outSize);

} // namespace tt::game
