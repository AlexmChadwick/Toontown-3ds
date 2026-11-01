#include "core/screenshot.h"

#include <cstdio>

#include "core/gpu_sync.h"
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
    // C3D_SyncTextureCopy is only synchronous with Citro3D's render queue: outside
    // a frame it can remain queued until the next FrameEnd. Writing the file here
    // then captured recycled linear memory from the preceding named shot. Fence the
    // completed render and submit a direct GX copy whose PPF event we wait for.
    gpu::frameSync();
    const Result copy = GX_TextureCopy(static_cast<u32*>(fb.colorBuf), 0,
                                       static_cast<u32*>(readback), 0, size,
                                       GX_TRANSFER_RAW_COPY(1));
    if (R_FAILED(copy)) {
        TT_LOG("shot: GX copy failed %08lX", static_cast<unsigned long>(copy));
        mem::LinearGuard guard;
        linearFree(readback);
        return false;
    }
    gspWaitForPPF();
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
