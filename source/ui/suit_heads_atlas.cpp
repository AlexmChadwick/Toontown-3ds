#include "ui/suit_heads_atlas.h"
#include <citro2d.h>

namespace tt::ui {

SuitHeadAtlas& SuitHeadAtlas::instance() {
    static SuitHeadAtlas atlas;
    return atlas;
}

bool SuitHeadAtlas::init() {
    if (model_.loaded()) return true;
    if (model_.load("romfs:/baked/suit_heads.ttm")) return true;
    return model_.load("sdmc:/3ds/toontown3ds/data/suits/suit_heads.ttm");
}

void SuitHeadAtlas::shutdown() {
    model_.unload();
}

void SuitHeadAtlas::drawHead(int dept, int type, float x, float y, float size, float depth) {
    if (!model_.loaded() || dept < 0 || dept >= 4 || type < 0 || type >= 8) return;
    C3D_Tex* tex = model_.texture(0);
    if (!tex) return;

    const int idx = cogHeadIndex(dept, type);
    const SuitHeadSubTex& st = kSuitHeadSubTex[idx];
    Tex3DS_SubTexture sub;
    sub.width = st.width;
    sub.height = st.height;
    sub.left = st.left;
    sub.top = st.top;
    sub.right = st.right;
    sub.bottom = st.bottom;

    C2D_Image img;
    img.tex = tex;
    img.subtex = &sub;

    const float scale = size / float(st.width);
    C2D_DrawImageAt(img, x, y, depth, nullptr, scale, scale);
}

} // namespace tt::ui
