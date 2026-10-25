#pragma once
#include <cstdint>
using u32 = uint32_t;
using u16 = uint16_t;
using u8 = uint8_t;
using s16 = int16_t;
struct circlePosition { s16 dx = 0, dy = 0; };
struct touchPosition { u16 px = 0, py = 0; };
constexpr u32 KEY_A = 1, KEY_DLEFT = 2, KEY_DRIGHT = 4, KEY_DUP = 8, KEY_DDOWN = 16;
