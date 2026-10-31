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
// range and the cutoff farther. A chunk whose mesh carries MickeyFont sign letters
// (`palette_2tmlc`) upgrades out to twice lodNear, ahead of a generic chunk, so
// Donald's Dock's far building does not stay on the 14-triangle far letters.
// When a camera is set, Zone spends the 15k-triangle and 130-draw caps
// (Zone::kDrawBudget) on near upgrades, and the submitted set also stays within
// 150 draws. A resident near mesh inside lodMust may stay up to that 150.
// lodKeep does not bypass the cap. Chunks outside the cone keep their near mesh
// until the retain budget evicts it, and that mesh is not replaced by the far
// blob. Their far textures do not count against a sign's upgrade. Inside the
// cone every chunk that fits draws at least its far mesh. A mesh that is in
// view is not evicted or swapped out until its replacement is published.
// The loading card waits for the distant chunks that cover most of the screen.
// The cell and lodMust read their near mesh directly. A choice-0 chunk that is
// not in the card reads near once. Up to two on-screen signs, ranked by
// letter-texel density, may demote the generic near mesh blocking them. The
// submitted set counts the mesh on screen (150 draws and 15k triangles), so a
// published far stays up while its near mesh is still loading. With no camera
// the whole cell stays inside 15k triangles and 150 draws.
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
