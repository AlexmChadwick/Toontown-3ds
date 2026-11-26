#include "net/net_platform.h"

#include "core/log.h"

// <3ds.h> must come before libctru's BSD socket headers: they include parts of it
// without the extern "C" wrapper, after which <3ds.h>'s guards skip the C linkage.
#ifdef __3DS__
#include <3ds.h>
#include <malloc.h>
#include "core/gpu_sync.h"
#endif

#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef __3DS__
#include <ctime>
#endif

namespace tt::net::plat {

// ---------------------------------------------------------------- time
uint32_t nowMs() {
#ifdef __3DS__
    return uint32_t(svcGetSystemTick() * 1000ull / uint64_t(SYSCLOCK_ARM11));
#else
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return uint32_t(uint64_t(ts.tv_sec) * 1000u + uint64_t(ts.tv_nsec) / 1000000u);
#endif
}

void sleepMs(uint32_t ms) {
#ifdef __3DS__
    svcSleepThread(int64_t(ms) * 1000000LL);
#else
    timespec ts{time_t(ms / 1000), long(ms % 1000) * 1000000L};
    nanosleep(&ts, nullptr);
#endif
}

// ---------------------------------------------------------------- mutex
#ifdef __3DS__
Mutex::Mutex() { LightLock_Init(&lock_); }
Mutex::~Mutex() {}
void Mutex::lock() { LightLock_Lock(&lock_); }
void Mutex::unlock() { LightLock_Unlock(&lock_); }
#else
Mutex::Mutex() { pthread_mutex_init(&lock_, nullptr); }
Mutex::~Mutex() { pthread_mutex_destroy(&lock_); }
void Mutex::lock() { pthread_mutex_lock(&lock_); }
void Mutex::unlock() { pthread_mutex_unlock(&lock_); }
#endif

// ---------------------------------------------------------------- thread
#ifdef __3DS__
bool Thread::start(Fn fn, void* arg, int prio, int core, size_t stackSize) {
    thread_ = threadCreate(fn, arg, stackSize, prio, core, false);
    started_ = thread_ != nullptr;
    return started_;
}

bool Thread::join() {
    if (!started_) return true;
    const bool joined = ::tt::gpu::joinBounded(thread_, "net");
    thread_ = nullptr;
    started_ = false;
    return joined;
}
#else
namespace {
struct Trampoline {
    Thread::Fn fn;
    void* arg;
};
Trampoline gTrampoline; // one net thread at a time

void* trampoline(void* p) {
    Trampoline* t = static_cast<Trampoline*>(p);
    t->fn(t->arg);
    return nullptr;
}
} // namespace

bool Thread::start(Fn fn, void* arg, int, int, size_t) {
    gTrampoline = {fn, arg};
    started_ = pthread_create(&thread_, nullptr, trampoline, &gTrampoline) == 0;
    return started_;
}

bool Thread::join() {
    if (!started_) return true;
    pthread_join(thread_, nullptr);
    started_ = false;
    return true;
}
#endif

// ---------------------------------------------------------------- sockets
#ifdef __3DS__
namespace {
constexpr uint32_t kSocBufferSize = 0x100000; // 1 MB, the usual soc:U work area
uint32_t* gSocBuffer = nullptr;
} // namespace

bool socketsInit() {
    if (gSocBuffer) return true;
    gSocBuffer = static_cast<uint32_t*>(memalign(0x1000, kSocBufferSize));
    if (!gSocBuffer) return false;
    if (R_FAILED(socInit(gSocBuffer, kSocBufferSize))) {
        free(gSocBuffer);
        gSocBuffer = nullptr;
        return false;
    }
    return true;
}

void socketsExit() {
    if (!gSocBuffer) return;
    socExit();
    free(gSocBuffer);
    gSocBuffer = nullptr;
}
#else
bool socketsInit() { return true; }
void socketsExit() {}
#endif

namespace {

bool resolve(const char* host, uint16_t port, sockaddr_in* out) {
    memset(out, 0, sizeof(*out));
    out->sin_family = AF_INET;
    out->sin_port = htons(port);
    if (inet_pton(AF_INET, host, &out->sin_addr) == 1) return true;
    addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(host, nullptr, &hints, &res) != 0 || !res) return false;
    out->sin_addr = reinterpret_cast<sockaddr_in*>(res->ai_addr)->sin_addr;
    freeaddrinfo(res);
    return true;
}

// Poll event bits. The 3DS SOC has no POLLERR / POLLHUP (3dbrew SOCU:poll): 0x08 is
// POLLWRNORM, the real "writable" bit, and 0x10 POLLWRBAND, which libctru's poll.h calls
// POLLOUT. libctru calls 0x08 POLLERR ("probably"), so a writable socket used to read as an
// error, which is how real hardware got "connection refused" right after the handshake.
#ifdef __3DS__
constexpr short kPollWritable = 0x08 | 0x10;
constexpr short kPollReadable = POLLIN | POLLNVAL;
#else
constexpr short kPollWritable = POLLOUT;
constexpr short kPollReadable = POLLIN | POLLERR | POLLHUP | POLLNVAL;
#endif

// Detailed text for the last failure (net thread only; the caller copies it).
char gConnectErr[96];
char gIoErr[64];

const char* connectFail(int fd, const char* step, int err, int revents) {
    if (fd >= 0) close(fd);
    int n = snprintf(gConnectErr, sizeof(gConnectErr), "Couldn't connect: %s errno %d", step, err);
    if (err < 0 && n > 0 && size_t(n) < sizeof(gConnectErr))
        n += snprintf(gConnectErr + n, sizeof(gConnectErr) - size_t(n), " (raw SOC)");
    if (revents >= 0 && n > 0 && size_t(n) < sizeof(gConnectErr))
        snprintf(gConnectErr + n, sizeof(gConnectErr) - size_t(n), " (revents 0x%02x)", unsigned(revents));
    return gConnectErr;
}

bool connectPending(int e) { return e == EINPROGRESS || e == EALREADY || e == EWOULDBLOCK || e == EAGAIN; }

} // namespace

int tcpConnect(const char* host, uint16_t port, uint32_t timeoutMs, const char** err, const volatile bool* abort) {
    const uint32_t t0 = nowMs();
    sockaddr_in addr;
    if (!resolve(host, port, &addr)) {
        snprintf(gConnectErr, sizeof(gConnectErr), "Couldn't connect: resolve failed for %s", host);
        *err = gConnectErr;
        return -1;
    }
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) { *err = connectFail(-1, "socket", errno, -1); return -1; }
    int one = 1;
    // Latency only; a SOC that refuses the option still connects.
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one)) != 0)
        TT_LOG("net: setsockopt TCP_NODELAY failed (errno %d), continuing", errno);
#ifdef SO_NOSIGPIPE
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
        *err = connectFail(fd, "fcntl", errno, -1);
        return -1;
    }

    int revents = 0;
    int rc = connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (rc != 0 && errno != EISCONN) {
        if (!connectPending(errno)) { *err = connectFail(fd, "connect", errno, -1); return -1; }
        // Completion is decided by calling connect() again (0 / EISCONN = connected), never by
        // SO_ERROR: on real hardware getsockopt(SO_ERROR) returns the raw SOC code -26
        // (EINPROGRESS) after a successful connect (libctru#412), and there is no POLLERR.
        bool connected = false;
        for (uint32_t i = 0;; ++i) {
            pollfd p{fd, short(POLLIN | kPollWritable), 0};
#ifdef __3DS__
            // Zero-timeout polls only (see tcpWait); the wait is a guest sleep.
            const int ready = poll(&p, 1, 0);
#else
            const int ready = poll(&p, 1, 10);
#endif
            if (ready < 0) { *err = connectFail(fd, "poll", errno, -1); return -1; }
            if (ready > 0) revents = p.revents;
            // Also recheck every ~100 ms without an event, in case the SOC never reports one.
            if (ready > 0 || i % 10 == 9) {
                rc = connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
                if (rc == 0 || errno == EISCONN) { connected = true; break; }
                if (!connectPending(errno)) {
                    *err = connectFail(fd, "connect-recheck", errno, ready > 0 ? p.revents : -1);
                    return -1;
                }
            }
            if (nowMs() - t0 >= timeoutMs) break;
            if (abort && *abort) break;
#ifdef __3DS__
            sleepMs(10);
#endif
        }
        if (!connected) {
            snprintf(gConnectErr, sizeof(gConnectErr), "Couldn't connect: timed out after %u ms (revents 0x%02x)",
                     unsigned(timeoutMs), unsigned(revents));
            close(fd);
            *err = gConnectErr;
            return -1;
        }
    }
    sockaddr_in peer;
    socklen_t peerLen = sizeof(peer);
    if (getpeername(fd, reinterpret_cast<sockaddr*>(&peer), &peerLen) != 0) {
        *err = connectFail(fd, "getpeername", errno, revents);
        return -1;
    }
    TT_LOG("net: connected to %s:%u in %u ms, revents 0x%02x", host, unsigned(port), unsigned(nowMs() - t0),
           unsigned(revents));
    gIoErr[0] = 0;
    *err = "";
    return fd;
}

int tcpWait(int fd, bool wantWrite, int timeoutMs) {
    pollfd p{fd, short(POLLIN | (wantWrite ? kPollWritable : 0)), 0};
#ifdef __3DS__
    // Never hand soc:U a poll timeout: Azahar runs a timed poll on a fresh host thread
    // (HLE RunAsync) and wakes the guest from there, and on the Linux rig its emulated clock
    // froze for minutes until Astron closed the socket. A zero-timeout poll is serviced
    // inline; the wait is a guest sleep instead (one SOC call per call either way).
    const int n = poll(&p, 1, 0);
    if (n == 0 && timeoutMs > 0) sleepMs(uint32_t(timeoutMs));
    if (n <= 0) return 0;
#else
    if (poll(&p, 1, timeoutMs) <= 0) return 0;
#endif
    return ((p.revents & kPollReadable) ? kTcpReadable : 0) | ((p.revents & kPollWritable) ? kTcpWritable : 0);
}

int tcpRecv(int fd, void* buf, int len) {
    int n = int(recv(fd, buf, size_t(len), 0));
    if (n > 0) return n;
    if (n == 0) { // orderly shutdown by peer
        snprintf(gIoErr, sizeof(gIoErr), "recv: peer closed");
        return -1;
    }
    const int e = errno;
    if (e == EAGAIN || e == EWOULDBLOCK || e == EINTR) return 0;
    snprintf(gIoErr, sizeof(gIoErr), "recv errno %d", e);
    return -1;
}

int tcpSend(int fd, const void* buf, int len) {
#ifdef MSG_NOSIGNAL
    int n = int(send(fd, buf, size_t(len), MSG_NOSIGNAL));
#else
    int n = int(send(fd, buf, size_t(len), 0));
#endif
    if (n >= 0) return n;
    const int e = errno;
    if (e == EAGAIN || e == EWOULDBLOCK || e == EINTR) return 0;
    snprintf(gIoErr, sizeof(gIoErr), "send errno %d", e);
    return -1;
}

const char* tcpLastIoError() { return gIoErr; }

void tcpClose(int fd) {
    if (fd < 0) return;
    shutdown(fd, SHUT_RDWR); // wake anything still blocked on it before the descriptor goes
    close(fd);
}

} // namespace tt::net::plat
