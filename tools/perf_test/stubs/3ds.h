#pragma once
#include <cstdint>
using u32 = uint32_t;
using u16 = uint16_t;

using s16 = int16_t;
struct circlePosition {
    s16 dx = 0, dy = 0;
};
struct touchPosition {
    uint16_t px = 0, py = 0;
};
enum : u32 {
    KEY_A = 1u << 0,
    KEY_B = 1u << 1,
    KEY_L = 1u << 2,
    KEY_TOUCH = 1u << 3,
    // padnav: the pad: autotest steps (distinct bits; only their presence matters here)
    KEY_START = 1u << 4,
    KEY_DRIGHT = 1u << 5,
    KEY_DLEFT = 1u << 6,
    KEY_DUP = 1u << 7,
    KEY_DDOWN = 1u << 8,
    KEY_R = 1u << 9,
    KEY_X = 1u << 10,
    KEY_Y = 1u << 11,
};
