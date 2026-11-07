// First frames on screen: a Toontown-style loading screen drawn right after the GPU and UI font
// are up, before any of the riskier boot steps (audio, shaders, zone data). Each boot step names
// itself with stage(), which logs (flushed to the SD card at once) and redraws, so a failure on
// real hardware shows which step it was, on screen and in sdmc:/3ds/toontown3ds/log.txt.
#pragma once

namespace tt::boot {

// Needs C3D_Init + C2D_Init/C2D_Prepare done. Creates its own top-screen target.
void begin();
// Logs "boot: <what>" and draws one loading frame with the progress bar at progress (0..1).
void stage(const char* what, float progress);
// Frees the loading screen's own target once the game renderer takes over.
void end();
bool active();

// A failure the game cannot run past (missing data, not enough memory): shows the message on
// both screens until START (or HOME close), then returns so main() can exit cleanly.
void showFatal(const char* title, const char* detail);

} // namespace tt::boot
