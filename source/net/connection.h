// TCP connection to the Astron client agent.
//
// Threading model (AGENTS.md section 4.6): one net thread at prio 0x31 on core 0,
// below the main thread (0x30). The 3DS scheduler does not time-slice between
// priorities, so the net thread runs whenever the main thread blocks (vblank
// wait, C3D_FrameSync), i.e. every frame, and it sleeps in poll() with a 10 ms
// timeout so it never starves anyone. We chose a thread over non-blocking
// polling from the main loop because (a) socket syscalls are IPC round trips to
// soc:U that should not sit in the 33 ms frame budget, (b) the heartbeat keeps
// flowing while the main loop is parked in the HOME menu, and (c) a slow
// connect() never stalls a frame.
//
// Data path: two lock-protected byte rings that hold the Astron wire format
// verbatim ([u16 length][body] ...). The net thread recv()s straight into the
// receive ring and send()s straight out of the send ring, so there is no frame
// assembly copy on the net thread and no allocation after init().
#pragma once

#include <cstdint>

#include "net/net_platform.h"

namespace tt::net {

// What the protocol client needs from a connection. Connection is the real
// implementation; tests may substitute their own.
class Transport {
public:
    enum class Status : uint8_t { Idle, Connecting, Connected, Disconnected };

    virtual ~Transport() = default;
    virtual void connect(const char* host, uint16_t port) = 0; // async
    virtual void disconnect() = 0;                             // flushes, then closes
    virtual Status status() const = 0;
    // Increments on every successful connect, so the client can tell a fresh
    // session (that needs CLIENT_HELLO) from the one it already set up.
    virtual uint32_t session() const = 0;
    virtual const char* lastError() const = 0;
    // Queue one message body (without the length prefix). False if not
    // connected or the send ring is full.
    virtual bool sendMessage(const uint8_t* body, uint32_t len) = 0;
    // Pop one complete message body into out. Returns its length, 0 if none is
    // pending, -1 if it does not fit (the message is dropped).
    virtual int recvMessage(uint8_t* out, uint32_t cap) = 0;
    // Start/stop the automatic CLIENT_HEARTBEAT (only valid after HELLO_RESP).
    virtual void setHeartbeat(bool on) = 0;
};

// Single-producer/single-consumer byte ring guarded by a mutex.
class ByteRing {
public:
    void init(uint8_t* storage, uint32_t capacity) { buf_ = storage; cap_ = capacity; head_ = tail_ = 0; }
    uint32_t used() const { return head_ - tail_; }
    uint32_t space() const { return cap_ - used(); }
    void clear() { head_ = tail_ = 0; }
    // Contiguous free region at head / used region at tail.
    uint8_t* writePtr(uint32_t* n);
    const uint8_t* readPtr(uint32_t* n) const;
    void commit(uint32_t n) { head_ += n; }
    void consume(uint32_t n) { tail_ += n; }
    bool push(const void* p, uint32_t n);           // all or nothing
    void peek(void* dst, uint32_t n) const;         // n <= used()

private:
    uint8_t* buf_ = nullptr;
    uint32_t cap_ = 0;
    uint32_t head_ = 0; // free-running; index = value % cap_
    uint32_t tail_ = 0;
};

class Connection final : public Transport {
public:
    static constexpr uint32_t kRecvRingSize = 128 * 1024; // > largest Astron message (64 KB)
    static constexpr uint32_t kSendRingSize = 16 * 1024;
    static constexpr uint32_t kHeartbeatMs = 10000;
    static constexpr uint32_t kConnectTimeoutMs = 5000;
    static constexpr int kThreadPrio = 0x31;
    static constexpr int kThreadCore = 0;
    static constexpr uint32_t kThreadStack = 16 * 1024;

    // Allocates the rings (heap, once), initialises soc:U and starts the thread.
    bool init();
    void shutdown();

    void connect(const char* host, uint16_t port) override;
    void disconnect() override;
    Status status() const override { return status_; }
    uint32_t session() const override { return session_; }
    const char* lastError() const override { return error_; }
    bool sendMessage(const uint8_t* body, uint32_t len) override;
    int recvMessage(uint8_t* out, uint32_t cap) override;
    void setHeartbeat(bool on) override { heartbeat_ = on; }

    // Totals since init (approximate; read without locking).
    uint32_t bytesIn() const { return bytesIn_; }
    uint32_t bytesOut() const { return bytesOut_; }

private:
    static void threadEntry(void* self);
    void run();
    void doConnect();
    void closeSocket(const char* why);
    bool pumpSend();
    bool pumpRecv();

    plat::Thread thread_;
    plat::Mutex sendLock_;
    plat::Mutex recvLock_;
    plat::Mutex cmdLock_;
    ByteRing send_;
    ByteRing recv_;
    uint8_t* storage_ = nullptr;

    // Commands from the main thread (under cmdLock_).
    char host_[64] = {};
    uint16_t port_ = 0;
    bool wantConnect_ = false;
    bool wantDisconnect_ = false;

    volatile bool quit_ = false;
    volatile bool heartbeat_ = false;
    volatile Status status_ = Status::Idle;
    volatile uint32_t session_ = 0;
    volatile uint32_t bytesIn_ = 0;
    volatile uint32_t bytesOut_ = 0;
    const char* volatile error_ = "";

    int fd_ = -1;
    uint32_t lastHeartbeatMs_ = 0;
};

} // namespace tt::net
