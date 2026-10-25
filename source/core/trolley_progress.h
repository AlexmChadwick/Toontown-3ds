#pragma once
#include <cstring>
namespace tt::autotest {
struct TrolleyProgress {
    bool seated = false, started = false, ended = false, returned = false;
    bool mayAdvance(const char* label) const {
        if (!std::strcmp(label, "board trolley")) return seated || started;
        if (!std::strcmp(label, "trolley: ride")) return started;
        if (!std::strcmp(label, "trolley: game")) return ended;
        if (!std::strcmp(label, "trolley: purchase")) return returned;
        return true;
    }
};
} // namespace tt::autotest
