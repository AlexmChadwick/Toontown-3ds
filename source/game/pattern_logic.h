// Pattern minigame pure logic (no 3DS headers). Simon-says dance pattern matching.
#pragma once

#include <cstdint>

namespace tt::patterngame {

constexpr int kNumRounds = 4;
constexpr int kMaxPatternLen = 16;
constexpr float kInputTimeout = 10.0f;

struct Simulation {
    int round = 0;
    int score = 0;
    bool isPerfect = true;
    bool isDone = false;

    uint8_t targetPattern[kMaxPatternLen] = {};
    int targetLen = 0;

    uint8_t inputPattern[kMaxPatternLen] = {};
    int inputLen = 0;
    bool hasWrong = false;

    void init();
    void setTargetPattern(const uint8_t* pattern, int len);
    bool inputButton(uint8_t buttonIndex);
    bool finishRound(float totalTime, bool isMultiplayer = false, bool isFastest = false);
};

} // namespace tt::patterngame
