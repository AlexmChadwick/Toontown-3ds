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
    // Chunks this close are upgraded first, against the 150-draw frame envelope rather than
    // Zone::kDrawBudget: the far mesh is vertex-coloured and visibly bloated up close, and a
    // nearest-first pass alone let a chunk under the toon stay far while distant ones went high.
    float lodMust;
    float fogStart;
    float fogEnd;
    bool new3ds;
};

// The far plane sits past the fog so the sky is the fog colour, not a clip edge.
// High LOD stays near the camera; past lodNear the chunk is the coarse far mesh.
// Fog is already falling there, which hides the swap. New 3DS keeps the sharp
// range and the cutoff farther. Zone still refuses a high-LOD upgrade that would
// pass 15k triangles, 130 draws (Zone::kDrawBudget), 6 MiB geometry or 8 MiB textures.
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
