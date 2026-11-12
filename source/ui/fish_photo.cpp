#include "ui/fish_photo.h"

#include "core/log.h"
#include "render/model.h"
#include "ui/fish_photo_atlas.h"

namespace tt::ui {

namespace {

render::Model gPhotos;
bool gTried = false;
bool gUsed = false; // drawn since the last fishPhotosFrame()

C3D_Tex* atlas() {
    gUsed = true;
    if (!gTried) {
        gTried = true;
        if (!gPhotos.load("romfs:/baked/fish_photos.ttm")) TT_LOG("fish photo: no fish_photos.ttm");
    }
    return gPhotos.loaded() ? gPhotos.texture(0) : nullptr;
}

void drawCell(C3D_Tex* tex, float ax, float ay, float aw, float ah, float x, float y, float w, float h, float z) {
    constexpr float k = 1.0f / kFishPhotoAtlas;
    Tex3DS_SubTexture sub{uint16_t(aw), uint16_t(ah), ax * k, 1.0f - ay * k, (ax + aw) * k, 1.0f - (ay + ah) * k};
    C2D_Image img{tex, &sub};
    C2D_DrawImageAt(img, x, y, z, nullptr, w / aw, h / ah);
}

} // namespace

bool drawFishPhoto(int genus, float x, float y, float w, float h, float z) {
    C3D_Tex* tex = atlas();
    if (!tex) return false;
    for (const FishPhotoCell& c : kFishPhotoCells) {
        if (c.genus != genus) continue;
        drawCell(tex, c.x, c.y, kFishPhotoW, kFishPhotoH, x, y, w, h, z);
        return true;
    }
    return false;
}

bool drawFishItemArt(int item, float cx, float cy, float pxPerUnit, float z) {
    if (item < 0 || item >= kFishItemCount) return false;
    C3D_Tex* tex = atlas();
    if (!tex) return false;
    const FishItemRect& r = kFishItemRects[item];
    const float w = (r.maxX - r.minX) * pxPerUnit, h = (r.maxZ - r.minZ) * pxPerUnit;
    const float x = cx + r.minX * pxPerUnit, y = cy - r.maxZ * pxPerUnit;
    drawCell(tex, r.x, r.y, r.w, r.h, x, y, w, h, z);
    return true;
}

void fishPhotosFrame() {
    if (!gUsed) releaseFishPhotos();
    gUsed = false;
}

void releaseFishPhotos() {
    if (gPhotos.loaded()) gPhotos.unload();
    gTried = false;
}

} // namespace tt::ui
