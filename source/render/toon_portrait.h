// A Toon's 3D head in a bottom-screen panel, as ToonAvatarPanel shows the avatar's ToonHead
// (fitAndCenterHead, blinking and looking around) above its name. The head is drawn by the world
// renderer into a 128 x 128 VRAM texture target, then shown with citro2d like any image.
// Main thread only; the target exists only while a panel asks for it.
#pragma once

#include <citro2d.h>
#include <citro3d.h>

namespace tt::world {
class ToonAvatar;
struct ToonLook;
} // namespace tt::world

namespace tt::render {

class Renderer;

class ToonPortrait {
public:
    static constexpr int kSize = 128;
    ~ToonPortrait() { release(); }

    // Renders the head of `toon` (look `look`, which must be the toon's) into the texture, on the
    // background `clearRgba8` (0xRRGGBBAA). Call between C3D_FrameBegin and the bottom screen's
    // citro2d pass, after C2D_Flush; the caller restores citro2d (endWorld, C2D_Prepare).
    // Returns false (and draws nothing) when the look is not loaded or the target cannot be made.
    bool render(Renderer& renderer, const world::ToonAvatar& toon, const world::ToonLook& look, u32 clearRgba8);
    // The last rendered head, drawn into (x, y, size, size). False before the first render.
    bool draw(float x, float y, float size, float z = 0.5f) const;
    void release();

private:
    C3D_Tex tex_{};
    C3D_RenderTarget* target_ = nullptr;
    bool rendered_ = false;
};

} // namespace tt::render
