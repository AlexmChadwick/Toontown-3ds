#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#define private public
#include "net/client.h"
#undef private
#include "net/connection.h"
#include "net/msg_types.h"
using namespace tt::net;
namespace dc = tt::dc;
class Recording final : public Transport {
  public:
    void connect(const char *, uint16_t) override {}
    void disconnect() override {}
    Status status() const override { return Status::Connected; }
    uint32_t session() const override { return 1; }
    const char *lastError() const override { return ""; }
    void setHeartbeat(bool) override {}
    int recvMessage(uint8_t *, uint32_t) override { return 0; }
    bool sendMessage(const uint8_t *bytes, uint32_t size) override {
        assert(size <= sizeof(message));
        std::memcpy(message, bytes, size);
        length = size;
        ++sends;
        return true;
    }
    uint8_t message[256] = {};
    uint32_t length = 0;
    int sends = 0;
};
Client client;
int main() {
    Recording transport;
    ClientConfig config;
    assert(client.start(transport, config));
    client.state_ = State::InZone;
    client.avatarId_ = 42;
    client.now_ = 1000;
    auto &race = client.racingRace_;
    race.active = true;
    race.doId = 100;
    race.racers[0] = race.results[0].avatar = 42;
    race.numRacers = 1;
    race.started = true;
    auto &block = client.racingBlocks_[0];
    block.active = true;
    block.doId = 101;
    auto &vehicle = client.racingVehicles_[0];
    vehicle.active = true;
    vehicle.doId = 102;
    vehicle.owner = vehicle.driver = 42;
    vehicle.state = 'C';
    auto &hazard = client.racingHazards_[0];
    hazard.active = true;
    hazard.race = 100;
    hazard.owner = 99;
    hazard.doId = 103;
    FILE *vectors = std::fopen("sdmc-out/racing2/vectors.bin", "rb");
    assert(vectors);
    auto compare = [&](uint32_t id) {
        uint16_t field = 0, size = 0;
        uint8_t expected[256];
        assert(std::fread(&field, 2, 1, vectors) == 1);
        assert(std::fread(&size, 2, 1, vectors) == 1);
        assert(std::fread(expected, 1, size, vectors) == size);
        DatagramReader actual(transport.message, transport.length);
        assert(actual.u16() == msg::CLIENT_OBJECT_SET_FIELD);
        assert(actual.u32() == id);
        assert(actual.u16() == field);
        assert(actual.remaining() == size);
        assert(!std::memcmp(transport.message + 8, expected, size));
    };
    assert(client.sendRacingEnter(101, false));
    compare(101);
    block.occupant = 42;
    block.movie = 1;
    assert(client.sendRacingExit(101));
    compare(101);
    assert(client.sendRacingMovieDone(101));
    compare(101);
    assert(client.sendRacingControl(102));
    compare(102);
    assert(client.sendRacingPark(102));
    compare(102);
    assert(client.sendRaceKart());
    compare(100);
    race.barriers[0].context = 123;
    race.barriers[0].waiting = true;
    assert(client.sendRaceBarrier(123));
    compare(100);
    assert(client.sendRaceProgress(2, .25f, 1000));
    compare(100);
    assert(client.sendRacingThrow(100, 1.25f, -2.5f, 3.75f));
    compare(100);
    assert(client.sendRacingGag(100, 3, 2, 4));
    compare(100);
    assert(client.sendRaceVehiclePose(102, 1.2f, -2.3f, 4.5f, 90, 0, 0, 1000));
    compare(102);
    assert(client.sendRaceHazardHit(103, 1000));
    compare(103);
    hazard.projectile = true;
    assert(client.sendRaceHazardHit(103, 1000));
    compare(103);
    assert(client.sendRaceLeave());
    compare(100);
    assert(std::fgetc(vectors) == EOF);
    std::fclose(vectors);
    const int sent = transport.sends;
    assert(!client.sendRacingInput(102, 1));
    assert(!client.sendRacingEnter(999));
    assert(!client.sendRaceBarrier(123));
    assert(!client.sendRaceProgress(0, NAN, 1000));
    assert(!client.sendRacingThrow(100, NAN, 0, 0));
    assert(!client.sendRacingGag(100, 32, 1, 0));
    assert(!client.sendRaceVehiclePose(102, 4000, 0, 0, 0, 0, 0, 1000));
    vehicle.driver = 99;
    assert(!client.sendRaceVehiclePose(102, 0, 0, 0, 0, 0, 0, 1000));
    race.racers[0] = 99;
    assert(!client.sendRaceKart());
    assert(!client.sendRaceLeave());
    assert(transport.sends == sent);
    // Truncated incoming state cannot partially replace a published object.
    race.racers[0] = 42;
    uint8_t bytes[128] = {};
    DatagramWriter writer(bytes, sizeof(bytes));
    writer.u32(42);
    writer.u32(12345);
    writer.u8(1);
    writer.u32(0);
    writer.u8(1);
    writer.u32(20);
    writer.u32(5);
    writer.blob(nullptr, 0);
    writer.blob(nullptr, 0);
    writer.u32(0);
    for (unsigned size = 0; size < writer.size(); ++size) {
        DatagramReader reader(bytes, size);
        assert(client.handleRacingField(reader, 100, dc::DistributedRace::k_setPlace));
        assert(!reader.ok() && !race.finished && !race.results[0].finished);
    }
    DatagramReader full(bytes, writer.size());
    assert(client.handleRacingField(full, 100, dc::DistributedRace::k_setPlace));
    assert(full.ok() && race.finished && race.results[0].winnings == 20 &&
           race.results[0].bonus == 5);
    for (unsigned index = 0; index < 20; ++index) {
        DatagramWriter shot(bytes, sizeof(bytes));
        shot.u32(42);
        shot.u32(99);
        shot.u8(1);
        DatagramReader input(bytes, shot.size());
        assert(client.handleRacingField(input, 100, dc::DistributedRace::k_shootPiejectile) &&
               input.ok());
    }
    assert(race.shotSerial == 20 && race.shots[3].serial == 20);
    DatagramReader truncatedShot(bytes, 7);
    assert(client.handleRacingField(truncatedShot, 100, dc::DistributedRace::k_shootPiejectile));
    assert(!truncatedShot.ok() && race.shotSerial == 20);
    // A dropped connection takes the race with it: nothing may be sent to it on the next session.
    client.fail("test drop", true);
    assert(!client.racingRace() && !client.racingBlocks_[0].active && !client.racingVehicles_[0].active &&
           !client.racingHazards_[0].active && !client.raceParticipant());
    std::printf("racing state: race %zu bytes, pads %zu, blocks %zu, vehicles %zu, hazards %zu\n",
                sizeof(RacingRace), sizeof(RacingPad) * kMaxRacingPads,
                sizeof(RacingBlock) * kMaxRacingBlocks, sizeof(RacingVehicle) * kMaxRacingVehicles,
                sizeof(RacingHazard) * 32);
    std::puts("racing: 14 DC sender vectors, authorization guards, truncated results, reset on disconnect PASS");
}
