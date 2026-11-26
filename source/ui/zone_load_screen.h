// Toontown loading screen for zone transitions. Top: the baked progress card,
// logo and tip scroll. Bottom: the same art, never a black frame.
#pragma once

#include <3ds.h>
#include <citro3d.h>

namespace tt::render {
class Renderer;
}

namespace tt::ui {

class ZoneLoadScreen {
public:
    // 3D card. Call inside beginWorld, once per eye.
    void drawCard(render::Renderer& renderer, const C3D_FVec& eye, const C3D_FVec& at);
    // Citro2d overlay on the top target(s) plus the bottom screen. Call after endWorld.
    void drawUi(render::Renderer& renderer, C3D_RenderTarget* bottom, bool stereo, u32 zoneId, float progress);
    // Frees the baked card after the world is about to show, so it does not sit in linear memory.
    void release();

private:
    void ensure();
    class Models;
    Models* models_ = nullptr;
    u32 tip_ = 0;
    bool tried_ = false;
};

} // namespace tt::ui
