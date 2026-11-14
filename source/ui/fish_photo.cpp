#include "ui/fish_photo.h"

#include "core/log.h"
#include "render/model.h"
#include "ui/fish_photo_atlas.h"
#include "ui/fish_trophy_atlas.h"

namespace tt::ui {

namespace {

render::Model gPhotos, gTrophies;
bool gTried = false, gTrophiesTried = false;
bool gUsed = false; // drawn since the last fishPhotosFrame()

C3D_Tex* atlas() {
    gUsed = true;
    if (!gTried) {
        gTried = true;
        if (!gPhotos.load("romfs:/baked/fish_photos.ttm")) TT_LOG("fish photo: no fish_photos.ttm");
    }
    return gPhotos.loaded() ? gPhotos.texture(0) : nullptr;
}

void drawCell(C3D_Tex* tex, float ax, float ay, float aw, float ah, float x, float y, float w, float h, float z,
              float atlasW = kFishPhotoAtlas, float atlasH = kFishPhotoAtlas) {
    const float kx = 1.0f / atlasW, ky = 1.0f / atlasH;
    Tex3DS_SubTexture sub{uint16_t(aw), uint16_t(ah), ax * kx, 1.0f - ay * ky, (ax + aw) * kx, 1.0f - (ay + ah) * ky};
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

bool drawFishTrophy(int level, float cx, float baseY, float height, float z) {
    if (level < 0 || level >= kFishTrophyLevels) return false;
    gUsed = true;
    if (!gTrophiesTried) {
        gTrophiesTried = true;
        if (!gTrophies.load("romfs:/baked/fish_trophies.ttm")) TT_LOG("fish photo: no fish_trophies.ttm");
    }
    C3D_Tex* tex = gTrophies.loaded() ? gTrophies.texture(0) : nullptr;
    if (!tex) return false;
    const float w = height * kFishTrophyCellW / kFishTrophyCellH;
    drawCell(tex, kFishTrophyCellX[level], 0.0f, kFishTrophyCellW, kFishTrophyCellH, cx - w * 0.5f,
             baseY - height * kFishTrophyBaseFrac, w, height, z, kFishTrophyAtlasW, kFishTrophyAtlasH);
    return true;
}

void fishPhotosFrame() {
    if (!gUsed) releaseFishPhotos();
    gUsed = false;
}

void releaseFishPhotos() {
    if (gPhotos.loaded()) gPhotos.unload();
    if (gTrophies.loaded()) gTrophies.unload();
    gTried = gTrophiesTried = false;
}

} // namespace tt::ui
