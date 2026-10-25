#pragma once
#include <cstdint>
namespace tt::net {
struct PairingAnswer {
    int16_t up = -1, match = -1;
    uint32_t avatar = 0;
    int points = 0, count = 0;
    int16_t down[4] = {};
};
// Preserve each wire answer even when several arrive in one net.update turn.
struct PairingAnswers {
    static constexpr int kCapacity = 64;
    PairingAnswer values[kCapacity];
    int head = 0, count = 0;
    bool push(const PairingAnswer& answer) {
        if (count == kCapacity) return false;
        values[(head + count++) % kCapacity] = answer;
        return true;
    }
    bool pop(PairingAnswer& answer) {
        if (!count) return false;
        answer = values[head];
        head = (head + 1) % kCapacity;
        --count;
        return true;
    }
};
} // namespace tt::net
