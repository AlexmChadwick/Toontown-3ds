// Test-only private access; production has no injection interface.
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#define private public
#include "net/client.h"
#undef private
#include "net/connection.h"
#include "net/msg_types.h"
using namespace tt::net;
namespace dc = tt::dc;
namespace {
class RecordingTransport final : public Transport {
public:
    void connect(const char *, uint16_t) override {}
    void disconnect() override {}
    Status status() const override {
        return Status::Connected;
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
    bool sendMessage(const uint8_t *, uint32_t) override {
        ++sends;
        return true;
    }
    int sends = 0;
};
Client client;
void seedSession(uint16_t type, ClerkKind kind) {
    client.resetClerks(false);
    auto &npc = client.npcs_[0];
    npc.active = true;
    npc.doId = 100;
    npc.classId = type;
    client.state_ = State::InZone;
    client.avatarId_ = 42;
    client.now_ = 1000;
    auto &movie = client.clerkSession_.movie;
    movie.clerk = 100;
    movie.kind = kind;
    movie.avatar = 42;
    movie.mode = 1;
    movie.deadline = 181000;
}
void movie(uint8_t *bytes, uint8_t mode, uint32_t avatar = 42) {
    DatagramWriter writer(bytes, 32);
    writer.u8(mode);
    writer.u32(2013);
    writer.u32(avatar);
    writer.u16(8);
    writer.u32(10);
    writer.u32(70);
    writer.i16(0);
}
} // namespace
int main() {
    RecordingTransport transport;
    ClientConfig config;
    assert(client.start(transport, config));
    const uint16_t classes[] = {dc::DistributedNPCPetclerk::kClassId, dc::DistributedNPCKartClerk::kClassId,
                                dc::DistributedNPCFisherman::kClassId, dc::DistributedNPCPartyPerson::kClassId};
    const uint16_t fields[] = {dc::DistributedNPCPetclerk::k_setMovie, dc::DistributedNPCKartClerk::k_setMovie,
                               dc::DistributedNPCFisherman::k_setMovie,
                               dc::DistributedNPCPartyPerson::k_setMovie};
    uint8_t bytes[32] = {};
    for (int kind = 0; kind < 4; ++kind) {
        for (int prefix = 0; prefix < 21; ++prefix) {
            seedSession(classes[kind], ClerkKind(kind + 1));
            movie(bytes, 4);
            DatagramReader reader(bytes, prefix);
            assert(client.handleClerksField(reader, 100, fields[kind]));
            assert(!reader.ok());
            assert(client.clerkSession_.movie.mode == 1 && client.clerkHead_ == 0);
        }
        seedSession(classes[kind], ClerkKind(kind + 1));
        movie(bytes, 4, 99);
        DatagramReader other(bytes, 21);
        assert(client.handleClerksField(other, 100, fields[kind]) && other.ok());
        assert(client.clerkHead_ == 0);
        for (uint8_t mode : {uint8_t(4), uint8_t(0)}) {
            movie(bytes, mode);
            DatagramReader reader(bytes, 21);
            assert(client.handleClerksField(reader, 100, fields[kind]) && reader.ok());
        }
        ClerkMovie event;
        assert(client.pollClerkMovie(event) && event.mode == 4 && event.numExtra == 2 && event.extra[1] == 70);
        assert(client.pollClerkMovie(event) && event.mode == 0);
        assert(!client.pollClerkMovie(event));
        assert(!client.sendServiceClerkDone());
    }
    // Seeds and owner arrays publish only after complete, bounded decoding.
    for (int prefix = 0; prefix < 26; ++prefix) {
        seedSession(classes[0], ClerkKind::Pet);
        DatagramWriter writer(bytes, sizeof(bytes));
        writer.u16(24);
        for (int i = 0; i < 6; ++i)
            writer.u32(i);
        DatagramReader reader(bytes, prefix);
        assert(client.handleClerksField(reader, 100, dc::DistributedNPCPetclerk::k_setPetSeeds));
        assert(!reader.ok() && !client.clerkSession_.seedsReceived);
    }
    const uint16_t ownerFields[] = {dc::DistributedToon::k_setPetId,
                                    dc::DistributedToon::k_setKartAccessoriesOwned,
                                    dc::DistributedToon::k_setKartBodyType,
                                    dc::DistributedToon::k_setKartBodyColor,
                                    dc::DistributedToon::k_setKartAccessoryColor,
                                    dc::DistributedToon::k_setKartEngineBlockType,
                                    dc::DistributedToon::k_setKartSpoilerType,
                                    dc::DistributedToon::k_setKartFrontWheelWellType,
                                    dc::DistributedToon::k_setKartBackWheelWellType,
                                    dc::DistributedToon::k_setKartRimType,
                                    dc::DistributedToon::k_setKartDecalType};
    for (int field = 0; field < 11; ++field) {
        const int length = field == 0 ? 4 : field == 1 ? 16 : 1;
        std::memset(bytes, 0, sizeof(bytes));
        for (int prefix = 0; prefix < length; ++prefix) {
            const uint32_t version = client.clerkProfile_.version;
            DatagramReader reader(bytes, prefix);
            assert(client.clerksOwnField(reader, ownerFields[field]));
            assert(!reader.ok() && client.clerkProfile_.version == version);
        }
        DatagramReader reader(bytes, length);
        const uint32_t version = client.clerkProfile_.version;
        assert(client.clerksOwnField(reader, ownerFields[field]) && reader.ok());
        assert(client.clerkProfile_.version == version + 1);
    }
    seedSession(classes[1], ClerkKind::Kart);
    client.ownBook_.tickets = 99;
    assert(!client.sendClerkKartBody(0));
    client.ownBook_.tickets = 100;
    client.clerkProfile_.kart[0] = -1;
    client.clerkSession_.movie.avatar = 43;
    assert(!client.sendClerkKartBody(0));
    client.clerkSession_.movie.avatar = 42;
    client.clerkSession_.movie.deadline = client.now_;
    assert(!client.sendClerkKartBody(0));
    client.clerkSession_.movie.deadline = client.now_ + 1;
    client.npcs_[0].classId = classes[0];
    assert(!client.sendClerkKartBody(0));
    client.clerkNewsManager_ = 900;
    for (int prefix = 0; prefix < 6; ++prefix) {
        client.clerkFishHoliday_ = false;
        DatagramWriter writer(bytes, sizeof(bytes));
        writer.u16(4);
        writer.u32(98);
        DatagramReader reader(bytes, prefix);
        assert(client.handleClerksField(reader, 900, dc::NewsManager::k_setHolidayIdList));
        assert(!reader.ok() && !client.clerkFishHoliday_);
    }
    DatagramReader holidays(bytes, 6);
    assert(client.handleClerksField(holidays, 900, dc::NewsManager::k_setHolidayIdList) && holidays.ok());
    client.ownFish_.tankCount = 1;
    client.ownFish_.genus[0] = 0;
    client.ownFish_.species[0] = 0;
    client.ownFish_.weight[0] = 16;
    const int doubled = client.clerkFishTotal();
    assert(client.clerkFishHoliday_ && doubled > 0);
    client.handleLeaving(900);
    assert(!client.clerkFishHoliday_ && client.clerkFishTotal() * 2 == doubled);
    client.resetClerks(true);
    assert(client.clerkProfile_.petId == 0 && client.clerkProfile_.kart[0] == -1);
    std::printf("Clerk state bytes: movie %zu session %zu profile %zu FIFO %zu\n", sizeof(ClerkMovie),
                sizeof(ClerkSession), sizeof(ClerkProfile), sizeof(ClerkMovie) * kClerkMovieQueue);
    std::printf(
        "Real Client: movie/seed/11 owner-field truncation, membership, expiry, funds and reset PASS\n");
}
