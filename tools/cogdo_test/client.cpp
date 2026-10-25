// Real Client, with test-only access to state and a recording transport.
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#define private public
#include "net/client.h"
#undef private
#include "net/connection.h"
#include "net/msg_types.h"
namespace dc = tt::dc;
using namespace tt::net;
class RecordingTransport final : public Transport {
public:
    void connect(const char *, uint16_t) override {
        connected = true;
    }
    void disconnect() override {
        connected = false;
    }
    Status status() const override {
        return connected ? Status::Connected : Status::Disconnected;
    }
    uint32_t session() const override {
        return 1;
    }
    const char *lastError() const override {
        return "";
    }
    void setHeartbeat(bool) override {}
    int recvMessage(uint8_t *, uint32_t) override {
        return 0;
    }
    bool sendMessage(const uint8_t *data, uint32_t length) override {
        assert(length <= sizeof(bytes));
        std::memcpy(bytes, data, length);
        size = length;
        ++messages;
        return true;
    }
    DatagramReader update(uint32_t object, uint16_t field) const {
        DatagramReader reader(bytes, size);
        assert(reader.u16() == msg::CLIENT_OBJECT_SET_FIELD);
        assert(reader.u32() == object && reader.u16() == field);
        return reader;
    }
    bool connected = false;
    uint8_t bytes[1024] = {};
    uint32_t size = 0;
    unsigned messages = 0;
};
static void state(CogdoState &object, uint32_t id, CogdoKind kind, uint16_t cls) {
    object = CogdoState{};
    object.active = true;
    object.doId = id;
    object.kind = kind;
    object.classId = cls;
}
int main() {
    RecordingTransport transport;
    static Client client;
    ClientConfig config;
    assert(client.start(transport, config));
    client.state_ = State::InZone;
    client.avatarId_ = 42;
    client.now_ = 1000;
    FILE *file = std::fopen("sdmc-out/cogdo-test/enters.bin", "rb");
    assert(file);
    uint32_t id = 0;
    int enters = 0;
    while (std::fread(&id, 4, 1, file) == 1) {
        uint16_t cls = 0;
        uint32_t size = 0;
        uint8_t data[16384];
        assert(std::fread(&cls, 2, 1, file) == 1 && std::fread(&size, 4, 1, file) == 1 && size <= sizeof(data));
        assert(std::fread(data, 1, size, file) == size);
        const auto *description = findClass(cls);
        assert(description);
        client.clearCogdo();
        DatagramReader reader(data, size);
        assert(client.handleCogdoEnter(id, cls, reader, requiredOther(*description), description->otherCount,
                                       true));
        assert(reader.ok() && !reader.remaining() && client.findCogdo(id));
        const CogdoState before = *client.findCogdo(id);
        for (uint32_t length = 0; length < size; ++length) {
            DatagramReader truncated(data, length);
            assert(client.handleCogdoEnter(id, cls, truncated, requiredOther(*description),
                                           description->otherCount, true));
            assert(!std::memcmp(&before, client.findCogdo(id), sizeof(before)));
        }
        client.cogdoLeaving(id);
        assert(!client.findCogdo(id));
        ++enters;
    }
    std::fclose(file);
    FILE *families = std::fopen("sdmc-out/cogdo-test/families.bin", "rb");
    assert(families);
    for (int index = 0; index < 4; ++index) {
        uint32_t size = 0;
        uint8_t data[16384];
        assert(std::fread(&size, 4, 1, families) == 1 && size <= sizeof(data));
        assert(std::fread(data, 1, size, families) == size);
        DatagramReader reader(data, size);
        client.handleEnter(reader, msg::CLIENT_ENTER_OBJECT_REQUIRED_OTHER);
        if (!reader.ok() || reader.remaining())
            std::fprintf(stderr, "family %d ok %d remaining %u total %u\n", index, int(reader.ok()),
                         reader.remaining(), size);
        assert(reader.ok() && !reader.remaining());
        if (index < 2)
            assert(client.findCogElevator(2000 + index));
        if (index == 2)
            assert(client.findBattle(2002));
        if (index == 3)
            assert(client.findCogBuilding(2003));
    }
    std::fclose(families);
    client.clearCogdo();
    auto &interior = client.cogdoObjects_[0];
    state(interior, 100, CogdoKind::Interior, dc::DistributedCogdoInterior::kClassId);
    assert(!client.sendCogdoReady());
    interior.numToons = 1;
    interior.toons[0] = 42;
    const char *phases[] = {"WaitForAllToonsInside", "Elevator",         "ReservesJoining",
                            "BarrelRoomIntro",       "BarrelRoomReward", "CollectBarrels"};
    const uint16_t fields[] = {dc::DistributedCogdoInterior::k_setAvatarJoined,
                               dc::DistributedCogdoInterior::k_elevatorDone,
                               dc::DistributedCogdoInterior::k_reserveJoinDone,
                               dc::DistributedCogdoInterior::k_toonBarrelRoomIntroDone,
                               dc::DistributedCogdoInterior::k_toonBarrelRoomRewardDone,
                               dc::DistributedCogdoInterior::k_toonLeftBarrelRoom};
    for (int phase = 0; phase < 6; ++phase) {
        std::strcpy(interior.state, phases[phase]);
        assert(client.sendCogdoInterior(static_cast<Client::CogdoInteriorAction>(phase)));
        assert(!transport.update(100, fields[phase]).remaining());
        assert(!client.sendCogdoInterior(static_cast<Client::CogdoInteriorAction>((phase + 1) % 6)));
    }
    auto &game = client.cogdoObjects_[1];
    state(game, 101, CogdoKind::Maze, dc::DistCogdoMazeGame::kClassId);
    game.parent = 100;
    game.phase = CogdoGamePhase::Intro;
    assert(client.sendCogdoReady());
    assert(!transport.update(101, dc::DistCogdoGame::k_setAvatarReady).remaining());
    assert(!client.sendCogdoGag(1, 2, 90));
    game.phase = CogdoGamePhase::Playing;
    assert(!client.sendCogdoReady() && !client.sendCogdoAction(0));
    game.doorOpen = true;
    assert(client.sendCogdoAction(0));
    auto reader = transport.update(101, dc::DistCogdoMazeGame::k_requestAction);
    assert(reader.u8() == 0 && reader.u32() == 0 && !reader.remaining());
    game.enteredDoor = true;
    assert(!client.sendCogdoAction(0) && !client.sendCogdoAction(2));
    game.hasGag = true;
    assert(!client.sendCogdoGag(NAN, 0, 0));
    assert(client.sendCogdoGag(1.5f, -2, 90));
    reader = transport.update(101, dc::DistCogdoMazeGame::k_requestUseGag);
    assert(reader.i16() == 15 && reader.i16() == -20 && reader.i16() == 900);
    reader.i16();
    assert(!reader.remaining());
    game.suitCounts[0] = 4;
    game.suitCounts[1] = 5;
    game.suitCounts[2] = 5;
    std::memset(game.suitHp, 1, 14);
    assert(!client.sendCogdoSuit(1, 0, true) && !client.sendCogdoSuit(0, 64, true));
    assert(client.sendCogdoSuit(0, 0, true));
    reader = transport.update(101, dc::DistCogdoMazeGame::k_requestSuitHitByGag);
    assert(reader.u8() == 0 && reader.u8() == 0 && !reader.remaining());
    assert(client.sendCogdoSuit(2, 10, false));
    assert(client.sendCogdoPickup(255));
    assert(transport.update(101, dc::DistCogdoMazeGame::k_requestPickUp).u8() == 255);
    game.pickups[255] = true;
    assert(!client.sendCogdoPickup(255) && !client.sendCogdoPickup(256));
    assert(!client.sendCogdoCooler(0));
    game.hasGag = false;
    assert(client.sendCogdoCooler(3));
    assert(transport.update(101, dc::DistCogdoMazeGame::k_requestGag).u8() == 3);
    auto &barrel = client.cogdoObjects_[2];
    state(barrel, 102, CogdoKind::Barrel, dc::DistributedCogdoBarrel::kClassId);
    barrel.state[0] = '1';
    assert(client.sendCogdoBarrel(102));
    barrel.grabbed = true;
    assert(!client.sendCogdoBarrel(102));
    barrel.grabbed = false;
    uint8_t grab[4] = {43, 0, 0, 0};
    DatagramReader receive(grab, sizeof(grab));
    assert(client.handleCogdoField(receive, 102, dc::DistributedCogdoBarrel::k_setGrab));
    assert(!barrel.grabbed && barrel.state[0] == '1');
    grab[0] = 42;
    receive = DatagramReader(grab, sizeof(grab));
    assert(client.handleCogdoField(receive, 102, dc::DistributedCogdoBarrel::k_setGrab) && barrel.grabbed);
    game.kind = CogdoKind::Flying;
    assert(!client.sendCogdoAction(2) && !client.sendCogdoAction(1) && !client.sendCogdoAction(0, 256));
    game.action = 2;
    assert(client.sendCogdoAction(1));
    game.kind = CogdoKind::CraneGame;
    auto &crane = client.cogdoObjects_[3];
    state(crane, 103, CogdoKind::Crane, dc::DistCogdoCrane::kClassId);
    crane.parent = 101;
    crane.state[0] = 'C';
    float links[3][3] = {{1, 2, -6}, {2, 3, -13}, {3, 4, -20}};
    assert(!client.sendCogdoCable(103, 255, 24.5f, 315, links));
    crane.owner = 42;
    assert(client.sendCogdoCable(103, 255, 24.5f, 315, links));
    reader = transport.update(103, dc::DistCogdoCrane::k_setCablePos);
    assert(reader.u8() == 255 && reader.i16() == 2450 && reader.u16() == 31500);
    for (const auto &link : links)
        for (float value : link)
            assert(reader.i16() == int(value * 10));
    reader.i16();
    assert(!reader.remaining());
    assert(!client.sendCogdoCable(103, 0, 26, 0, links));
    auto &bag = client.cogdoObjects_[4];
    state(bag, 104, CogdoKind::MoneyBag, dc::DistCogdoCraneMoneyBag::kClassId);
    bag.parent = 101;
    bag.state[0] = 'F';
    assert(client.sendCogdoBag(104, true) && !client.sendCogdoBag(104, false));
    bag.owner = 42;
    bag.state[0] = 'G';
    assert(client.sendCogdoBag(104, false));
    assert(client.sendCogdoBagMotion(104, Client::CogdoBagAction::Pose, 1, 2, 3, 90));
    assert(!client.sendCogdoBagMotion(104, Client::CogdoBagAction::Floor, 1, 2, 3, 90));
    bag.state[0] = 'D';
    assert(client.sendCogdoBagMotion(104, Client::CogdoBagAction::Floor, 1, 2, 3, 90));
    bag.state[0] = 's';
    assert(client.sendCogdoBagMotion(104, Client::CogdoBagAction::Free, 1, 2, 3, 90));
    const unsigned before = transport.messages;
    interior.toons[0] = 43;
    assert(!client.sendCogdoBag(104, false) && !client.sendCogdoReady());
    assert(!client.sendCogdoSuit(0, 0, true) && !client.sendCogdoBarrel(102));
    assert(!client.sendCogdoCable(103, 0, 20, 0, links) && transport.messages == before);
    auto &building = client.cogBuildings_[0];
    building.active = true;
    building.doId = 99;
    building.cogdoVictory = true;
    building.numVictors = 1;
    building.victors[0] = 42;
    assert(client.sendCogdoVictor(99));
    assert(!transport.update(99, dc::DistributedBuilding::k_setVictorReady).remaining());
    building.victors[0] = 43;
    assert(!client.sendCogdoVictor(99));
    client.clearCogdo();
    assert(!client.cogdoInterior() && !client.cogdoGame());
    std::printf("Cogdo real Client: %d Panda enters + every truncation, RPC guards/owners/claims/exit PASS\n",
                enters);
}
