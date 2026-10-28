// The real Client vs SpeedChat ToonTask fields (TTSCToontaskMenu): setSCToontask said aloud and
// setWhisperSCToontaskFrom, both directions. Test-only private access, as tools/clerks_test.
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
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
    void connect(const char*, uint16_t) override {}
    void disconnect() override {}
    Status status() const override { return Status::Connected; }
    uint32_t session() const override { return 1; }
    const char* lastError() const override { return ""; }
    void setHeartbeat(bool) override {}
    int recvMessage(uint8_t*, uint32_t) override { return 0; }
    bool sendMessage(const uint8_t* body, uint32_t len) override {
        last.assign(body, body + len);
        return true;
    }
    std::vector<uint8_t> last;
};
Client client;
RecordingTransport transport;

void feed(uint32_t doId, uint16_t field, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint8_t idx, bool whisper,
          uint32_t len = 0) {
    uint8_t bytes[64];
    DatagramWriter w(bytes, sizeof(bytes));
    w.u16(msg::CLIENT_OBJECT_SET_FIELD);
    w.u32(doId);
    w.u16(field);
    if (whisper) w.u32(a);
    w.u32(b);
    w.u32(c);
    w.u32(d);
    w.u8(idx);
    DatagramReader r(bytes, len ? len : w.size());
    client.handleMessage(r);
}
} // namespace

int main() {
    ClientConfig config;
    assert(client.start(transport, config));
    client.state_ = State::InZone;
    client.avatarId_ = 42;
    client.now_ = 1000;
    RemoteToon& t = client.toons_[0];
    t = RemoteToon{};
    t.active = true;
    t.doId = 77;
    t.scIndex = 5;

    // Quest 101 (defeat a Flunky) to Toon HQ (1000), progress 0: "I need to defeat a Flunky."
    feed(77, dc::DistributedToon::k_setSCToontask, 0, 101, 1000, 0, 0, false);
    assert(!strcmp(t.chat, "I need to defeat a Flunky.") && t.scIndex == -1 && t.chatMs == 1000);
    // Undecodable (unknown quest): dropped, the last phrase stays.
    feed(77, dc::DistributedToon::k_setSCToontask, 0, 65000, 1000, 0, 0, false);
    assert(!strcmp(t.chat, "I need to defeat a Flunky."));
    // Truncated payloads never publish.
    t.chat[0] = 0;
    for (uint32_t len = 6; len < 6 + 13; len++) {
        feed(77, dc::DistributedToon::k_setSCToontask, 0, 101, 1000, 0, 0, false, len);
        assert(t.chat[0] == 0);
    }

    // Whisper to us.
    const int before = client.numWhispers();
    feed(42, dc::DistributedToon::k_setWhisperSCToontaskFrom, 77, 101, 1000, 1, 1, true);
    assert(client.numWhispers() == before + 1);
    const Whisper& w = client.whisperAt(client.numWhispers() - 1);
    assert(w.kind == WhisperKind::ToonTask && w.fromId == 77 && !strcmp(w.text, "I need to see an HQ Officer."));

    // Outgoing: setSCToontask(u32 taskId, u32 toNpc, u32 progress, u8 msgIndex) on our own avatar.
    assert(client.sendSCToontask(101, 1000, 1, 1));
    const std::vector<uint8_t>& s = transport.last;
    assert(s.size() == 2 + 4 + 2 + 13);
    DatagramReader r(s.data(), uint32_t(s.size()));
    assert(r.u16() == msg::CLIENT_OBJECT_SET_FIELD && r.u32() == 42 && r.u16() == dc::DistributedToon::k_setSCToontask);
    assert(r.u32() == 101 && r.u32() == 1000 && r.u32() == 1 && r.u8() == 1 && r.ok() && r.remaining() == 0);

    assert(client.sendWhisperSCToontask(77, 101, 1000, 1, 1, "I need to see an HQ Officer."));
    DatagramReader r2(transport.last.data(), uint32_t(transport.last.size()));
    assert(r2.u16() == msg::CLIENT_OBJECT_SET_FIELD && r2.u32() == 77 &&
           r2.u16() == dc::DistributedToon::k_setWhisperSCToontaskFrom);
    assert(r2.u32() == 42 && r2.u32() == 101 && r2.u32() == 1000 && r2.u32() == 1 && r2.u8() == 1 && r2.ok() &&
           r2.remaining() == 0);
    assert(!client.sendWhisperSCToontask(99, 101, 1000, 1, 1, nullptr)); // not a toon we can see
    client.state_ = State::Idle;
    assert(!client.sendSCToontask(101, 1000, 1, 1));
    std::printf("sc_test client: setSCToontask rx/tx, whisper rx/tx, truncation PASS\n");
}
