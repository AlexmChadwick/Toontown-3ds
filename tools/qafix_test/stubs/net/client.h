#pragma once
#include <3ds.h>
#include "net/pairing_answers.h"
namespace tt::net {
constexpr int kNoDifficultyOverride = 2147483647;
struct Minigame {
    uint32_t pairingDeckSeed = 0, trolleyZone = 2000, gameStartMs = 1000;
    int numParticipants = 1, difficultyOverride = kNoDifficultyOverride;
    bool started = true, pairingEveryoneDone = false;
};
struct Client {
    Minigame game;
    PairingAnswers answers;
    int requests = 0, done = 0, card = -1;
    const Minigame* minigame() const { return &game; }
    u32 avatarId() const { return 1; }
    bool popPairingAnswer(PairingAnswer& answer) { return answers.pop(answer); }
    bool sendPairingOpenCard(int16_t index, int16_t) { ++requests; card = index; return true; }
    bool sendPairingDone() { ++done; return true; }
};
}
