#include "core/screenshot.h"

#include <cstdio>

#include "core/log.h"

namespace tt::shot {

bool dumpTarget(C3D_RenderTarget* target, const char* path) {
    const C3D_FrameBuf& fb = target->frameBuf;
    const u32 size = C3D_CalcColorBufSize(fb.width, fb.height, fb.colorFmt);
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    // Header: magic, buffer width/height (GPU orientation: 240 x 400/320), color format.
    const u16 header[4] = {fb.width, fb.height, static_cast<u16>(fb.colorFmt), 0};
    fwrite("TTSH", 1, 4, f);
    fwrite(header, sizeof(header), 1, f);
    const bool ok = fwrite(fb.colorBuf, 1, size, f) == size;
    fclose(f);
    TT_LOG("shot: %s %s", path, ok ? "ok" : "FAILED");
    return ok;
}

} // namespace tt::shot
