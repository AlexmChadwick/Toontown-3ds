// Thin platform layer for the net code: monotonic time, a mutex, one thread and
// TCP sockets. The 3DS build uses libctru (soc:U BSD sockets, LightLock,
// threadCreate); the host build (tools/nettest) uses POSIX. Everything else in
// source/net is platform independent so the protocol code is testable on a PC.
#pragma once

#include <cstddef>
#include <cstdint>

#ifdef __3DS__
// The umbrella header: only it wraps libctru in extern "C".
#include <3ds.h>
#else
#include <pthread.h>
#endif

namespace tt::net::plat {

uint32_t nowMs();             // monotonic milliseconds (wraps after 49 days)
void sleepMs(uint32_t ms);

class Mutex {
public:
    Mutex();
    ~Mutex();
    void lock();
    void unlock();
    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;

private:
#ifdef __3DS__
    LightLock lock_;
#else
    pthread_mutex_t lock_;
#endif
};

class LockGuard {
public:
    explicit LockGuard(Mutex& m) : m_(m) { m_.lock(); }
    ~LockGuard() { m_.unlock(); }
    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;

private:
    Mutex& m_;
};

// Sticky one-shot wake used to interrupt the network thread's idle sleep when the
// main thread queues output.  No allocation or socket call occurs in signal().
class Event {
public:
    Event();
    ~Event();
    void signal();
    void wait(uint32_t timeoutMs);
    Event(const Event&) = delete;
    Event& operator=(const Event&) = delete;

private:
#ifdef __3DS__
    LightEvent event_;
#else
    pthread_mutex_t lock_;
    pthread_cond_t cond_;
    bool signaled_ = false;
#endif
};

class Thread {
public:
    using Fn = void (*)(void*);
    // prio/core are only used on 3DS (0x18..0x3F, lower = more urgent).
    bool start(Fn fn, void* arg, int prio, int core, size_t stackSize);
    void join();
    bool running() const { return started_; }

private:
#ifdef __3DS__
    ::Thread thread_ = nullptr;
#else
    pthread_t thread_{};
#endif
    bool started_ = false;
};

// Socket service lifetime (soc:U on 3DS needs a 0x1000-aligned work buffer).
bool socketsInit();
void socketsExit();

// Blocking-with-timeout TCP connect; the returned socket is non-blocking.
// Returns -1 on failure (err describes why).
int tcpConnect(const char* host, uint16_t port, uint32_t timeoutMs, const char** err);
// Waits until the socket is readable (or, if wantWrite, writable). Returns a mask of
// kTcpReadable / kTcpWritable, 0 on timeout. Errors and hang-ups count as readable, so
// the next recv reports them.
constexpr int kTcpReadable = 1, kTcpWritable = 2;
int tcpWait(int fd, bool wantWrite, int timeoutMs);
// > 0 bytes moved; 0 would block; -1 closed by peer or error.
int tcpRecv(int fd, void* buf, int len);
int tcpSend(int fd, const void* buf, int len);
void tcpClose(int fd);

} // namespace tt::net::plat
