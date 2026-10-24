#pragma once

#include <cstdint>

namespace tt::game {

// Panda3D RandomNumGen: Mersenne(seed).getUint31() == mt19937(seed)() >> 1
class RandomNumGen {
public:
    explicit RandomNumGen(uint32_t seed) { seedMt(seed); }
    explicit RandomNumGen(RandomNumGen& parent) { seedMt(parent.randint(0, 1 << 16)); }

    uint32_t getUint31() {
        if (idx_ >= 624) generate();
        uint32_t y = mt_[idx_++];
        y ^= y >> 11;
        y ^= (y << 7) & 0x9d2c5680u;
        y ^= (y << 15) & 0xefc60000u;
        y ^= y >> 18;
        return y >> 1;
    }

    uint32_t rand(uint32_t n) { return uint32_t((uint64_t(getUint31()) * n) >> 31); } // [0, n)
    int choice(int n) { return int(rand(uint32_t(n))); }                              // index into [0, n)
    int randrange(int start, int stop) { return start + int(rand(uint32_t(stop - start))); }
    int randint(int a, int b) { return a + int(rand(uint32_t(b - a + 1))); }          // [a, b]
    double random() { return double(getUint31()) / double(1u << 31); }                // [0, 1)

    template <typename T> void shuffle(T* x, int n) {
        for (int i = n - 1; i > 0; --i) {
            const int j = int(rand(uint32_t(i + 1)));
            const T tmp = x[i];
            x[i] = x[j];
            x[j] = tmp;
        }
    }

private:
    void seedMt(uint32_t seed) {
        mt_[0] = seed;
        for (int i = 1; i < 624; ++i) mt_[i] = 1812433253u * (mt_[i - 1] ^ (mt_[i - 1] >> 30)) + uint32_t(i);
        idx_ = 624;
    }

    void generate() {
        for (int i = 0; i < 624; ++i) {
            const uint32_t y = (mt_[i] & 0x80000000u) | (mt_[(i + 1) % 624] & 0x7fffffffu);
            uint32_t v = mt_[(i + 397) % 624] ^ (y >> 1);
            if (y & 1u) v ^= 0x9908b0dfu;
            mt_[i] = v;
        }
        idx_ = 0;
    }

    uint32_t mt_[624];
    int idx_ = 624;
};

} // namespace tt::game
