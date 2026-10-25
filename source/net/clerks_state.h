#pragma once
#include <cstdint>
namespace tt::net {
enum class ClerkKind : uint8_t { None, Pet, Kart, Fisherman, Party };
ClerkKind clerkKind(uint16_t classId);
constexpr int kClerkMovieQueue = 16;
struct ClerkMovie {
    uint32_t clerk = 0;
    ClerkKind kind = ClerkKind::None;
    uint8_t mode = 0;
    uint32_t npc = 0;
    uint32_t avatar = 0;
    uint32_t extra[8] = {};
    uint8_t numExtra = 0;
    uint32_t deadline = 0;
};
struct ClerkSession {
    ClerkMovie movie;
    uint32_t pending = 0;
    uint32_t pendingMs = 0;
    uint32_t seeds[6] = {};
    uint8_t seedCount = 0;
    bool seedsReceived = false;
    bool finishing = false;
    bool petAction = false;
    int pendingBody = -1;
    int pendingAccessory = -1;
};
struct ClerkProfile {
    uint32_t petId = 0;
    int8_t kart[9] = {-1, -1, -1, -1, -1, -1, -1, -1, -1};
    int8_t accessories[16] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    uint32_t version = 0;
};
} // namespace tt::net
