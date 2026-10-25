#include "net/client.h"
#include "net/connection.h"
#include "net/net_platform.h"
#include "ui/clerk_rules.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace tt::net;
namespace {
Client client;
Connection connection;
int terminalMovies = 0;
template <typename Predicate> void await(Predicate ready) {
    const uint32_t start = plat::nowMs();
    do {
        client.update(plat::nowMs());
        if (ready())
            return;
        ClerkMovie movie;
        while (client.pollClerkMovie(movie)) {
            if (movie.mode > 1 && movie.mode < 255)
                ++terminalMovies;
        }
        plat::sleepMs(5);
    } while (plat::nowMs() - start < 8000);
    std::fprintf(stderr, "clerk fixture timed out\n");
    std::abort();
}
void enter(uint32_t clerk) {
    assert(client.sendServiceClerkEnter(clerk));
    assert(!client.sendServiceClerkEnter(clerk));
    await([&] { return client.clerkSession().movie.clerk == clerk && client.clerkSession().movie.mode == 1; });
}
void done() {
    assert(client.sendServiceClerkDone());
    assert(!client.sendServiceClerkDone());
    await([] { return client.clerkSession().movie.mode == 0; });
}
void terminal(uint32_t clerk, int expected) {
    assert(client.sendServiceClerkEnter(clerk));
    bool seen = false;
    await([&] {
        ClerkMovie movie;
        while (client.pollClerkMovie(movie)) {
            if (movie.clerk == clerk && movie.mode == expected)
                seen = true;
        }
        return seen && client.clerkSession().movie.mode == 0;
    });
}
} // namespace
int main(int argc, char **argv) {
    assert(argc == 3);
    const bool offers = std::strcmp(argv[2], "offers") == 0;
    assert(connection.init());
    ClientConfig config;
    config.host = "127.0.0.1";
    config.port = uint16_t(std::atoi(argv[1]));
    config.playToken = "clerk-test";
    assert(client.start(connection, config));
    assert(!client.sendPetAdopt(0, 0));
    assert(!client.sendPetReturn());
    assert(!client.sendClerkKartBody(0));
    assert(!client.sendClerkKartAccessory(0));
    assert(!client.sendServiceFishSale(true));
    assert(!client.sendServiceClerkDone());
    await([] { return client.inZone() && client.findNpc(200000103) && client.ownBook().tickets == 10000 && client.ownFish().tankCount == 2; });
    assert(client.clerkFishTotal() == 2 * (tt::clerks::fishValue(0, 0, 16) + tt::clerks::fishValue(1, 0, 40)));
    enter(200000100);
    assert(client.clerkSession().seedsReceived);
    assert(client.clerkSession().seedCount == (offers ? 6 : 0));
    assert(!client.sendPetAdopt(6, 0));
    assert(!client.sendPetAdopt(0, 2713));
    if (offers) {
        assert(client.sendPetAdopt(0, 0));
        assert(!client.sendPetAdopt(0, 0));
        assert(!client.sendServiceFishSale(true));
        await([] { return client.clerkProfile().petId == 501; });
        done();
        enter(200000100);
        assert(client.sendPetReturn());
        assert(!client.sendPetReturn());
        await([] { return client.clerkProfile().petId == 0; });
        done();
    } else {
        assert(!client.sendPetAdopt(0, 0));
        assert(!client.sendPetReturn());
        done();
    }
    enter(200000100);
    assert(client.clerkFishTotal() > 0);
    assert(client.sendServiceFishSale(true));
    assert(!client.sendServiceFishSale(true));
    await([] { return client.clerkSession().movie.mode == 0 && client.ownFish().tankCount == 0; });
    enter(200000101);
    assert(!client.sendClerkKartBody(3));
    assert(!client.sendClerkKartAccessory(0));
    assert(client.sendClerkKartBody(0));
    assert(!client.sendClerkKartBody(1));
    await([] { return client.clerkProfile().kart[0] == 0 && client.ownBook().tickets == 9900; });
    assert(!client.sendClerkKartBody(0));
    assert(!client.sendClerkKartAccessory(40));
    assert(!client.sendClerkKartAccessory(92));
    assert(client.sendClerkKartAccessory(0));
    assert(!client.sendClerkKartAccessory(1));
    await([] {
        return client.clerkProfile().accessories[0] == 0 && client.clerkProfile().kart[3] == 0 &&
               client.ownBook().tickets == 9700;
    });
    assert(!client.sendClerkKartAccessory(0));
    done();
    enter(200000102);
    assert(client.sendServiceFishSale(true));
    await([] { return client.clerkSession().movie.mode == 0 && client.ownFish().tankCount == 0; });
    terminal(200000102, 3);
    enter(200000102);
    assert(client.sendServiceFishSale(false));
    await([] { return client.clerkSession().movie.mode == 0; });
    terminal(200000103, 6);
    assert(!client.sendServiceClerkDone());
    enter(200000103);
    done();
    // All terminal replies precede CLEAR in the retained FIFO, even when both
    // arrive in one socket read. No world/UI polling is needed to preserve them.
    ClerkMovie movie;
    int clears = 0;
    while (client.pollClerkMovie(movie))
        if (movie.mode == 0)
            ++clears;
    assert(clears > 0 && terminalMovies > 0);
    connection.shutdown();
    std::printf("Real Client service-clerk %s fields, session guards, owner resends: PASS\n", argv[2]);
}
