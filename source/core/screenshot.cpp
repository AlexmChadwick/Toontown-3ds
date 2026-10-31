#include "core/screenshot.h"

#include <cstdio>

#include "core/log.h"
#include "core/memory.h"

namespace tt::shot {

bool dumpTarget(C3D_RenderTarget* target, const char* path) {
    const C3D_FrameBuf& fb = target->frameBuf;
    const u32 size = C3D_CalcColorBufSize(fb.width, fb.height, fb.colorFmt);
    // Citro3D render targets live in VRAM. CPU reads from that address are not
    // a supported readback path and return zero on current Azahar memory modes.
    // Preserve the PICA's tiled bytes with a GPU copy into CPU-readable linear
    // memory. The caller has already fenced the completed frame at FrameBegin.
    void* readback = nullptr;
    {
        mem::LinearGuard guard;
        readback = linearAlloc(size);
    }
    if (!readback) {
        TT_LOG("shot: could not allocate %lu-byte readback", static_cast<unsigned long>(size));
        return false;
    }
    // Raw contiguous copy, matching Citro3D's own VRAM texture upload path.
    C3D_SyncTextureCopy(static_cast<u32*>(fb.colorBuf), 0,
                        static_cast<u32*>(readback), 0, size, GX_TRANSFER_RAW_COPY(1));
    GSPGPU_InvalidateDataCache(readback, size);
    FILE* f = fopen(path, "wb");
    if (!f) {
        mem::LinearGuard guard;
        linearFree(readback);
        return false;
    }
    // Header: magic, buffer width/height (GPU orientation: 240 x 400/320), color format.
    const u16 header[4] = {fb.width, fb.height, static_cast<u16>(fb.colorFmt), 0};
    fwrite("TTSH", 1, 4, f);
    fwrite(header, sizeof(header), 1, f);
    const bool ok = fwrite(readback, 1, size, f) == size;
    fclose(f);
    {
        mem::LinearGuard guard;
        linearFree(readback);
    }
    TT_LOG("shot: %s %s", path, ok ? "ok" : "FAILED");
    return ok;
}

} // namespace tt::shot
