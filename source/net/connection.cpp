#include "net/connection.h"

#include <cstdlib>
#include <cstring>

#include "core/log.h"
#include "net/msg_types.h"

namespace tt::net {

static_assert((Connection::kRecvRingSize & (Connection::kRecvRingSize - 1)) == 0, "ring size must be 2^n");
static_assert((Connection::kSendRingSize & (Connection::kSendRingSize - 1)) == 0, "ring size must be 2^n");
static_assert(Connection::kRecvRingSize > 2 + 0xffff, "recv ring must hold one maximal message");

// ---------------------------------------------------------------- ByteRing
uint8_t* ByteRing::writePtr(uint32_t* n) {
    uint32_t idx = head_ & (cap_ - 1);
    uint32_t contiguous = cap_ - idx;
    uint32_t free = space();
    *n = free < contiguous ? free : contiguous;
    return buf_ + idx;
}

const uint8_t* ByteRing::readPtr(uint32_t* n) const {
    uint32_t idx = tail_ & (cap_ - 1);
    uint32_t contiguous = cap_ - idx;
    uint32_t avail = used();
    *n = avail < contiguous ? avail : contiguous;
    return buf_ + idx;
}

bool ByteRing::push(const void* p, uint32_t n) {
    if (n > space()) return false;
    const uint8_t* src = static_cast<const uint8_t*>(p);
    uint32_t idx = head_ & (cap_ - 1);
    uint32_t first = cap_ - idx < n ? cap_ - idx : n;
    memcpy(buf_ + idx, src, first);
    memcpy(buf_, src + first, n - first);
    head_ += n;
    return true;
}

void ByteRing::peek(void* dst, uint32_t n) const {
    uint8_t* out = static_cast<uint8_t*>(dst);
    uint32_t idx = tail_ & (cap_ - 1);
    uint32_t first = cap_ - idx < n ? cap_ - idx : n;
    memcpy(out, buf_ + idx, first);
    memcpy(out + first, buf_, n - first);
}

// ---------------------------------------------------------------- Connection
bool Connection::init() {
    if (storage_) return true;
    storage_ = static_cast<uint8_t*>(malloc(kRecvRingSize + kSendRingSize));
    if (!storage_) { error_ = "out of memory"; return false; }
    recv_.init(storage_, kRecvRingSize);
    send_.init(storage_ + kRecvRingSize, kSendRingSize);
    if (!plat::socketsInit()) {
        error_ = "socInit failed (is Wi-Fi enabled?)";
        free(storage_);
        storage_ = nullptr;
        return false;
    }
    quit_ = false;
    if (!thread_.start(&Connection::threadEntry, this, kThreadPrio, kThreadCore, kThreadStack)) {
        error_ = "net thread start failed";
        plat::socketsExit();
        free(storage_);
        storage_ = nullptr;
        return false;
    }
    return true;
}

void Connection::shutdown() {
    if (!storage_) return;
    quit_ = true;
    if (!thread_.join()) {
        // Net thread still running (stuck in a SOC call): shut the socket down under it and
        // leave its buffers and soc:U alone; the process exit reclaims them.
        TT_LOG("net: thread stuck at exit; skipping socExit");
        plat::tcpClose(fd_);
        fd_ = -1;
        return;
    }
    plat::tcpClose(fd_);
    fd_ = -1;
    plat::socketsExit();
    free(storage_);
    storage_ = nullptr;
    status_ = Status::Idle;
}

void Connection::connect(const char* host, uint16_t port) {
    plat::LockGuard g(cmdLock_);
    strncpy(host_, host, sizeof(host_) - 1);
    host_[sizeof(host_) - 1] = 0;
    port_ = port;
    wantConnect_ = true;
    wantDisconnect_ = false;
    status_ = Status::Connecting;
}

void Connection::disconnect() {
    plat::LockGuard g(cmdLock_);
    wantDisconnect_ = true;
    wantConnect_ = false;
}

bool Connection::sendMessage(const uint8_t* body, uint32_t len) {
    if (len > 0xffff) return false;
    uint8_t hdr[2] = {uint8_t(len), uint8_t(len >> 8)};
    plat::LockGuard g(sendLock_);
    if (status_ != Status::Connected || send_.space() < len + 2) return false;
    send_.push(hdr, 2);
    send_.push(body, len);
    return true;
}

int Connection::recvMessage(uint8_t* out, uint32_t cap) {
    plat::LockGuard g(recvLock_);
    if (recv_.used() < 2) return 0;
    uint8_t hdr[2];
    recv_.peek(hdr, 2);
    uint32_t len = uint32_t(hdr[0] | (hdr[1] << 8));
    if (recv_.used() < 2 + len) return 0;
    recv_.consume(2);
    if (len > cap) {
        recv_.consume(len);
        return -1;
    }
    recv_.peek(out, len);
    recv_.consume(len);
    return int(len);
}

void Connection::threadEntry(void* self) { static_cast<Connection*>(self)->run(); }

void Connection::closeSocket(const char* why) {
    plat::tcpClose(fd_);
    fd_ = -1;
    heartbeat_ = false;
    plat::LockGuard g(sendLock_);
    error_ = why;
    status_ = Status::Disconnected;
}

void Connection::doConnect() {
    char host[sizeof(host_)];
    uint16_t port;
    {
        plat::LockGuard g(cmdLock_);
        memcpy(host, host_, sizeof(host));
        port = port_;
        wantConnect_ = false;
    }
    if (fd_ >= 0) closeSocket("reconnecting");
    status_ = Status::Connecting;
    const char* err = "";
    int fd = plat::tcpConnect(host, port, kConnectTimeoutMs, &err, &quit_);
    if (fd < 0) {
        plat::LockGuard g(sendLock_);
        error_ = err;
        status_ = Status::Disconnected;
        return;
    }
    fd_ = fd;
    {
        plat::LockGuard r(recvLock_);
        recv_.clear();
    }
    plat::LockGuard g(sendLock_);
    send_.clear();
    heartbeat_ = false;
    lastHeartbeatMs_ = plat::nowMs();
    error_ = "";
    session_ = session_ + 1;
    status_ = Status::Connected;
}

bool Connection::pumpSend() {
    for (;;) {
        uint32_t n;
        const uint8_t* p;
        {
            plat::LockGuard g(sendLock_);
            p = send_.readPtr(&n);
        }
        if (n == 0) return true;
        int sent = plat::tcpSend(fd_, p, int(n));
        if (sent < 0) return false;
        if (sent == 0) return true; // kernel buffer full; retry next loop
        {
            plat::LockGuard g(sendLock_);
            send_.consume(uint32_t(sent));
        }
        bytesOut_ = bytesOut_ + uint32_t(sent);
    }
}

bool Connection::pumpRecv() {
    for (;;) {
        uint32_t n;
        uint8_t* p;
        {
            plat::LockGuard g(recvLock_);
            p = recv_.writePtr(&n);
        }
        if (n == 0) return true; // ring full: back-pressure until the game drains it
        int got = plat::tcpRecv(fd_, p, int(n));
        if (got < 0) return false;
        if (got == 0) return true;
        {
            plat::LockGuard g(recvLock_);
            recv_.commit(uint32_t(got));
        }
        bytesIn_ = bytesIn_ + uint32_t(got);
    }
}

void Connection::run() {
    while (!quit_) {
        bool connectNow, disconnectNow;
        {
            plat::LockGuard g(cmdLock_);
            connectNow = wantConnect_;
            disconnectNow = wantDisconnect_;
            wantDisconnect_ = false;
        }
        if (connectNow) doConnect();
        if (disconnectNow && fd_ >= 0) {
            // Best-effort flush (CLIENT_DISCONNECT etc.) for up to 200 ms.
            uint32_t start = plat::nowMs();
            for (;;) {
                if (!pumpSend()) break;
                uint32_t pending;
                {
                    plat::LockGuard g(sendLock_);
                    pending = send_.used();
                }
                if (pending == 0 || plat::nowMs() - start > 200) break;
                plat::tcpWait(fd_, true, 10);
            }
            closeSocket("disconnected by client");
            plat::LockGuard g(sendLock_);
            status_ = Status::Idle;
            continue;
        }
        if (fd_ < 0) {
            plat::sleepMs(20);
            continue;
        }

        uint32_t now = plat::nowMs();
        if (heartbeat_ && now - lastHeartbeatMs_ >= kHeartbeatMs) {
            uint8_t hb[4] = {2, 0, uint8_t(msg::CLIENT_HEARTBEAT), uint8_t(msg::CLIENT_HEARTBEAT >> 8)};
            plat::LockGuard g(sendLock_);
            if (send_.push(hb, sizeof(hb))) lastHeartbeatMs_ = now;
        }

        if (!pumpSend()) { closeSocket("send failed (connection lost)"); continue; }
        uint32_t pending;
        {
            plat::LockGuard g(sendLock_);
            pending = send_.used();
        }
        // recv only when poll reports data (or a hang-up): on hardware every recv is a SOC
        // service call, and an idle loop runs ~100 times a second.
        const int ready = plat::tcpWait(fd_, pending > 0, pending > 0 ? 2 : 10);
        if ((ready & plat::kTcpReadable) && !pumpRecv()) { closeSocket("connection closed by server"); continue; }
        uint32_t freeSpace;
        {
            plat::LockGuard g(recvLock_);
            freeSpace = recv_.space();
        }
        if (freeSpace == 0) plat::sleepMs(5); // game is behind; don't spin on POLLIN
    }
}

} // namespace tt::net
