// Host stub of world/battle_fx.h for the GUI panel tests. The full battle-fx header pulls
// in the citro3d world renderer; the bottom-screen sources compiled here (render/ui.cpp,
// battle_panel, gag_table, inventory_art, purchase/shop panels, page_quests) use it only
// for the GagAtlas icon accessors. The GagAtlas implementation lives in the host test
// TU (gui_panel_test.cpp): it draws nothing, but records like any image.
#pragma once

#include "citro2d.h"

#include <cstdint>

namespace tt::world {

class GagAtlas {
public:
    static GagAtlas& instance();
    bool init();
    void shutdown();
    bool loaded() const;
    void drawIcon(int track, int level, float x, float y, float size, float depth = 0.55f,
                  float alpha = 1.0f);
    // romfs:/baked/inventory_gui.ttm texture 1 (the panel pieces); nullptr here is fine,
    // the callers fall back to their atlas art as they do when the file is absent.
    C3D_Tex* guiTexture() { return nullptr; }

private:
    bool modelLoaded_ = false;
};

} // namespace tt::world
