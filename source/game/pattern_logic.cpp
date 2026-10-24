#include "game/pattern_logic.h"

#include <cstring>

namespace tt::patterngame {

void Simulation::init() {
    round = 0;
    score = 0;
    isPerfect = true;
    isDone = false;
    targetLen = 0;
    inputLen = 0;
    hasWrong = false;
    std::memset(targetPattern, 0, sizeof(targetPattern));
    std::memset(inputPattern, 0, sizeof(inputPattern));
}

void Simulation::setTargetPattern(const uint8_t* pattern, int len) {
    if (len > kMaxPatternLen) len = kMaxPatternLen;
    targetLen = len;
    std::memcpy(targetPattern, pattern, len);
    round++;
    inputLen = 0;
    hasWrong = false;
    std::memset(inputPattern, 0, sizeof(inputPattern));
}

bool Simulation::inputButton(uint8_t buttonIndex) {
    if (inputLen >= kMaxPatternLen || isDone) {
        return false;
    }

    bool correct = false;
    if (inputLen < targetLen && targetPattern[inputLen] == buttonIndex) {
        correct = true;
    } else {
        hasWrong = true;
    }

    inputPattern[inputLen++] = buttonIndex;
    return correct;
}

bool Simulation::finishRound(float totalTime, bool isMultiplayer, bool isFastest) {
    (void)totalTime;
    bool match = (inputLen == targetLen && !hasWrong);
    if (match) {
        for (int i = 0; i < targetLen; ++i) {
            if (inputPattern[i] != targetPattern[i]) {
                match = false;
                break;
            }
        }
    }

    if (match) {
        score += round;
        if (isMultiplayer && isFastest) {
            score += 2;
        }
    } else {
        isPerfect = false;
    }

    if (round >= kNumRounds) {
        if (isPerfect) {
            score += 4;
        }
        isDone = true;
    }

    return match;
}

} // namespace tt::patterngame
