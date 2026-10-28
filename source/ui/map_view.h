// The minimap with its Go Home / place name / teleport controls, shared by the Map tab
// (render/ui.cpp) and the Shtiker Book's Map page, so both draw and clip the same way.
#pragma once

#include <3ds.h>
#include <citro2d.h>

namespace tt::ui {

struct Context;

struct MapSource {
    const C2D_Image* image = nullptr; // baked .tmap, or null (zone bounds on plain ground)
    float minX = 0, minY = 0, maxX = 1, maxY = 1;
    unsigned w = 0, h = 0;
};

// Paper frame at (x, y, w, h) holding the map, markers and the player arrow; nothing is drawn
// outside the frame.
void drawMapFrame(const Context& c, const MapSource& map, float x, float y, float w, float h);
// Go Home, the place label and the teleport row at the pad::hoods positions.
void drawHoodControls(const Context& c);

} // namespace tt::ui
