// Raw render-target dumps to SD for unattended verification. Decode on PC with
// tools/dev/decode_shot.py. Call after C3D_FrameBegin (previous frame's GPU work is
// complete) and before the target is cleared.
#pragma once

#include <citro3d.h>

namespace tt::shot {

bool dumpTarget(C3D_RenderTarget* target, const char* path);

} // namespace tt::shot
