#include "net/net_platform.h"

// <3ds.h> must come before libctru's BSD socket headers: they include parts of it
// without the extern "C" wrapper, after which <3ds.h>'s guards skip the C linkage.
#ifdef __3DS__
#include <3ds.h>
#include <malloc.h>
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

// ---------------------------------------------------------------- event
#ifdef __3DS__
Event::Event() { LightEvent_Init(&event_, RESET_ONESHOT); }
Event::~Event() {}
void Event::signal() { LightEvent_Signal(&event_); }
void Event::wait(uint32_t timeoutMs) { LightEvent_WaitTimeout(&event_, int64_t(timeoutMs) * 1000000LL); }
#else
Event::Event() {
    pthread_mutex_init(&lock_, nullptr);
    pthread_cond_init(&cond_, nullptr);
}
Event::~Event() {
    pthread_cond_destroy(&cond_);
    pthread_mutex_destroy(&lock_);
}
void Event::signal() {
    pthread_mutex_lock(&lock_);
    signaled_ = true;
    pthread_cond_signal(&cond_);
    pthread_mutex_unlock(&lock_);
}
void Event::wait(uint32_t timeoutMs) {
    timespec until;
    clock_gettime(CLOCK_REALTIME, &until);
    until.tv_sec += timeoutMs / 1000;
    until.tv_nsec += long(timeoutMs % 1000) * 1000000L;
    if (until.tv_nsec >= 1000000000L) { ++until.tv_sec; until.tv_nsec -= 1000000000L; }
    pthread_mutex_lock(&lock_);
    while (!signaled_ && pthread_cond_timedwait(&cond_, &lock_, &until) == 0) {}
    signaled_ = false;
    pthread_mutex_unlock(&lock_);
}
#endif

// ---------------------------------------------------------------- thread
#ifdef __3DS__
bool Thread::start(Fn fn, void* arg, int prio, int core, size_t stackSize) {
    thread_ = threadCreate(fn, arg, stackSize, prio, core, false);
    started_ = thread_ != nullptr;
    return started_;
}

void Thread::join() {
    if (!started_) return;
    threadJoin(thread_, U64_MAX);
    threadFree(thread_);
    thread_ = nullptr;
    started_ = false;
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

void Thread::join() {
    if (!started_) return;
    pthread_join(thread_, nullptr);
    started_ = false;
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

} // namespace

int tcpConnect(const char* host, uint16_t port, uint32_t timeoutMs, const char** err) {
    sockaddr_in addr;
    if (!resolve(host, port, &addr)) { *err = "cannot resolve host"; return -1; }
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) { *err = "socket() failed"; return -1; }
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
#ifdef SO_NOSIGPIPE
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);

    int rc = connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (rc != 0 && errno != EINPROGRESS && errno != EWOULDBLOCK && errno != EALREADY) {
        close(fd);
        *err = "connection refused";
        return -1;
    }
    if (rc != 0) {
        pollfd p{fd, POLLOUT, 0};
#ifdef __3DS__
        // Zero-timeout polls only (see tcpWait).
        int ready = 0;
        for (uint32_t waited = 0; (ready = poll(&p, 1, 0)) == 0 && waited < timeoutMs; waited += 10)
            sleepMs(10);
#else
        const int ready = poll(&p, 1, int(timeoutMs));
#endif
        if (ready <= 0) { close(fd); *err = "connect timed out"; return -1; }
        int soErr = 0;
        socklen_t len = sizeof(soErr);
        if ((p.revents & (POLLERR | POLLHUP)) ||
            (getsockopt(fd, SOL_SOCKET, SO_ERROR, &soErr, &len) == 0 && soErr != 0)) {
            close(fd);
            *err = "connection refused";
            return -1;
        }
    }
    *err = "";
    return fd;
}

int tcpWait(int fd, bool wantWrite, int timeoutMs) {
    pollfd p{fd, short(POLLIN | (wantWrite ? POLLOUT : 0)), 0};
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
    return ((p.revents & (POLLIN | POLLERR | POLLHUP | POLLNVAL)) ? kTcpReadable : 0) |
           ((p.revents & POLLOUT) ? kTcpWritable : 0);
}

int tcpRecv(int fd, void* buf, int len) {
    int n = int(recv(fd, buf, size_t(len), 0));
    if (n > 0) return n;
    if (n == 0) return -1; // orderly shutdown by peer
    return (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) ? 0 : -1;
}

int tcpSend(int fd, const void* buf, int len) {
#ifdef MSG_NOSIGNAL
    int n = int(send(fd, buf, size_t(len), MSG_NOSIGNAL));
#else
    int n = int(send(fd, buf, size_t(len), 0));
#endif
    if (n >= 0) return n;
    return (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) ? 0 : -1;
}

void tcpClose(int fd) {
    if (fd >= 0) close(fd);
}

} // namespace tt::net::plat
