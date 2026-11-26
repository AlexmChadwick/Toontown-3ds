#include "core/crash.h"

#include <3ds.h>
#include <cstdio>

#include "core/log.h"

namespace tt::crash {

namespace {

// threadOnException runs the handler on this stack, with the exception data written to it.
// Big enough for the console + printf of the on-screen report.
// One handler stack per thread (two threads faulting together must not share one): the
// first install (the main thread) gets the large one, the worker threads the small ones.
constexpr size_t kHandlerStackSize = 0x8000;
constexpr size_t kWorkerStackSize = 0x4000;
constexpr int kWorkerStacks = 6; // asset loader, toon cache, zone loader, music stream, net, spare
alignas(8) u8 gHandlerStack[kHandlerStackSize];
alignas(8) u8 gWorkerStacks[kWorkerStacks][kWorkerStackSize];
volatile int gInstalls = 0;
volatile bool gPanicking = false;
bool gScreensReady = false;

const char* typeName(ERRF_ExceptionType type) {
    switch (type) {
    case ERRF_EXCEPTION_PREFETCH_ABORT: return "prefetch abort";
    case ERRF_EXCEPTION_DATA_ABORT: return "data abort";
    case ERRF_EXCEPTION_UNDEFINED: return "undefined instruction";
    case ERRF_EXCEPTION_VFP: return "VFP exception";
    default: return "unknown exception";
    }
}

} // namespace

void screensReady() { gScreensReady = true; }
bool panicking() { return gPanicking; }

// Shows what happened on both screens with the CPU console (no GPU needed), waits for START,
// then ends the process. The HOME Menu may still show its own error after that; the point is
// that the player (and a phone video) sees the reason first, and log.txt has it too.
[[noreturn]] void panic(const char* what, const char* line1, const char* line2) {
    const bool first = !gPanicking;
    gPanicking = true;
    if (first) {
        TT_LOG("PANIC: %s", what);
        if (line1 && *line1) TT_LOG("PANIC: %s", line1);
        if (line2 && *line2) TT_LOG("PANIC: %s", line2);
    }
    log::flush();
    if (first && gScreensReady) {
        gfxSet3D(false);
        static PrintConsole top, bottom;
        consoleInit(GFX_TOP, &top);
        consoleInit(GFX_BOTTOM, &bottom);
        consoleSelect(&top);
        printf("\x1b[2;2H\x1b[33mToontown 3DS hit a snag :(\x1b[0m");
        printf("\x1b[5;2H%s", what);
        if (line1) printf("\x1b[7;2H%s", line1);
        if (line2) printf("\x1b[8;2H%s", line2);
        printf("\x1b[11;2HDetails are in sdmc:/3ds/toontown3ds/log.txt");
        printf("\x1b[13;2HPress START to close.");
        consoleSelect(&bottom);
        printf("\x1b[2;1HPlease send a photo of this screen\n to the developer. Thanks, toon!\n");
        for (int i = 0; i < log::kRingLines; i++) {
            const char* l = log::recent(log::kRingLines - 1 - i);
            if (*l) printf("\x1b[%d;1H%.39s", 6 + i * 2, l);
        }
        gfxFlushBuffers();
        gfxSwapBuffers();
        if (FILE* t = fopen("sdmc:/3ds/toontown3ds/panic_test", "r")) { // test runs: keep what was shown
            fclose(t);
            u16 w, h;
            const u8* fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, &w, &h); // the buffer just shown
            if (FILE* o = fopen("sdmc:/3ds/toontown3ds/panic_top.raw", "wb")) {
                fwrite(fb, 1, size_t(w) * h * 3, o);
                fclose(o);
            }
        }
        for (int frames = 0; frames < 60 * 60 * 10; frames++) {
            hidScanInput();
            if (hidKeysDown() & KEY_START) break;
            gspWaitForVBlank();
        }
    }
    svcExitProcess();
    for (;;) {
    }
}

namespace {

[[noreturn]] void onException(ERRF_ExceptionInfo* info, CpuRegisters* regs) {
    char l1[64], l2[64];
    snprintf(l1, sizeof(l1), "pc %08lx lr %08lx", static_cast<unsigned long>(regs->pc),
             static_cast<unsigned long>(regs->lr));
    snprintf(l2, sizeof(l2), "addr %08lx status %08lx sp %08lx", static_cast<unsigned long>(info->far),
             static_cast<unsigned long>(info->fsr), static_cast<unsigned long>(regs->sp));
    for (int i = 0; i < 13; i += 4) {
        TT_LOG("CRASH: r%d-r%d %08lx %08lx %08lx %08lx", i, i + 3, static_cast<unsigned long>(regs->r[i]),
               static_cast<unsigned long>(i + 1 < 13 ? regs->r[i + 1] : 0),
               static_cast<unsigned long>(i + 2 < 13 ? regs->r[i + 2] : 0),
               static_cast<unsigned long>(i + 3 < 13 ? regs->r[i + 3] : 0));
    }
    char what[48];
    snprintf(what, sizeof(what), "CPU %s", typeName(info->type));
    panic(what, l1, l2);
}

} // namespace

void installForThisThread() {
    const int slot = __atomic_fetch_add(&gInstalls, 1, __ATOMIC_SEQ_CST);
    u8* top = slot == 0 ? gHandlerStack + kHandlerStackSize
                        : gWorkerStacks[(slot - 1) % kWorkerStacks] + kWorkerStackSize; // threads that restart reuse slots
    threadOnException(onException, top, WRITE_DATA_TO_HANDLER_STACK);
}

} // namespace tt::crash

// Every svcBreak (libctru/citro3d/citro2d internal checks) and abort (uncaught C++ errors such as
// a failed operator new, asserts) goes through here: on screen and in the log instead of the
// HOME Menu's bare "An error has occurred". Linked with -Wl,--wrap=svcBreak,--wrap=abort.
extern "C" {
void __real_svcBreak(UserBreakType reason);

void __wrap_svcBreak(UserBreakType reason) {
    char l1[64];
    snprintf(l1, sizeof(l1), "reason %d from %08lx", static_cast<int>(reason),
             reinterpret_cast<unsigned long>(__builtin_return_address(0)));
    tt::crash::panic("system check failed (svcBreak)", l1, nullptr);
}

void __wrap_abort(void) {
    char l1[64];
    snprintf(l1, sizeof(l1), "from %08lx", reinterpret_cast<unsigned long>(__builtin_return_address(0)));
    tt::crash::panic("abort (out of memory or internal error)", l1, nullptr);
}
}
