// Python's random.Random (MT19937 seeded like random.seed(int)) for the few places where the
// client must pick exactly what the AI picks from the same seed, e.g. BarrelBase.getRng:
// random.Random(entId * level.doId). Host tested in tools/level_spec_test against CPython.
#pragma once

#include <cstdint>

namespace tt::world {

class PyRandom {
public:
    explicit PyRandom(uint64_t seed) {
        // random.seed(int): init_by_array with the seed's 32-bit words, least significant first.
        uint32_t key[2] = {uint32_t(seed), uint32_t(seed >> 32)};
        const int keyLength = key[1] ? 2 : 1;
        initGenrand(19650218u);
        int i = 1, j = 0;
        for (int k = kN > keyLength ? kN : keyLength; k; --k) {
            mt_[i] = (mt_[i] ^ ((mt_[i - 1] ^ (mt_[i - 1] >> 30)) * 1664525u)) + key[j] + uint32_t(j);
            if (++i >= kN) { mt_[0] = mt_[kN - 1]; i = 1; }
            if (++j >= keyLength) j = 0;
        }
        for (int k = kN - 1; k; --k) {
            mt_[i] = (mt_[i] ^ ((mt_[i - 1] ^ (mt_[i - 1] >> 30)) * 1566083941u)) - uint32_t(i);
            if (++i >= kN) { mt_[0] = mt_[kN - 1]; i = 1; }
        }
        mt_[0] = 0x80000000u;
    }
    // getrandbits(k), 0 < k <= 32
    uint32_t getrandbits(int k) { return next() >> (32 - k); }
    // _randbelow(n): getrandbits(n.bit_length()) until below n.
    uint32_t randbelow(uint32_t n) {
        if (n <= 1) return 0;
        int bits = 0;
        for (uint32_t v = n; v; v >>= 1) ++bits;
        uint32_t r;
        do r = getrandbits(bits);
        while (r >= n);
        return r;
    }
    int32_t randrange(int32_t start, int32_t stop) { return start + int32_t(randbelow(uint32_t(stop - start))); }

private:
    static constexpr int kN = 624;
    void initGenrand(uint32_t s) {
        mt_[0] = s;
        for (int i = 1; i < kN; ++i) mt_[i] = 1812433253u * (mt_[i - 1] ^ (mt_[i - 1] >> 30)) + uint32_t(i);
        index_ = kN;
    }
    uint32_t next() {
        if (index_ >= kN) {
            for (int i = 0; i < kN; ++i) {
                const uint32_t y = (mt_[i] & 0x80000000u) | (mt_[(i + 1) % kN] & 0x7fffffffu);
                mt_[i] = mt_[(i + 397) % kN] ^ (y >> 1) ^ ((y & 1) ? 0x9908b0dfu : 0u);
            }
            index_ = 0;
        }
        uint32_t y = mt_[index_++];
        y ^= y >> 11;
        y ^= (y << 7) & 0x9d2c5680u;
        y ^= (y << 15) & 0xefc60000u;
        return y ^ (y >> 18);
    }
    uint32_t mt_[kN];
    int index_ = kN;
};

} // namespace tt::world
