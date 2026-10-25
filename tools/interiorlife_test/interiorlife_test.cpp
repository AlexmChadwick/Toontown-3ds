#include <cassert>
#include <cstdio>
#include <cstring>

#include "game/knockknock_jokes.h"
#include "net/client.h"

#ifdef TT_HOST_TEST
namespace tt::net {
bool skipField(DatagramReader&, uint16_t) { return true; }
DatagramWriter Client::beginMessage(uint16_t) { return DatagramWriter(nullptr, 0); }
void Client::beginUpdate(DatagramWriter&, uint32_t, uint16_t) {}
bool Client::finish(DatagramWriter&) { return true; }
void Client::pushEvent(EventType, uint32_t) {}
uint32_t Client::networkToLocal(int16_t, uint32_t now) const { return now; }
} // namespace tt::net
#endif

namespace {

constexpr float kDoorOpenDelay = 0.4f;
constexpr float kDoorSwingDuration = 0.6f;

float smooth(float t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

float evalEnterSwing(float t) {
    if (t < kDoorOpenDelay) return 0.0f;
    const float openT = t - kDoorOpenDelay;
    if (openT < kDoorSwingDuration) {
        return smooth(openT / kDoorSwingDuration) * 100.0f;
    }
    // After t = 1.0s, closing over 1.0s
    const float closeT = t - (kDoorOpenDelay + kDoorSwingDuration);
    if (closeT < 1.0f) {
        return (1.0f - smooth(closeT / 1.0f)) * 100.0f;
    }
    return 0.0f;
}

float evalExitSwing(float t) {
    // Left leaf starts open at -100 deg and closes over 1.0s to 0 deg
    if (t < 0.0f) return -100.0f;
    if (t >= 1.0f) return 0.0f;
    return -100.0f * (1.0f - smooth(t / 1.0f));
}

void testLeaderboardPickle() {
    printf("Testing leaderboard pickle decoding...\n");

    // 1. Empty tuple: ([], [], [])
    // python: pickle.dumps(([], [], []), 1)
    const uint8_t vEmpty[] = {0x28, 0x5d, 0x71, 0x00, 0x5d, 0x71, 0x01, 0x5d, 0x71, 0x02, 0x74, 0x71, 0x03, 0x2e};
    tt::net::Client::HQLeaderboardState sEmpty{};
    assert(tt::net::Client::decodeLeaderboardPickle(vEmpty, sizeof(vEmpty), sEmpty));
    assert(sEmpty.numLeaders == 0);
    printf("  [PASS] Empty leaderboard tuple decoded correctly\n");

    // 2. 1 leader: ([1001], ["Mickey"], [42])
    // python: pickle.dumps(([1001], ["Mickey"], [42]), 1)
    const uint8_t vOne[] = {
        0x28, 0x5d, 0x71, 0x00, 0x4d, 0xe9, 0x03, 0x61, 0x5d, 0x71, 0x01, 0x58, 0x06, 0x00, 0x00, 0x00,
        'M', 'i', 'c', 'k', 'e', 'y', 0x71, 0x02, 0x61, 0x5d, 0x71, 0x03, 0x4b, 0x2a, 0x61, 0x74,
        0x71, 0x04, 0x2e
    };
    tt::net::Client::HQLeaderboardState sOne{};
    assert(tt::net::Client::decodeLeaderboardPickle(vOne, sizeof(vOne), sOne));
    assert(sOne.numLeaders == 1);
    assert(sOne.leaders[0].avId == 1001);
    assert(strcmp(sOne.leaders[0].name, "Mickey") == 0);
    assert(sOne.leaders[0].score == 42);
    printf("  [PASS] 1-leader pickle vector decoded correctly\n");

    // 3. 2 leaders: ([1001, 1002], ["Mickey", "Donald"], [100, 25])
    const uint8_t vTwo[] = {
        0x28, 0x5d, 0x71, 0x00, 0x28, 0x4d, 0xe9, 0x03, 0x4d, 0xea, 0x03, 0x65, 0x5d, 0x71, 0x01, 0x28,
        0x58, 0x06, 0x00, 0x00, 0x00, 'M', 'i', 'c', 'k', 'e', 'y', 0x71, 0x02, 0x58, 0x06, 0x00,
        0x00, 0x00, 'D', 'o', 'n', 'a', 'l', 'd', 0x71, 0x03, 0x65, 0x5d, 0x71, 0x04, 0x28, 0x4b,
        0x64, 0x4b, 0x19, 0x65, 0x74, 0x71, 0x05, 0x2e
    };
    tt::net::Client::HQLeaderboardState sTwo{};
    assert(tt::net::Client::decodeLeaderboardPickle(vTwo, sizeof(vTwo), sTwo));
    assert(sTwo.numLeaders == 2);
    assert(sTwo.leaders[0].avId == 1001 && strcmp(sTwo.leaders[0].name, "Mickey") == 0 && sTwo.leaders[0].score == 100);
    assert(sTwo.leaders[1].avId == 1002 && strcmp(sTwo.leaders[1].name, "Donald") == 0 && sTwo.leaders[1].score == 25);
    printf("  [PASS] 2-leader pickle vector decoded correctly\n");

    // 4. Malformed / truncated data tests
    tt::net::Client::HQLeaderboardState sBad{};
    assert(!tt::net::Client::decodeLeaderboardPickle(nullptr, 0, sBad));
    assert(!tt::net::Client::decodeLeaderboardPickle(vOne, 5, sBad));
    assert(!tt::net::Client::decodeLeaderboardPickle(vTwo, 12, sBad));
    const uint8_t vGarbage[] = {0xff, 0xee, 0xdd, 0xcc};
    assert(!tt::net::Client::decodeLeaderboardPickle(vGarbage, sizeof(vGarbage), sBad));
    printf("  [PASS] Malformed and truncated pickles rejected safely\n");
}

void testJokeTable() {
    printf("Testing knock-knock joke table...\n");

    assert(tt::game::getRegularJokeCount() == 118);

    // Regular joke 0
    tt::game::KnockKnockJoke j0 = tt::game::getRegularJoke(0);
    assert(strcmp(j0.setup, "Who") == 0);
    assert(strstr(j0.punchline, "echo") != nullptr);
    printf("  [PASS] Regular joke 0 verified: '%s' / '%s'\n", j0.setup, j0.punchline);

    // Contest jokes:
    // Silly Street (2100) block 44: "Wally", "Wally's not looking, hit him with a pie!"
    tt::game::KnockKnockJoke j2100_44 = tt::game::getKnockKnockJoke(2100, 44);
    assert(strcmp(j2100_44.setup, "Wally") == 0);
    assert(strstr(j2100_44.punchline, "pie") != nullptr);
    printf("  [PASS] Silly Street contest joke 44 verified: '%s'\n", j2100_44.setup);

    // Loopy Lane (2200) blocks 27, 28, 40, 41
    tt::game::KnockKnockJoke j2200_27 = tt::game::getKnockKnockJoke(2200, 27);
    assert(strcmp(j2200_27.setup, "Disguise") == 0);
    tt::game::KnockKnockJoke j2200_28 = tt::game::getKnockKnockJoke(2200, 28);
    assert(strcmp(j2200_28.setup, "Biscuit") == 0);
    tt::game::KnockKnockJoke j2200_40 = tt::game::getKnockKnockJoke(2200, 40);
    assert(strcmp(j2200_40.setup, "Minnie") == 0);
    tt::game::KnockKnockJoke j2200_41 = tt::game::getKnockKnockJoke(2200, 41);
    assert(strcmp(j2200_41.setup, "Dewey") == 0);
    printf("  [PASS] Loopy Lane contest jokes verified: Disguise, Biscuit, Minnie, Dewey\n");

    // Punchline Place (2300) block 1: "Justin"
    tt::game::KnockKnockJoke j2300_1 = tt::game::getKnockKnockJoke(2300, 1);
    assert(strcmp(j2300_1.setup, "Justin") == 0);
    assert(strstr(j2300_1.punchline, "Cog parts") != nullptr);
    printf("  [PASS] Punchline Place contest joke 1 verified: '%s'\n", j2300_1.setup);

    // Polar Place (3300) blocks 6, 10, 12, 28, 30
    assert(strcmp(tt::game::getKnockKnockJoke(3300, 6).setup, "Weirdo") == 0);
    assert(strcmp(tt::game::getKnockKnockJoke(3300, 10).setup, "Aladdin") == 0);
    assert(strcmp(tt::game::getKnockKnockJoke(3300, 12).setup, "Juliet") == 0);
    assert(strcmp(tt::game::getKnockKnockJoke(3300, 28).setup, "Isaiah") == 0);
    assert(strcmp(tt::game::getKnockKnockJoke(3300, 30).setup, "Bacon") == 0);
    printf("  [PASS] Polar Place contest jokes verified: Weirdo, Aladdin, Juliet, Isaiah, Bacon\n");

    // Fallback: non-contest block falls back to propId % 118
    tt::game::KnockKnockJoke jFallback = tt::game::getKnockKnockJoke(2100, 15);
    tt::game::KnockKnockJoke jExpected = tt::game::getRegularJoke(15 % 118);
    assert(strcmp(jFallback.setup, jExpected.setup) == 0);
    assert(strcmp(jFallback.punchline, jExpected.punchline) == 0);
    printf("  [PASS] Fallback to regular joke table verified\n");
}

void testDoorLeaves() {
    printf("Testing door swing kinematics & timings...\n");

    // Enter movie: right leaf swings 0 -> +100 deg over 0.6s after 0.4s delay
    assert(evalEnterSwing(0.0f) == 0.0f);
    assert(evalEnterSwing(0.2f) == 0.0f);
    assert(evalEnterSwing(0.4f) == 0.0f);
    const float midOpen = evalEnterSwing(0.7f);
    assert(midOpen > 40.0f && midOpen < 60.0f);
    assert(evalEnterSwing(1.0f) == 100.0f);
    const float midClose = evalEnterSwing(1.5f);
    assert(midClose > 40.0f && midClose < 60.0f);
    assert(evalEnterSwing(2.0f) == 0.0f);
    printf("  [PASS] Enter movie door swing verified (0 -> 100 -> 0 deg)\n");

    // Exit movie: left leaf starts open at -100 deg and closes over 1.0s to 0 deg
    assert(evalExitSwing(0.0f) == -100.0f);
    const float exitMid = evalExitSwing(0.5f);
    assert(exitMid > -60.0f && exitMid < -40.0f);
    assert(evalExitSwing(1.0f) == 0.0f);
    assert(evalExitSwing(1.5f) == 0.0f);
    printf("  [PASS] Exit movie left door swing verified (-100 -> 0 deg)\n");
}

} // namespace

int main() {
    printf("=== Interior Life Host Tests ===\n");
    testLeaderboardPickle();
    testJokeTable();
    testDoorLeaves();
    printf("=== All C++ Host Tests Passed ===\n");
    return 0;
}
