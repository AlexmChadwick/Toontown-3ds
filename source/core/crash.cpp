#include "core/crash.h"

#include <3ds.h>

#include "core/log.h"

namespace tt::crash {

namespace {

// threadOnException runs the handler on this stack, with the exception data written to it.
constexpr size_t kHandlerStackSize = 0x2000;
alignas(8) u8 gHandlerStack[kHandlerStackSize];

const char* typeName(ERRF_ExceptionType type) {
    switch (type) {
    case ERRF_EXCEPTION_PREFETCH_ABORT: return "prefetch abort";
    case ERRF_EXCEPTION_DATA_ABORT: return "data abort";
    case ERRF_EXCEPTION_UNDEFINED: return "undefined instruction";
    case ERRF_EXCEPTION_VFP: return "VFP exception";
    default: return "unknown exception";
    }
}

[[noreturn]] void onException(ERRF_ExceptionInfo* info, CpuRegisters* regs) {
    TT_LOG("CRASH: %s at pc %08lx (fault address %08lx, status %08lx)", typeName(info->type),
           static_cast<unsigned long>(regs->pc), static_cast<unsigned long>(info->far),
           static_cast<unsigned long>(info->fsr));
    TT_LOG("CRASH: lr %08lx sp %08lx cpsr %08lx", static_cast<unsigned long>(regs->lr),
           static_cast<unsigned long>(regs->sp), static_cast<unsigned long>(regs->cpsr));
    for (int i = 0; i < 13; i += 4) {
        TT_LOG("CRASH: r%d-r%d %08lx %08lx %08lx %08lx", i, i + 3, static_cast<unsigned long>(regs->r[i]),
               static_cast<unsigned long>(i + 1 < 13 ? regs->r[i + 1] : 0),
               static_cast<unsigned long>(i + 2 < 13 ? regs->r[i + 2] : 0),
               static_cast<unsigned long>(i + 3 < 13 ? regs->r[i + 3] : 0));
    }
    log::flush();
    svcExitProcess();
    for (;;) {
    }
}

} // namespace

void installForThisThread() {
    threadOnException(onException, gHandlerStack + kHandlerStackSize, WRITE_DATA_TO_HANDLER_STACK);
}

} // namespace tt::crash
