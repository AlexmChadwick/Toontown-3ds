// Bottom-screen look shared by the tab panels (docs/design/ui_theme.md).
//
// Toontown's 2D interface is cartoon paper and candy colours, not flat dark rectangles:
// - SpeedChat uses SCColorScheme's defaults: a pale lavender frame (derived from the blue
//   arrow colour 0.5, 0.5, 1), black ImpressBT text and a green rollover (0.53, 0.9, 0.53).
// - The Shtiker Book is cream paper inside a brown/red binder.
// - Buttons are rounded and drop a darker "shadow" lip below them.
// The sky gradient behind the panels is the playground sky the top screen shows.
#pragma once

#include <3ds.h>
#include <citro2d.h>

namespace tt::ui::theme {

constexpr u32 rgb(u8 r, u8 g, u8 b, u8 a = 0xFF) {
    return u32(r) | u32(g) << 8 | u32(b) << 16 | u32(a) << 24; // C2D_Color32
}

// Sky behind every tab.
constexpr u32 kSkyTop = rgb(0x6E, 0xC6, 0xF5);
constexpr u32 kSkyBottom = rgb(0xC4, 0xEA, 0xFF);
// Paper (Shtiker Book page) and its pencil-brown edge.
constexpr u32 kPaper = rgb(0xFF, 0xF8, 0xE4);
constexpr u32 kPaperShade = rgb(0xF1, 0xE6, 0xC8);
constexpr u32 kPaperEdge = rgb(0x8C, 0x5E, 0x34);
// Book binder colours (tab bar).
constexpr u32 kBinder = rgb(0x7A, 0x1E, 0x18);
constexpr u32 kBinderDark = rgb(0x52, 0x10, 0x0C);
// Text.
constexpr u32 kInk = rgb(0x2A, 0x1A, 0x0C);
constexpr u32 kInkSoft = rgb(0x7A, 0x66, 0x50);
constexpr u32 kWhite = rgb(0xFF, 0xFF, 0xFF);
// Candy accents.
constexpr u32 kYellow = rgb(0xFF, 0xD3, 0x3C);
constexpr u32 kYellowDark = rgb(0xC8, 0x92, 0x10);
constexpr u32 kOrange = rgb(0xF7, 0x8F, 0x1E);
constexpr u32 kRed = rgb(0xE0, 0x3A, 0x2E);
constexpr u32 kRedDark = rgb(0x9C, 0x1E, 0x16);
constexpr u32 kBlue = rgb(0x4F, 0x8C, 0xE8);
constexpr u32 kBlueDark = rgb(0x2B, 0x58, 0xA8);
constexpr u32 kGreen = rgb(0x5C, 0xC0, 0x4E);
constexpr u32 kGreenDark = rgb(0x2E, 0x7E, 0x26);
constexpr u32 kGrey = rgb(0xC8, 0xC2, 0xB6);
constexpr u32 kGreyDark = rgb(0x8E, 0x86, 0x7A);
// SpeedChat (SCColorScheme defaults, alpha 0.95 dropped: the bottom screen has nothing behind).
constexpr u32 kScFrame = rgb(0xF2, 0xF2, 0xFF);
constexpr u32 kScFrameEdge = rgb(0x80, 0x80, 0xFF); // arrowColor
constexpr u32 kScRollover = rgb(0x87, 0xE6, 0x87);
constexpr u32 kScPressed = rgb(0x79, 0xD4, 0x79); // rollover * 0.92 (luma)
constexpr u32 kScMenuOpen = rgb(0x6E, 0xC2, 0x6E); // menuHolderActiveColor: rollover * 0.84
constexpr u32 kScText = rgb(0x00, 0x00, 0x00);
constexpr u32 kScArrow = rgb(0x80, 0x80, 0xFF);
// ToontownBattleGlobals.TrackColors (Toon-Up, Trap, Lure, Sound, Throw, Squirt, Drop).
constexpr u32 kTrackColors[7] = {rgb(211, 148, 255), rgb(249, 255, 93), rgb(79, 190, 76),  rgb(93, 108, 239),
                                 rgb(255, 145, 66),  rgb(255, 65, 199), rgb(67, 243, 255)};

// Track names as the Shtiker Book's Gags page prints them (gags::kTrackNames is lower case).
inline constexpr const char* kTrackTitles[7] = {"Toon-Up", "Trap", "Lure", "Sound", "Throw", "Squirt", "Drop"};

// Text scale ladder for the bottom screen (ImpressBT, ~30 px line at 1.0). Nothing a player
// must read goes below kTextTiny (~11 px): smaller glyphs blur on the 3DS's 320x240 panel.
constexpr float kTextTitle = 0.52f;
constexpr float kTextBody = 0.44f;
constexpr float kTextSmall = 0.40f;
constexpr float kTextTiny = 0.36f;

// Whole-screen sky gradient (one quad).
void sky(float w = 320.0f, float h = 240.0f);
// The band y0..y1 of a sky(w, h) gradient, to cover content scrolled under a header.
void skyBand(float y0, float y1, float w = 320.0f, float h = 240.0f, float z = 0.5f);
// Filled rounded rectangle (r is clamped to half the smaller side).
void roundRect(float x, float y, float w, float h, float r, u32 color, float z = 0.5f);
// Paper card: edge-coloured rounded rect with a paper inset.
void paper(float x, float y, float w, float h, float r = 6.0f, u32 fill = kPaper, u32 edge = kPaperEdge,
           float z = 0.5f);
// Cartoon button: a darker lip under a rounded face. `down` sinks the face onto the lip.
void button(float x, float y, float w, float h, u32 face, u32 lip, bool down = false, float r = 6.0f,
            float z = 0.5f);
// Blend from a toward b (0..1), per channel including alpha.
u32 mix(u32 a, u32 b, float t);
// A darker shade of `c` (for lips and outlines), scale 0..1.
u32 shade(u32 c, float scale);

// Text helpers for one parsed C2D_Text: draw centred / right-aligned, shrinking to maxW.
void textCentered(const C2D_Text& t, float cx, float y, float scale, u32 color, float maxW = 0.0f,
                  float z = 0.5f);
void textFit(const C2D_Text& t, float x, float y, float scale, u32 color, float maxW, float z = 0.5f);

} // namespace tt::ui::theme
