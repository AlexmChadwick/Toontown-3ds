#include "ui/quest_poster_art.h"

#include <citro2d.h>

#include "core/log.h"
#include "render/model.h"
#include "ui/laff_meter.h"
#include "ui/quest_npc_heads.h"

namespace tt::ui {

namespace {

render::Model gArt;
bool gTried = false;

const QuestNpcHead* findHead(uint32_t npc) {
    int lo = 0, hi = kNumQuestNpcHeads - 1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        if (kQuestNpcHeads[mid].npc == npc) return &kQuestNpcHeads[mid];
        if (kQuestNpcHeads[mid].npc < npc) lo = mid + 1;
        else hi = mid - 1;
    }
    return nullptr;
}

} // namespace

bool drawQuestArt(QuestArt art, float x, float y, float w, float h, u32 tint, float z) {
    if (!gTried) {
        gTried = true;
        if (!gArt.load("romfs:/baked/quest_poster.ttm")) TT_LOG("ui: no quest_poster.ttm");
    }
    C3D_Tex* tex = gArt.loaded() ? gArt.texture(0) : nullptr;
    if (!tex || int(art) < 0 || int(art) >= int(QuestArt::Count)) return false;
    const QuestArtRect& r = kQuestArtRects[int(art)];
    const float u0 = r.x / kQuestPosterAtlasW, u1 = (r.x + r.w) / kQuestPosterAtlasW;
    const float v0 = 1.0f - r.y / kQuestPosterAtlasH, v1 = 1.0f - (r.y + r.h) / kQuestPosterAtlasH; // top row = t 1
    Tex3DS_SubTexture sub{uint16_t(r.w + 0.5f), uint16_t(r.h + 0.5f), u0, v0, u1, v1};
    C2D_Image img{tex, &sub};
    C2D_ImageTint t;
    C2D_PlainImageTint(&t, tint, 1.0f);
    C2D_SetTintMode(C2D_TintMult);
    C2D_DrawImageAt(img, x, y, z, &t, w / r.w, h / r.h);
    return true;
}

bool drawQuestNpcHead(uint32_t npc, float x, float y, float size, float z) {
    const QuestNpcHead* head = findHead(npc);
    if (!head) return false;
    LaffFace face;
    face.hp = face.maxHp = 15; // the smile with open eyes, no numbers
    face.rgb[0] = head->r / 255.0f;
    face.rgb[1] = head->g / 255.0f;
    face.rgb[2] = head->b / 255.0f;
    face.species = head->species;
    return drawLaffMeter(face, x, y, size, nullptr, nullptr, z);
}

} // namespace tt::ui
