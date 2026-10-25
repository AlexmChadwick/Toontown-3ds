#pragma once
#include <cstdint>
#include <cstring>
namespace tt {
// Exact content keys: changes in position/colour/scale do not change glyph layout.
// Fixed storage, including collision resolution; no per-frame allocation.
class TextKeys {
public:
    static constexpr int kEntries = 128;
    static constexpr int kBytes = 200;
    void clear() {
        count_ = 0;
        std::memset(buckets_, 0, sizeof(buckets_));
    }
    int find(const char* text) const {
        const uint32_t hash = hashOf(text);
        for (int probe = 0; probe < kBuckets; ++probe) {
            const int index = buckets_[(hash + probe) & (kBuckets - 1)] - 1;
            if (index < 0)
                return -1;
            if (!std::strcmp(strings_[index], text))
                return index;
        }
        return -1;
    }
    int add(const char* text) {
        const size_t bytes = std::strlen(text);
        if (count_ == kEntries || bytes >= kBytes)
            return -1;
        const uint32_t hash = hashOf(text);
        for (int probe = 0; probe < kBuckets; ++probe) {
            uint16_t& bucket = buckets_[(hash + probe) & (kBuckets - 1)];
            if (!bucket) {
                std::memcpy(strings_[count_], text, bytes + 1);
                bucket = uint16_t(count_ + 1);
                return count_++;
            }
        }
        return -1;
    }
    bool full() const {
        return count_ == kEntries;
    }

private:
    static constexpr int kBuckets = 256;
    static uint32_t hashOf(const char* text) {
        uint32_t hash = 2166136261u;
        while (*text) {
            hash ^= uint8_t(*text++);
            hash *= 16777619u;
        }
        return hash;
    }
    uint16_t buckets_[kBuckets] = {};
    char strings_[kEntries][kBytes] = {};
    int count_ = 0;
};
} // namespace tt
