// Animated DNA props that move in the stock client on this server (no holidays run):
// GenericAnimatedProp street props (Dreamland / Melodyland / Donald's Dock / Brrrgh / TTC
// hydrants, mailboxes and trash cans that loop their DNA anim) and the TTC Toon HQ periscope
// and telescope (HQPeriscopeAnimatedProp / HQTelescopeAnimatedProp frame-range tracks).
// tools/pipeline/anim_props.py takes their geometry out of the static zone and writes
// data/animprops/<stem>.ttm + .tta and data/zones/<zone>.tap; the interactive props stay static
// (posed at idle0 frame 0, as InteractiveAnimatedProp.enter does without a holiday).
#pragma once

#include <citro3d.h>
#include <cstdint>

#include "render/anim.h"
#include "render/model.h"

namespace tt::render {
class Renderer;
}

namespace tt::world {

class AnimProps {
public:
    // On every zone load: reads <zone>.tap when the zone has one.
    void setZone(uint32_t zoneId);
    // Draws the nearest in-view props inside the profile draw distance, at most kMaxDrawn and
    // kTriangleBudget triangles (a Dreamland trash can is 1,368; the chunks no longer hold them).
    void draw(render::Renderer& renderer, float eyeX, float eyeY, uint32_t nowMs);
    void shutdown();

private:
    static constexpr int kMaxKinds = 8;
    static constexpr int kMaxInstances = 64;
    static constexpr int kMaxDrawn = 10;
    static constexpr u32 kTriangleBudget = 6000;
    struct Kind {
        char stem[64] = {};
        render::Model model;
        render::Anim anim;
    };
    struct Instance {
        C3D_Mtx world;
        float x, y;
        C3D_FVec centre; // bounding sphere over every frame, world space
        float radius;
        uint8_t kind, track;
    };
    Kind kinds_[kMaxKinds];
    int numKinds_ = 0;
    Instance instances_[kMaxInstances];
    int count_ = 0;
    uint32_t zone_ = 0;
};

} // namespace tt::world
