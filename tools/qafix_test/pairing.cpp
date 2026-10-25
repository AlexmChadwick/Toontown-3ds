#include <cassert>
#include <cstring>
#define private public
#include "game/pairing_game.h"
#undef private
// The runtime descriptor code is real; the shared loader is dormant in this test.
namespace tt::world {
LoadTicket AssetLoader::requestModel(const char*) { return 0; }
LoadTicket AssetLoader::requestAnim(const char*) { return 0; }
LoadStatus AssetLoader::poll(LoadTicket) { return LoadStatus::Pending; }
bool AssetLoader::takeModel(LoadTicket, render::Model&) { return false; }
bool AssetLoader::takeAnim(LoadTicket, render::Anim&) { return false; }
void AssetLoader::cancel(LoadTicket) {}
}
int main() {
    using namespace tt;
    net::Client client;
    game::PairingGame game;
    game.init(client, 0, true);
    circlePosition pad{};
    for (int i = 0; i < 300; ++i) game.update(client, pad, 0, 1.0f/30, 1000 + i*33);
    assert(client.requests == 1 && game.flips_ == 0); // no speculative flips or request flood
    net::PairingAnswer answer;
    answer.avatar = 1;
    answer.up = int16_t(client.card);
    assert(client.answers.push(answer));
    game.update(client, pad, 0, .01f, 11000);
    assert(game.flips_ == 1);
    for (int i = 0; i < 100; ++i) game.update(client, pad, 0, .01f, 11010+i*10);
    assert(game.flips_ == 1 && client.requests == 2);
    // Multiple wire answers in a TCP turn each get consumed exactly once.
    answer.up = 4;
    client.answers.push(answer);
    answer.up = 5;
    answer.match = 4;
    answer.points = 1;
    client.answers.push(answer);
    game.update(client, pad, 0, .01f, 13000);
    assert(game.flips_ == 3 && game.matches_ == 1);
    game.update(client, pad, 0, .01f, 13010);
    assert(game.flips_ == 3 && game.matches_ == 1);
    game.shutdown();
    game::pairing::Card deck[game::pairing::kMaxDeckCards];
    for (int players = 1; players <= 4; ++players) {
        int count = 0;
        game::pairing::generateDeck(0, players, deck, count);
        assert(count == (players <= 2 ? 32 : players == 3 ? 36 : 40));
    }
}
