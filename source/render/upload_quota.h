#pragma once
#include <cstdint>
namespace tt::render {
// Work ceiling in addition to time. Never depends on an emulated clock ticking.
struct UploadQuota {
    uint32_t limit;
    uint32_t worked = 0;
    bool consume(uint32_t bytes) {
        worked += bytes;
        return worked >= limit;
    }
};
}
