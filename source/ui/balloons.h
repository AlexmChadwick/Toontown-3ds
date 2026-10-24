// Top-screen nametags and SpeedChat speech balloons (citro2d, drawn after the 3D pass).
//
// Per frame:
//   balloons.begin();
//   for each visible toon: projectToScreen(...) -> balloons.addSpeaker(sx, sy, name, phrase, age, depth);
//   renderer.endWorld(); C2D_Prepare();
//   balloons.draw(renderer.topLeft());           // (and topRight() in stereo)
//
// Strings are copied into an internal cache and parsed only when they change, so the
// caller may pass temporary buffers. Speech replaces the nametag while it is shown, as
// in Toontown (Nametag3d hides the name while a chat balloon is up).
#pragma once

#include <3ds.h>
#include <citro2d.h>

#include "ui/projection.h"

namespace tt::ui {

class Balloons {
public:
    static constexpr int kMaxSpeakers = 12;
    static constexpr float kHoldSec = 10.0f; // CFTimeout: balloon visible this long ...
    static constexpr float kFadeSec = 0.6f;  // ... then fades out over this long
    static constexpr float kMaxTextW = 160.0f;
    // Approximation of libotp's CCNormal nametag (the C++ nametag code is not in ref/).
    static constexpr u32 kNameColorToon = 0xFFC04020; // ABGR: RGB(0x20,0x40,0xC0)

    bool init();
    void shutdown();

    // Clears the speaker list for this frame.
    void begin();
    // (sx, sy): top-screen pixel just above the head (projectToScreen of head-top + ~0.5
    // units). phrase may be null/empty for "nametag only". ageSec is the time since the
    // phrase was said; past kHoldSec + kFadeSec the nametag is shown instead. depth (eye
    // distance, from projectToScreen) orders drawing far-to-near. Extra speakers beyond
    // kMaxSpeakers are dropped (callers should add nearest first).
    void addSpeaker(float sx, float sy, const char* name, const char* phrase, float ageSec, float depth = 0.0f,
                    u32 nameColor = kNameColorToon);
    // Draws on the top target. Must be inside C3D_FrameBegin/End, after Renderer::endWorld()
    // and C2D_Prepare(). parallaxPx shifts everything horizontally (stereo: +/- per eye).
    void draw(C3D_RenderTarget* top, float parallaxPx = 0.0f);

private:
    static constexpr int kCacheSlots = 40;
    static constexpr int kMaxStr = 160;
    static constexpr int kGlyphCapacity = 3072;

    struct Entry {
        char str[kMaxStr]; // source string (unwrapped)
        u8 wrapped;        // 1 = word-wrapped to kMaxTextW (phrases), 0 = single line (names)
        u8 used;
        C2D_Text text;
        float w, h; // at the draw scale
    };
    struct Speaker {
        float sx, sy, age, depth;
        const char* name;
        const char* phrase;
        u32 nameColor;
        int nameEntry, phraseEntry;
    };

    int resolve(const char* s, bool wrap); // -1 if the cache must be flushed first
    void flush();
    void wrapInto(const char* src, char* dst, int cap) const;

    C2D_TextBuf buf_ = nullptr;
    Entry* cache_ = nullptr; // kCacheSlots, heap (keeps the object small)
    Speaker speakers_[kMaxSpeakers];
    int numSpeakers_ = 0;
    float fontScale_ = 1.0f; // citro2d's 30 / cellHeight for the system font
};

} // namespace tt::ui
