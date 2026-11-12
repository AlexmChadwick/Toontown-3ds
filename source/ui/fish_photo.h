// FishPhoto's pictures of the fish genera and the other catches (jellybean jar, old boot, quest
// package): romfs:/baked/fish_photos.ttm from tools/pipeline/bake_fish_photos.py, loaded on the
// first draw (512 KB of linear memory) and released when no screen shows them.
#pragma once

#include <citro2d.h>

namespace tt::ui {

// The genus picture over whatever the caller filled (FishPanel's swim colour), stretched over
// the rectangle like the square lens over the display region. False when it cannot be drawn.
bool drawFishPhoto(int genus, float x, float y, float w, float h, float z);
// An item picture (FishItemArt) centred on (cx, cy), at `pxPerUnit` pixels per itemFrame unit.
bool drawFishItemArt(int item, float cx, float cy, float pxPerUnit, float z);
void releaseFishPhotos();

} // namespace tt::ui
