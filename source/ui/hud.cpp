#include <cstring>
#include "render/ui_font.h"
#include "ui/hud.h"

#include <cstdio>

#include "core/log.h"

#include "ui/fish_photo.h"
#include "ui/fish_photo_atlas.h"
#include "ui/fishing_panel.h"
#include "ui/laff_meter.h"
#include "ui/race_gui_atlas.h"

namespace tt::ui {

namespace {

// The meter is larger than upstream's share of the screen: at 64 px the numbers in the eyes were
// unreadable on the 400x240 top screen. The bottom-left margin stays 6 px.
constexpr float kLaffSize = 88.0f; // on-screen size of one atlas cell (the whole meter)
constexpr float kLaffX = 6.0f + kLaffSize * 0.5f, kLaffY = 234.0f - kLaffSize * 0.5f;
// TownBattle's timer: ToontownTimer at (1.182, 0.842) and scale 0.4 in aspect2d, i.e. a 48 px clock
// about 19 px in from the top-right corner (1 aspect2d unit = 120 px of the 240 px screen).
constexpr float kBattleTimerSize = 48.0f, kBattleTimerX = 400.0f - 22.0f, kBattleTimerY = 22.0f;

} // namespace

bool Hud::init() {
    raceBuf_ = C2D_TextBufNew(256);
    toastBuf_ = C2D_TextBufNew(160);
    fishBuf_ = C2D_TextBufNew(256);
    return laff_.init() && battleTimer_.init() && raceBuf_ != nullptr && toastBuf_ != nullptr && fishBuf_ != nullptr;
}

void Hud::shutdown() {
    laff_.shutdown();
    battleTimer_.shutdown();
    if (raceBuf_) C2D_TextBufDelete(raceBuf_);
    if (toastBuf_) C2D_TextBufDelete(toastBuf_);
    if (fishBuf_) C2D_TextBufDelete(fishBuf_);
    raceBuf_ = toastBuf_ = fishBuf_ = nullptr;
    releaseFishPhotos();
}

namespace {

// Photo Fun viewfinder (DistributedPhotoGame's photo_game_viewfinder frame, drawn as flat
// bars): corner brackets around the sight, which stays at the centre of the top screen because
// the tripod camera always looks where the swivel points.
constexpr float kVfLeft = 120.0f, kVfRight = 280.0f, kVfTop = 70.0f, kVfBottom = 170.0f;
constexpr float kVfArm = 22.0f, kVfThick = 3.0f;
constexpr float kVfSightArm = 9.0f;
constexpr float kVfZ = 0.45f;

void drawViewfinder(C3D_RenderTarget* top) {
    C2D_SceneBegin(top);
    C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
    const u32 c = C2D_Color32(0xFF, 0xFF, 0xFF, 0xE0);
    const float cx = (kVfLeft + kVfRight) * 0.5f;
    const float cy = (kVfTop + kVfBottom) * 0.5f;
    // Top-left, top-right, bottom-left, bottom-right corners.
    C2D_DrawRectSolid(kVfLeft, kVfTop, kVfZ, kVfArm, kVfThick, c);
    C2D_DrawRectSolid(kVfLeft, kVfTop, kVfZ, kVfThick, kVfArm, c);
    C2D_DrawRectSolid(kVfRight - kVfArm, kVfTop, kVfZ, kVfArm, kVfThick, c);
    C2D_DrawRectSolid(kVfRight - kVfThick, kVfTop, kVfZ, kVfThick, kVfArm, c);
    C2D_DrawRectSolid(kVfLeft, kVfBottom - kVfThick, kVfZ, kVfArm, kVfThick, c);
    C2D_DrawRectSolid(kVfLeft, kVfBottom - kVfArm, kVfZ, kVfThick, kVfArm, c);
    C2D_DrawRectSolid(kVfRight - kVfArm, kVfBottom - kVfThick, kVfZ, kVfArm, kVfThick, c);
    C2D_DrawRectSolid(kVfRight - kVfThick, kVfBottom - kVfArm, kVfZ, kVfThick, kVfArm, c);
    // Sight.
    C2D_DrawRectSolid(cx - kVfSightArm, cy - 0.5f, kVfZ, 2 * kVfSightArm, 1.0f, c);
    C2D_DrawRectSolid(cx - 0.5f, cy - kVfSightArm, kVfZ, 1.0f, 2 * kVfSightArm, c);
    C3D_DepthTest(true, GPU_GEQUAL, GPU_WRITE_ALL);
}

} // namespace

namespace {

render::Model gRaceArt;
bool gRaceArtTried = false;

// aspect2d -> top screen pixels (120 px per unit, origin at the centre).
float px(float x) { return 200.0f + x * 120.0f; }
float py(float z) { return 120.0f - z * 120.0f; }

// One race_gui.ttm part centred on (cx, cy), w x h pixels, turned clockwise by deg, tinted rgb.
void raceArt(RaceArt art, float cx, float cy, float w, float h, const float* rgb = nullptr,
             float deg = 0.0f) {
    C3D_Tex* tex = gRaceArt.loaded() ? gRaceArt.texture(0) : nullptr;
    if (!tex) return;
    const RaceArtRect& r = kRaceArtRects[int(art)];
    Tex3DS_SubTexture sub{uint16_t(r.w + 0.5f), uint16_t(r.h + 0.5f), r.x / kRaceGuiAtlasW,
                          1.0f - r.y / kRaceGuiAtlasH, (r.x + r.w) / kRaceGuiAtlasW,
                          1.0f - (r.y + r.h) / kRaceGuiAtlasH};
    C2D_Image img{tex, &sub};
    C2D_ImageTint tint;
    if (rgb) {
        C2D_PlainImageTint(&tint, C2D_Color32f(rgb[0], rgb[1], rgb[2], 1.0f), 1.0f);
        C2D_SetTintMode(C2D_TintMult);
    }
    C2D_DrawImageAtRotated(img, cx, cy, 0.5f, deg * 0.01745329252f, rgb ? &tint : nullptr, w / r.w, h / r.h);
    if (rgb) C2D_SetTintMode(C2D_TintSolid);
}

} // namespace

// RaceGUI.initRaceMode / update and DistributedVehicle's speedometer, at their aspect2d places.
// Sizes are upstream's except where 3DS pixels make them unreadable: the head frames are drawn
// at twice their share (18 px) and the lap times at 0.075 instead of 0.06.
void Hud::drawRace(const RaceHud& r) {
    if (!gRaceArtTried) {
        gRaceArtTried = true;
        if (!gRaceArt.load("romfs:/baked/race_gui.ttm")) TT_LOG("hud: no race_gui.ttm");
    }
    C2D_TextBufClear(raceBuf_);
    // SignFont at TextNode scale s: the font's em is ~30 px at C2D scale 1, so C2D scale = s * 4.
    auto sign = [&](const char* str, float x, float z, float s, u32 color, u32 flags) {
        C2D_Text t;
        tt::render::textParseSign(&t, raceBuf_, str);
        C2D_DrawText(&t, C2D_WithColor | C2D_AtBaseline | flags, px(x), py(z), 0.5f, s * 4, s * 4, color);
    };
    const u32 yellow = C2D_Color32f(0.95f, 0.95f, 0.0f, 1.0f);
    char text[32];

    // The map: the curve in white, mapScene at (1.1, 0.75) scale 0.25, mapLines 0.00025 x the
    // track's map scale, setP(90) so world y is screen up.
    const float k = 0.25f * 0.00025f * r.mapScale * 120.0f;
    for (int i = 1; i < r.curveCount; i++) {
        C2D_DrawLine(px(1.1f) + r.curve[i - 1][0] * k, py(0.75f) - r.curve[i - 1][1] * k, C2D_Color32(255, 255, 255, 255),
                     px(1.1f) + r.curve[i][0] * k, py(0.75f) - r.curve[i][1] * k, C2D_Color32(255, 255, 255, 255),
                     1.5f, 0.5f);
    }
    for (int pass = 0; pass < 2; pass++) { // the local spot last (on top) and 1.25 times larger
        for (int i = 0; i < r.racerCount; i++) {
            const RaceHud::Racer& q = r.racers[i];
            if (q.local != (pass == 1)) continue;
            const float size = 0.15f * 0.25f * 120.0f * (q.local ? 1.25f : 1.0f);
            raceArt(RaceArt::MapSpot, px(1.1f) + q.x * k, py(0.75f) - q.y * k, size, size, q.body);
        }
    }

    // Progress line (-0.8 .. 0.8 at z 0.93) with a hash per lap, the racers' head frames on it and
    // each lap's time under its stretch.
    const float x0 = -0.8f, x1 = 0.8f, lineZ = 0.93f;
    C2D_DrawRectSolid(px(x0), py(lineZ) - 0.6f, 0.5f, (x1 - x0) * 120.0f, 1.2f, C2D_Color32(255, 255, 255, 255));
    for (int n = 0; n <= r.laps; n++) {
        const float t = float(n) / r.laps;
        C2D_DrawRectSolid(px(x0 + (x1 - x0) * t) - 0.6f, py(lineZ) - 3.0f, 0.5f, 1.2f, 6.0f,
                          C2D_Color32(255, 255, 255, 255));
    }
    for (int lap = 0; lap < r.lapsShown && lap < RaceHud::kMaxLaps; lap++) {
        const float fx = x0 + (x1 - x0) * (2.0f * lap + 1) / (r.laps * 2.0f);
        float t = r.lapSeconds[lap];
        const int minutes = int(t / 60);
        t -= minutes * 60;
        const int seconds = int(t);
        const int fraction = int((t - seconds) * 100);
        snprintf(text, sizeof(text), "%d'", minutes);
        sign(text, fx - 0.06f, 0.84f, 0.075f, yellow, C2D_AlignRight);
        snprintf(text, sizeof(text), "%02d''", seconds);
        sign(text, fx + 0.06f, 0.84f, 0.075f, yellow, C2D_AlignRight);
        snprintf(text, sizeof(text), "%02d", fraction);
        sign(text, fx + 0.14f, 0.84f, 0.075f, yellow, C2D_AlignRight);
    }
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < r.racerCount; i++) {
            const RaceHud::Racer& q = r.racers[i];
            if (q.local != (pass == 1)) continue;
            const float w = 18.0f * (q.local ? 1.25f : 1.0f);
            const float cx = px(x0 + (x1 - x0) * q.progress), cy = py(lineZ);
            raceArt(RaceArt::PanelBg, cx, cy, w, w);
            raceArt(RaceArt::PanelFg, cx, cy, w, w, q.body);
            LaffFace face;
            face.maxHp = face.hp = 1;
            for (int c = 0; c < 3; c++) face.rgb[c] = q.head[c];
            face.species = q.species;
            const float head = w * 0.8f;
            drawLaffMeter(face, cx - head * 0.5f, cy - head * 0.5f + w * 0.09f, head, nullptr, nullptr);
        }
    }

    // Place (bottom left), lap (right), wrong way (flashing, top right).
    static const char* const kSuffix[5] = {"", "st", "    nd", "  rd", "   th"};
    if (r.photoFinish) {
        sign("Photo Finish!", 0.0f, -0.1f, 0.25f, yellow, C2D_AlignCenter);
    } else {
        snprintf(text, sizeof(text), "%d", r.place);
        sign(text, -1.2f, -0.97f, 0.35f, yellow, C2D_AlignCenter);
        sign(kSuffix[r.place >= 1 && r.place <= 4 ? r.place : 1], -1.05f, -0.8f, 0.1f, yellow, C2D_AlignCenter);
    }
    if (r.lapNotice) {
        // lapNotice: a default DirectLabel (interface font, black) at scale 0.1 in the centre.
        if (r.lapNotice < 0)
            snprintf(text, sizeof(text), "Final Lap!");
        else
            snprintf(text, sizeof(text), "Lap %d", r.lapNotice);
        C2D_Text t;
        tt::render::textParse(&t, raceBuf_, text);
        C2D_DrawText(&t, C2D_WithColor | C2D_AtBaseline | C2D_AlignCenter, px(0), py(0), 0.5f, 0.4f, 0.4f,
                     C2D_Color32(0, 0, 0, 255));
    }
    snprintf(text, sizeof(text), "%d/%d", r.lap, r.laps);
    sign(text, 1.1f, 0.45f, 0.1f, yellow, C2D_AlignCenter);
    if (r.wrongWay) {
        // wrongWaySeq: alpha 0 -> 1 over 0.25 s, back to 0 over 0.25 s, looped.
        const float phase = float(osGetTime() % 500) / 250.0f;
        const float a = phase < 1.0f ? phase : 2.0f - phase;
        sign("Wrong\nWay!", 1.1f, 0.85f, 0.1f, C2D_Color32f(0.95f, 0.0f, 0.0f, a), C2D_AlignCenter);
    }

    // Gag panel at (-1.13, -0.5), image scale 0.25; the gag at scale 0.2 on it.
    raceArt(RaceArt::GagPanel, px(-1.13f), py(-0.5f), 30.0f, 30.0f);
    if (r.gag > 0 && r.gag <= 4) raceArt(RaceArt(int(RaceArt::GagCouch) + r.gag), px(-1.13f), py(-0.5f), 24.0f, 24.0f);

    // Speedometer at (1.24, -0.98), scale 0.75: gauge, spin (setR), face (1 + accessory) / 2,
    // tics and ring (body + 1) / 2, then the speed in the body colour.
    const float sx = px(1.24f), sy = py(-0.98f), ss = 0.75f * 120.0f;
    float face[3], trim[3];
    for (int c = 0; c < 3; c++) {
        face[c] = (1.0f + r.accessory[c]) * 0.5f;
        trim[c] = (r.body[c] + 1.0f) * 0.5f;
    }
    raceArt(RaceArt::SpeedGauge, sx, sy, ss, ss);
    raceArt(RaceArt::SpeedSpin, sx, sy, ss, ss, nullptr, r.needle);
    raceArt(RaceArt::SpeedFace, sx, sy, ss, ss, face);
    raceArt(RaceArt::SpeedTics, sx, sy, ss, ss, trim);
    raceArt(RaceArt::SpeedRing, sx, sy, ss, ss, trim);
    snprintf(text, sizeof(text), "%d", r.speed);
    sign(text, 1.24f - 0.04f, -0.98f + 0.02f, 0.18f, C2D_Color32f(r.body[0], r.body[1], r.body[2], 1.0f),
         C2D_AlignCenter);
}

// DistributedFishingSpot's catch, over the world as upstream's aspect2d: FishPanel (species name,
// FishPhoto on the swim colour, weight, value, "New Species!" / "New Record!") for a fish, itemGui
// ("You caught:", the jar / boot picture, the item name) for jellybeans and the old boot. Both at
// 1.6 times the aspect2d scale (192 px per unit instead of 120) so their 0.05-0.08 text stays
// readable on the 240 px screen; the panel keeps its place near the top, clear of the Toon.
void Hud::drawFishCatch(const FishingView& f) {
    const bool fish = f.result == FishingView::kFish && f.genus >= 0;
    const bool item = f.result == FishingView::kBeans || f.result == FishingView::kBoot;
    if (!fish && !item) return;
    enum { kName, kWeight, kValue, kExtra, kFound };
    if (f.revision != fishRevision_) {
        fishRevision_ = f.revision;
        C2D_TextBufClear(fishBuf_);
        const char* weight = f.itemWeight;
        tt::render::textParse(&fishText_[kName], fishBuf_, f.itemName);
        tt::render::textParse(&fishText_[kWeight], fishBuf_, weight);
        tt::render::textParse(&fishText_[kValue], fishBuf_, fish ? f.itemValue : "");
        tt::render::textParseSign(&fishText_[kExtra], fishBuf_, f.newRecord ? "New Record!" : "New Species!");
        tt::render::textParse(&fishText_[kFound], fishBuf_, "You caught:");
        for (C2D_Text& t : fishText_) C2D_TextOptimize(&t);
    }
    constexpr float k = 192.0f; // pixels per aspect2d unit
    // C2D scale of an aspect2d text scale s: the fonts' em is about 30 px at C2D scale 1.
    auto textAt = [&](int slot, float cx, float y, float s, u32 color) {
        float w = 0, h = 0;
        const float c = s * k / 30.0f;
        C2D_TextGetDimensions(&fishText_[slot], c, c, &w, &h);
        C2D_DrawText(&fishText_[slot], C2D_WithColor | C2D_AtBaseline, cx - w * 0.5f, y, 0.6f, c, c, color);
    };
    const u32 ink = C2D_Color32(0, 0, 0, 0xFF);
    const u32 dialog = C2D_Color32(0xFF, 0xFF, 0xBF, 0xFF); // GlobalDialogColor (1, 1, 0.75)
    const u32 edge = C2D_Color32(0x50, 0x50, 0x50, 0xFF);
    const float cx = 200.0f;
    if (fish) {
        // getDefaultDialogGeom at image_scale (0.65, 0.85); the frame's top 8 px under the screen top.
        const float w = 0.65f * k, h = 0.85f * k, top = 6.0f, cy = top + h * 0.5f;
        C2D_DrawRectSolid(cx - w * 0.5f - 2, top - 2, 0.5f, w + 4, h + 4, edge);
        C2D_DrawRectSolid(cx - w * 0.5f, top, 0.5f, w, h, dialog);
        // setSwimBounds(-0.3, 0.3, -0.235, 0.25), setSwimColor(1, 1, 0.749)
        const float rx = cx - 0.3f * k, ry = cy - 0.25f * k, rw = 0.6f * k, rh = 0.485f * k;
        C2D_DrawRectSolid(rx, ry, 0.52f, rw, rh, C2D_Color32f(1.0f, 1.0f, 0.749f, 1.0f));
        drawFishPhoto(f.genus, rx, ry, rw, rh, 0.55f);
        textAt(kName, cx, cy - 0.35f * k, 0.06f, ink);       // text_pos (0, 0.35)
        if (f.newEntry) textAt(kExtra, cx, cy - 0.26f * k, 0.08f, C2D_Color32f(0.2f, 0.8f, 0.4f, 1.0f));
        textAt(kWeight, cx, cy + 0.28f * k, 0.05f, ink);
        textAt(kValue, cx, cy + 0.35f * k, 0.05f, ink);
    } else {
        // itemFrame: geom_scale (1, 1, 0.6) at z 0.587, "You caught:" at 0.2, the label at -0.25.
        const float w = 1.0f * k, h = 0.6f * k, top = 6.0f, cy = top + h * 0.5f;
        C2D_DrawRectSolid(cx - w * 0.5f - 2, top - 2, 0.5f, w + 4, h + 4, edge);
        C2D_DrawRectSolid(cx - w * 0.5f, top, 0.5f, w, h, dialog);
        textAt(kFound, cx, cy - 0.2f * k, 0.08f, ink);
        drawFishItemArt(f.result == FishingView::kBeans ? kFishItemJar : kFishItemBoot, cx, cy, k, 0.55f);
        textAt(kName, cx, cy + 0.25f * k, 0.06f, ink);
    }
}

void Hud::draw(C3D_RenderTarget* top, const HudValues& v) {
    // The photo atlas stays resident only while a catch or the book's album shows it.
    fishPhotosFrame();
    const bool fishShown = v.fishing && (v.fishing->result == FishingView::kFish ||
                                         v.fishing->result == FishingView::kBeans ||
                                         v.fishing->result == FishingView::kBoot);
    if (fishShown) {
        C2D_SceneBegin(top);
        C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
        drawFishCatch(*v.fishing);
        C3D_DepthTest(true, GPU_GEQUAL, GPU_WRITE_ALL);
    }
    if (v.toast) {
        // By content: callers reuse one buffer for different messages (whispers, friend invites).
        if (v.toast != lastToast_ || std::strncmp(v.toast, lastToastText_, sizeof(lastToastText_) - 1)) {
            C2D_TextBufClear(toastBuf_);
            tt::render::textParse(&toastText_, toastBuf_, v.toast);
            C2D_TextOptimize(&toastText_);
            lastToast_ = v.toast;
            std::snprintf(lastToastText_, sizeof(lastToastText_), "%s", v.toast);
        }
        C2D_SceneBegin(top);
        float w, h;
        C2D_TextGetDimensions(&toastText_, 0.6f, 0.6f, &w, &h);
        C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
        C2D_DrawRectSolid(200 - w * 0.5f - 10, 70, 0.5f, w + 20, h + 12, C2D_Color32(0x10, 0x18, 0x30, 0xD0));
        C2D_DrawText(&toastText_, C2D_WithColor, 200 - w * 0.5f, 76, 0.5f, 0.6f, 0.6f,
                     C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF));
        C3D_DepthTest(true, GPU_GEQUAL, GPU_WRITE_ALL);
    }
    if (v.photoViewfinder) drawViewfinder(top);
    if (!v.online) return; // stats come from the server
    C2D_SceneBegin(top);
    C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR); // overlay: never hidden by the world
    if (v.racing && v.race.active) drawRace(v.race);

    // Upstream's HUD is the laff meter alone; jellybeans show on the book's Gags page and the
    // Toon tab (InventoryPage.moneyDisplay), not over the world. While the bottom screen shows
    // our laff (its tab bar pocket, the battle panel's Toon cards) the world stays clear.
    if (!v.laffOnBottom && !v.laffHidden) {
        LaffFace face;
        face.hp = v.hp;
        face.maxHp = v.maxHp;
        for (int i = 0; i < 3; i++) face.rgb[i] = v.headRgb[i];
        face.species = v.species;
        // quest_assign_101 flies the meter from (-1.18, -0.87) scale 0.075 to (-0.25, -0.15) scale 0.2
        // (aspect2d: 120 px per unit on the 400 x 240 top screen)
        const float k = v.laffGrow;
        const float bigSize = kLaffSize * (0.2f / 0.075f) * 0.75f; // keeps the whole meter on the 240 px screen
        const float cx = kLaffX + (200.0f - 0.25f * 120.0f - kLaffX) * k;
        const float cy = kLaffY + (120.0f + 0.15f * 120.0f - kLaffY) * k;
        laff_.draw(face, cx, cy, kLaffSize + (bigSize - kLaffSize) * k);
    }
    if (v.timerSeconds >= 0) battleTimer_.draw(v.timerSeconds, kBattleTimerX, kBattleTimerY, kBattleTimerSize);
    C3D_DepthTest(true, GPU_GEQUAL, GPU_WRITE_ALL); // citro2d's default
}

} // namespace tt::ui
