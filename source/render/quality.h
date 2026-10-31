// Common render distances preserve the same visible landmarks on Old and New.
// Model detection still controls optional hardware behavior and granted memory
// controls retention. Device timing of these Round 12 settings is pending.
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

// Frame admission remains 150 draws / 15k triangles. The baker must make
// visible content cheaper; a shorter Old 3DS fog band is not a budget tool.
// Every far mesh reserves its budget before optional near upgrades. Letter
// chunks remain eligible throughout the cone and use real near geometry.
inline Quality qualityFor(bool new3ds) {
    // Fidelity uses the same camera, detail band and fog on both machines.
    // Granted heaps change retention, never the visible landmark set.
    return {900.0f, 740.0f, 260.0f, 290.0f, 64.0f, 180.0f, 680.0f, new3ds};
}

inline Quality& activeQuality() {
    static Quality q = qualityFor(false);
    return q;
}

inline void setActiveQuality(bool new3ds) { activeQuality() = qualityFor(new3ds); }

} // namespace tt::render
