// The minimap with its Go Home / place name / teleport controls, shared by the Map tab
// (render/ui.cpp) and the Shtiker Book's Map page, so both draw and clip the same way.
#pragma once

#include <3ds.h>
#include <citro2d.h>

namespace tt::ui {

struct Context;
struct State;
struct MapMarker;

// Marker colours; the legend beside the map names the ones present.
constexpr u32 kMarkToon = C2D_Color32(0x40, 0xF0, 0xFF, 0xFF);     // other players
constexpr u32 kMarkHq = C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF);       // Toon HQ
constexpr u32 kMarkShop = C2D_Color32(0xF5, 0xB8, 0x1A, 0xFF);     // landmark buildings
constexpr u32 kMarkTunnel = C2D_Color32(0xE0, 0x60, 0x20, 0xFF);   // tunnels to streets / hoods
constexpr u32 kMarkTrolley = C2D_Color32(0xB0, 0x5C, 0xE0, 0xFF);  // the trolley
constexpr u32 kMarkFishing = C2D_Color32(0x40, 0x90, 0xE0, 0xFF);  // fishing docks

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
// The static markers of a zone from its baked anchor names (bake_zone/bake_street .pts):
// playgrounds get every landmark, streets only the Toon HQ; both get tunnels, the trolley and
// one marker per fishing dock. names[i] is anchor i's name, xy[2i..] its position.
int placeMarkers(bool playground, const char* const* names, const float* xy, int count, MapMarker* out, int max);
// Press a pad::hoods control (kIdHome or a hood index) with the rules of upstream's MapPage:
// the left button is "Playground" off a playground (MapPage.backToSafeZone), else "Go Home";
// a hood teleports only when it is installed and in our teleportAccess. Uses the state of the
// last drawHoodControls call.
void pressHoodControl(int id, State& s);

} // namespace tt::ui
