// Runtime render profile. Old 3DS is the baseline; New 3DS (APT_CheckNew3DS) keeps
// the high LOD farther and draws farther. Distances are toon units. The playground
// baker's --distance 760 plus a 78-unit margin (camera orbit plus the coarsest
// far-mesh snap) covers the New 3DS draw distance.
// Old 3DS loads a shorter radius of that same set.
#pragma once

namespace tt::render {

struct Quality {
    float farPlane;
    float drawDistance;
    float lodNear; // upgrade a chunk to the high mesh inside this distance
    float lodKeep; // hysteresis: a chunk already high stays high out to here
    // Chunks this close are the ones the nearer-first sort upgrades before anything
    // farther. They still share the 130-draw cap of what the frame submits. A separate
    // 150-draw envelope for this band counted chunks behind the camera and overran DD.
    float lodMust;
    float fogStart;
    float fogEnd;
    bool new3ds;
};

// The far plane sits past the fog so the sky is the fog colour, not a clip edge.
// High LOD stays near the camera; past lodNear the chunk is the coarse far mesh.
// Fog is already falling there, which hides the swap. New 3DS keeps the sharp
// range and the cutoff farther. When a camera is set, Zone spends the 15k-triangle
// and 130-draw caps (Zone::kDrawBudget) on chunks that frame will submit, and
// still refuses an upgrade past 6 MiB geometry or 8 MiB textures for the whole set.
inline Quality qualityFor(bool new3ds) {
    if (new3ds) return {900.0f, 740.0f, 260.0f, 290.0f, 64.0f, 180.0f, 680.0f, true};
    return {520.0f, 440.0f, 160.0f, 185.0f, 48.0f, 110.0f, 400.0f, false};
}

inline Quality& activeQuality() {
    static Quality q = qualityFor(false);
    return q;
}

inline void setActiveQuality(bool new3ds) { activeQuality() = qualityFor(new3ds); }

} // namespace tt::render
