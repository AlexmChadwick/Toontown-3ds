#include "render/ui.h"

#include "ui/gag_data.h"
#include "world/battle_fx.h"
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "core/log.h"
#include "core/memory.h"
#include "core/profile.h"

namespace tt::ui {

const float kToonColors[kNumToonColors][3] = {
    {1.0f, 1.0f, 1.0f},         {0.96875f, 0.691406f, 0.699219f}, {0.933594f, 0.265625f, 0.28125f},
    {0.863281f, 0.40625f, 0.417969f}, {0.710938f, 0.234375f, 0.4375f}, {0.570312f, 0.449219f, 0.164062f},
    {0.640625f, 0.355469f, 0.269531f}, {0.996094f, 0.695312f, 0.511719f}, {0.832031f, 0.5f, 0.296875f},
    {0.992188f, 0.480469f, 0.167969f}, {0.996094f, 0.898438f, 0.320312f}, {0.996094f, 0.957031f, 0.597656f},
    {0.855469f, 0.933594f, 0.492188f}, {0.550781f, 0.824219f, 0.324219f}, {0.242188f, 0.742188f, 0.515625f},
    {0.304688f, 0.96875f, 0.402344f},  {0.433594f, 0.90625f, 0.835938f}, {0.347656f, 0.820312f, 0.953125f},
    {0.191406f, 0.5625f, 0.773438f},   {0.558594f, 0.589844f, 0.875f},   {0.285156f, 0.328125f, 0.726562f},
    {0.460938f, 0.378906f, 0.824219f}, {0.546875f, 0.28125f, 0.75f},     {0.726562f, 0.472656f, 0.859375f},
    {0.898438f, 0.617188f, 0.90625f},  {0.7f, 0.7f, 0.8f},               {0.3f, 0.3f, 0.35f},
};

const unsigned kTeleportHoods[kNumTeleportHoods] = {2000, 1000, 5000, 4000, 3000, 9000, 6000, 8000};

namespace {

constexpr float kW = 320.0f;
// Map tab teleport row (just above the tab bar).
constexpr float kHoodRowY = 178.0f, kHoodRowH = 26.0f, kHoodW = 320.0f / kNumTeleportHoods;
constexpr float kHomeY = 148.0f, kHomeH = 24.0f;
const char* const kHoodShort[kNumTeleportHoods] = {"TTC", "DD", "DG", "MM", "BR", "DL", "AA", "GS"};
constexpr float kTabH = 32.0f;
constexpr float kTabY = 240.0f - kTabH;
constexpr float kTabW = kW / kTabCount;
const char* const kTabNames[kTabCount] = {"Map", "Chat", "Toon", "Gags", "Setup", "Debug"};

const u32 kBg = C2D_Color32(0x1E, 0x2A, 0x44, 0xFF);
const u32 kPanel = C2D_Color32(0x2C, 0x3C, 0x60, 0xFF);
const u32 kAccent = C2D_Color32(0xF5, 0xB8, 0x1A, 0xFF);
const u32 kWhite = C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF);
const u32 kDim = C2D_Color32(0xA8, 0xB4, 0xCC, 0xFF);

// Toon tab swatch grid
constexpr float kSwX = 8, kSwY = 44, kSwSize = 30, kSwGap = 4;
constexpr int kSwCols = 9;
// Settings buttons
constexpr float kBtnX = 12, kBtnW = 140, kBtnH = 36;
constexpr float kStereoY = 30, kStressY = 90;

bool inRect(const touchPosition& t, float x, float y, float w, float h) {
    return t.px >= x && t.px < x + w && t.py >= y && t.py < y + h;
}

u32 colorOf(int idx) {
    const float* c = kToonColors[idx];
    return C2D_Color32f(c[0], c[1], c[2], 1.0f);
}

} // namespace

bool Ui::init() {
    textBuf_ = C2D_TextBufNew(1024);
    staticBuf_ = C2D_TextBufNew(64);
    debugBuf_ = C2D_TextBufNew(1024);
    if (!textBuf_ || !staticBuf_ || !debugBuf_ || !chat_.init() || !shop_.init() || !tailor_.init() ||
        !closet_.init() ||
        !battle_.init() || !quest_.init() || !fishingPanel_.init()) return false;
    for (int i = 0; i < kTabCount; i++) {
        C2D_TextParse(&tabText_[i], staticBuf_, kTabNames[i]);
        C2D_TextOptimize(&tabText_[i]);
    }
    return true;
}

void Ui::shutdown() {
    unloadMap();
    chat_.shutdown();
    shop_.shutdown();
    tailor_.shutdown();
    closet_.shutdown();
    battle_.shutdown();
    quest_.shutdown();
    fishingPanel_.shutdown();
    C2D_TextBuf* bufs[] = {&textBuf_, &staticBuf_, &debugBuf_};
    for (C2D_TextBuf* b : bufs) {
        if (*b) C2D_TextBufDelete(*b);
        *b = nullptr;
    }
}

void Ui::debugLine(float x, float y, float scale, u32 color, const char* fmt, ...) {
    if (numDebugLines_ >= kMaxDebugLines) return;
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    CachedLine& l = debugLines_[numDebugLines_++];
    C2D_TextParse(&l.text, debugBuf_, buf);
    C2D_TextOptimize(&l.text);
    l.x = x;
    l.y = y;
    l.scale = scale;
    l.color = color;
}

void Ui::text(float x, float y, float scale, u32 color, const char* fmt, ...) {
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    C2D_Text t;
    C2D_TextParse(&t, textBuf_, buf);
    C2D_TextOptimize(&t);
    C2D_DrawText(&t, C2D_WithColor, x, y, 0.5f, scale, scale, color);
}

u32 Ui::handleInput(u32 down, const touchPosition& t, State& s) {
    return handleInput(down, hidKeysHeld() | (down & KEY_TOUCH), t, s);
}

u32 Ui::handleInput(u32 down, u32 held, const touchPosition& t, State& s) {
    if (shop_.active()) return shop_.handleInput(down, t); // the shop swallows every button
    if (tailor_.active()) return tailor_.handleInput(down, t);
    if (closet_.active()) return closet_.handleInput(down, t);
    if (battleView_) return battle_.handleInput(down, t, *battleView_, battleGags_);
    if (talk_ && talk_->locked()) return quest_.handleInput(down, t, *talk_);
    if (down & KEY_DLEFT) s.tab = (s.tab + kTabCount - 1) % kTabCount;
    if (down & KEY_DRIGHT) s.tab = (s.tab + 1) % kTabCount;
    if (down & KEY_X) s.tab = kTabChat;
    if (down & KEY_Y) s.tab = kTabMap;
    if ((down & KEY_TOUCH) && t.py >= kTabY) {
        s.tab = static_cast<int>(t.px / kTabW);
        if (s.tab >= kTabCount) s.tab = kTabCount - 1;
    }
    if (s.tab != lastTab_) {
        chat_.blur(); // the highlight (and A/B capture) never survives a tab change
        lastTab_ = s.tab;
        return 0;
    }
    if (s.tab == kTabChat) {
        // Header button: cycle the whisper target through the toons in view.
        if ((down & KEY_TOUCH) && numNearby_ > 0 && inRect(t, kW - 100, 2, 96, 24)) {
            int next = 0;
            for (int i = 0; i < numNearby_; i++)
                if (nearbyIds_[i] == s.whisperTo) next = i + 1;
            s.whisperTo = next < numNearby_ ? nearbyIds_[next] : 0;
            return down;
        }
        bool stillHere = s.whisperTo == 0;
        for (int i = 0; i < numNearby_; i++) stillHere |= nearbyIds_[i] == s.whisperTo;
        if (!stillHere) s.whisperTo = 0;
        return chat_.handleInput(down, held, t, kTabY);
    }
    if (!(down & KEY_TOUCH) || t.py >= kTabY) return 0;

    switch (s.tab) {
    case kTabMap:
        if (inRect(t, 4, kHomeY, 76, kHomeH)) {
            s.homeRequest = true;
            break;
        }
        if (t.py >= kHoodRowY && t.py < kHoodRowY + kHoodRowH) {
            const int i = static_cast<int>(t.px / kHoodW);
            if (i >= 0 && i < kNumTeleportHoods) s.teleportRequest = kTeleportHoods[i];
        }
        break;
    case kTabToon:
        for (int i = 0; i < 3; i++) {
            if (inRect(t, 8 + i * 102, 8, 98, 28)) s.colorTarget = i;
        }
        for (int i = 0; i < kNumToonColors; i++) {
            const float x = kSwX + (i % kSwCols) * (kSwSize + kSwGap);
            const float y = kSwY + (i / kSwCols) * (kSwSize + kSwGap);
            if (inRect(t, x, y, kSwSize, kSwSize)) {
                (s.colorTarget == 0 ? s.bodyColor : s.colorTarget == 1 ? s.shirtColor : s.bottomColor) = i;
            }
        }
        break;
    case kTabSettings:
        if (inRect(t, kBtnX, kStereoY, kBtnW, kBtnH)) s.stereo = !s.stereo;
        if (inRect(t, kBtnX, kStressY, 60, kBtnH) && s.stressToons > 0) s.stressToons -= s.stressToons > 8 ? 8 : 1;
        if (inRect(t, kBtnX + 80, kStressY, 60, kBtnH) && s.stressToons < 64) s.stressToons += s.stressToons >= 8 ? 8 : 1;
        break;
    default:
        break;
    }
    return 0;
}

void Ui::draw(C3D_RenderTarget* target, const State& s, const Context& c) {
    frame_++;
    C2D_TextBufClear(textBuf_);
    C2D_TargetClear(target, kBg);
    C2D_SceneBegin(target);
    if (shop_.active()) {
        shop_.draw();
        return;
    }
    if (tailor_.active()) {
        tailor_.draw();
        return;
    }
    if (closet_.active()) {
        closet_.draw();
        return;
    }
    if (battleView_) {
        battle_.draw(*battleView_, battleGags_);
        return;
    }
    if (MinigamePurchasePanel::activeInstance() && MinigamePurchasePanel::activeInstance()->active()) {
        MinigamePurchasePanel::activeInstance()->draw();
        return;
    }
    if (c.infoTitle) {
        drawInfo(c);
        return;
    }
    if (talk_ && talk_->locked()) {
        quest_.drawTalk(*talk_);
        return;
    }
    if (c.fishing) {
        fishingPanel_.draw(*c.fishing, kTabY);
        drawTabs(s);
        return;
    }
    switch (s.tab) {
    case kTabMap: drawMap(c); break;
    case kTabChat: {
        chat_.draw(kTabY);
        numNearby_ = c.numNearby < 8 ? c.numNearby : 8;
        const char* target = "Everyone";
        for (int i = 0; i < numNearby_; i++) {
            nearbyIds_[i] = c.nearbyIds[i];
            snprintf(nearbyNames_[i], sizeof(nearbyNames_[i]), "%s", c.nearbyNames[i] ? c.nearbyNames[i] : "");
            if (nearbyIds_[i] == s.whisperTo) target = nearbyNames_[i];
        }
        if (numNearby_ > 0) {
            C2D_DrawRectSolid(kW - 100, 2, 0.5f, 96, 24, s.whisperTo ? kAccent : kPanel);
            text(kW - 96, 7, 0.4f, s.whisperTo ? kBg : kWhite, "To: %.12s", target);
        }
        break;
    }
    case kTabToon: drawToon(s, c); break;
    case kTabGags: drawGags(c); break;
    case kTabSettings: drawSettings(s); break;
    default: drawDebug(c); break;
    }
    drawTabs(s);
}

void Ui::drawTabs(const State& s) {
    C2D_DrawRectSolid(0, kTabY, 0.5f, kW, kTabH, kPanel);
    for (int i = 0; i < kTabCount; i++) {
        if (i == s.tab) C2D_DrawRectSolid(i * kTabW + 2, kTabY + 2, 0.5f, kTabW - 4, kTabH - 4, kAccent);
        C2D_DrawText(&tabText_[i], C2D_WithColor, i * kTabW + 10, kTabY + 8, 0.5f, 0.5f, 0.5f,
                     i == s.tab ? kBg : kWhite);
    }
}

void Ui::unloadMap() {
    if (hasMap_) {
        mem::LinearGuard g;
        C3D_TexDelete(&mapTex_);
    }
    mapTex_ = C3D_Tex{};
    mapImage_ = C2D_Image{};
    hasMap_ = false;
}

// .tmap v1, see tools/pipeline/bake_minimap.py.
void Ui::setMapZone(unsigned fileZone) {
    if (fileZone == mapZone_) return;
    mapZone_ = fileZone;
    unloadMap();
    if (!fileZone) return;
    char path[80];
    snprintf(path, sizeof path, "sdmc:/3ds/toontown3ds/data/zones/%u.tmap", fileZone);
    FILE* f = fopen(path, "rb");
    if (!f) return;
    unsigned char h[48];
    bool ok = fread(h, 1, sizeof h, f) == sizeof h && memcmp(h, "TMAP", 4) == 0;
    u16 version = 0, w = 0, ht = 0;
    float rect[4] = {};
    u32 dataOffset = 0, dataSize = 0;
    if (ok) {
        memcpy(&version, h + 4, 2);
        memcpy(&w, h + 8, 2);
        memcpy(&ht, h + 10, 2);
        memcpy(rect, h + 16, sizeof rect);
        memcpy(&dataOffset, h + 32, 4);
        memcpy(&dataSize, h + 36, 4);
        ok = version == 1 && h[12] == GPU_ETC1 && w >= 8 && ht >= 8 && w <= 256 && ht <= 256 &&
             rect[2] > rect[0] && rect[3] > rect[1];
    }
    if (ok) {
        render::TexEntry e{};
        e.width = w;
        e.height = ht;
        e.format = GPU_ETC1;
        e.mipLevels = 1;
        e.wrapS = e.wrapT = GPU_CLAMP_TO_EDGE;
        e.dataOffset = dataOffset;
        e.dataSize = dataSize;
        snprintf(e.name, sizeof e.name, "minimap %u", fileZone);
        ok = render::loadTexture(f, e, &mapTex_);
    }
    fclose(f);
    if (!ok) {
        TT_LOG("map: bad or unreadable %s", path);
        mapTex_ = C3D_Tex{};
        return;
    }
    mapSub_.width = w;
    mapSub_.height = ht;
    mapSub_.left = 0.0f;
    mapSub_.top = 1.0f;
    mapSub_.right = 1.0f;
    mapSub_.bottom = 0.0f;
    mapImage_.tex = &mapTex_;
    mapImage_.subtex = &mapSub_;
    mapW_ = w;
    mapH_ = ht;
    mapMinX_ = rect[0];
    mapMinY_ = rect[1];
    mapMaxX_ = rect[2];
    mapMaxY_ = rect[3];
    hasMap_ = true;
    TT_LOG("map: %s %ux%u", path, w, ht);
}

void Ui::drawMap(const Context& c) {
    // Fit the map (baked image rectangle, else zone bounds) into the content area,
    // preserving aspect. Map +Y is up.
    const float areaX = 4, areaY = 4, areaW = kW - 8, areaH = kHoodRowY - 8;
    const float minX = hasMap_ ? mapMinX_ : c.zoneMinX, maxX = hasMap_ ? mapMaxX_ : c.zoneMaxX;
    const float minY = hasMap_ ? mapMinY_ : c.zoneMinY, maxY = hasMap_ ? mapMaxY_ : c.zoneMaxY;
    const float zw = maxX - minX, zh = maxY - minY;
    const float scale = hasMap_ ? fminf(areaW / mapW_, areaH / mapH_) * mapW_ / zw
                                : fminf(areaW / zw, areaH / zh);
    const float ox = areaX + (areaW - zw * scale) * 0.5f;
    const float oy = areaY + (areaH - zh * scale) * 0.5f;
    auto mx = [&](float x) { return ox + (x - minX) * scale; };
    auto my = [&](float y) { return oy + (maxY - y) * scale; };

    if (hasMap_) {
        C2D_DrawImageAt(mapImage_, ox, oy, 0.45f, nullptr, zw * scale / mapW_, zh * scale / mapH_);
    } else {
        C2D_DrawRectSolid(ox, oy, 0.5f, zw * scale, zh * scale, C2D_Color32(0x4E, 0x9A, 0x3C, 0xFF));
    }
    // Squares, not C2D_DrawCircleSolid: citro2d circles use the procedural-texture
    // unit and did not render after the 3D pass in Azahar (unverified on hardware).
    for (int i = 0; i < c.numMarkers; i++) {
        C2D_DrawRectSolid(mx(c.markers[i].x) - 3, my(c.markers[i].y) - 3, 0.55f, 6, 6, c.markers[i].color);
    }
    // Player arrow (heading 0 = +Y = up on the map; positive heading turns left).
    const float h = c.playerHeadingDeg * 3.14159265f / 180.0f;
    const float px = mx(c.playerX), py = my(c.playerY);
    const float fx = -sinf(h), fy = -cosf(h); // screen-space forward
    const float sx = -fy, sy = fx;
    // Long, narrow arrow so the tip is unambiguous at this size.
    C2D_DrawTriangle(px + fx * 11, py + fy * 11, kAccent, px - fx * 5 + sx * 4.5f, py - fy * 5 + sy * 4.5f, kAccent,
                     px - fx * 5 - sx * 4.5f, py - fy * 5 - sy * 4.5f, kAccent, 0.6f);
    text(8, 8, 0.45f, kWhite, "Zone %u  (%.0f, %.0f)", c.currentZone, c.playerX, c.playerY);
    C2D_DrawRectSolid(4, kHomeY, 0.5f, 76, kHomeH, kAccent);
    text(18, kHomeY + 6, 0.42f, kBg, "Home");
    // Teleport row: tap a playground to go there (installed ones only).
    for (int i = 0; i < kNumTeleportHoods; i++) {
        const bool have = c.hoodsAvailable & (1u << i);
        const bool here = kTeleportHoods[i] == c.currentHood;
        const float x = i * kHoodW + 1;
        C2D_DrawRectSolid(x, kHoodRowY, 0.5f, kHoodW - 2, kHoodRowH, here ? kAccent : have ? kPanel : kBg);
        text(x + 6, kHoodRowY + 6, 0.42f, here ? kBg : have ? kWhite : kDim, "%s", kHoodShort[i]);
    }
}

void Ui::drawToon(const State& s, const Context& c) {
    if (c.quests) { // online: the server's DNA wins, so the tab shows our ToonTasks instead
        quest_.drawTasks(*c.quests, 6, kTabY - 4);
        return;
    }
    static const char* const kTargets[3] = {"Body", "Shirt", "Shorts"};
    for (int i = 0; i < 3; i++) {
        C2D_DrawRectSolid(8 + i * 102, 8, 0.5f, 98, 28, i == s.colorTarget ? kAccent : kPanel);
        text(16 + i * 102, 14, 0.5f, i == s.colorTarget ? kBg : kWhite, "%s", kTargets[i]);
    }
    const int sel = s.colorTarget == 0 ? s.bodyColor : s.colorTarget == 1 ? s.shirtColor : s.bottomColor;
    for (int i = 0; i < kNumToonColors; i++) {
        const float x = kSwX + (i % kSwCols) * (kSwSize + kSwGap);
        const float y = kSwY + (i / kSwCols) * (kSwSize + kSwGap);
        if (i == sel) C2D_DrawRectSolid(x - 2, y - 2, 0.5f, kSwSize + 4, kSwSize + 4, kWhite);
        C2D_DrawRectSolid(x, y, 0.5f, kSwSize, kSwSize, colorOf(i));
    }
    text(8, 154, 0.45f, kDim, "Tap a color. Head/arms/legs share the body color.");
    text(8, 172, 0.45f, kDim, "A: jump  B: walk  L/R: orbit camera");
}

void Ui::drawSettings(const State& s) {
    text(12, 8, 0.5f, kWhite, "Settings");
    C2D_DrawRectSolid(kBtnX, kStereoY, 0.5f, kBtnW, kBtnH, s.stereo ? kAccent : kPanel);
    text(kBtnX + 8, kStereoY + 10, 0.5f, s.stereo ? kBg : kWhite, "Stereo 3D: %s", s.stereo ? "On" : "Off");
    text(kBtnX + kBtnW + 12, kStereoY + 10, 0.45f, kDim, "uses the 3D slider");

    C2D_DrawRectSolid(kBtnX, kStressY, 0.5f, 60, kBtnH, kPanel);
    C2D_DrawRectSolid(kBtnX + 80, kStressY, 0.5f, 60, kBtnH, kPanel);
    text(kBtnX + 24, kStressY + 8, 0.7f, kWhite, "-");
    text(kBtnX + 102, kStressY + 8, 0.7f, kWhite, "+");
    text(kBtnX + kBtnW + 12, kStressY + 10, 0.5f, kWhite, "Stress toons: %d", s.stressToons);
    text(12, 140, 0.45f, kDim, "Stress toons are extra copies of the toon,");
    text(12, 156, 0.45f, kDim, "used to measure the Old 3DS draw budget.");
}

void Ui::drawInfo(const Context& c) {
    text(12, 12, 0.7f, kAccent, "%s", c.infoTitle);
    for (int i = 0; i < 3; i++) {
        if (c.infoLines[i] && c.infoLines[i][0]) text(12, 60 + i * 30, 0.55f, kWhite, "%s", c.infoLines[i]);
    }
}

void Ui::drawGags(const Context& c) {
    // Like the Shticker Book inventory page: one row per track, counts per gag level.
    // Text is re-parsed every kDebugRefreshFrames (shares the debug line cache).
    if (numDebugLines_ == 0 || frame_ % kDebugRefreshFrames == 0) {
        C2D_TextBufClear(debugBuf_);
        numDebugLines_ = 0;
        if (!c.gagCount) {
            debugLine(8, 8, 0.5f, kDim, "Gags appear once you are online.");
        } else {
            int total = 0;
            for (int t = 0; t < gags::kTracks; t++)
                for (int l = 0; l < gags::kLevels; l++) total += c.gagCount[t][l];
            debugLine(8, 4, 0.5f, kWhite, "Gags  %d / %d", total, c.maxCarry);
            for (int t = 0; t < gags::kTracks; t++) {
                const bool have = c.trackAccess[t];
                const int levels = have ? gags::levelsUnlocked(t, c.gagExp[t]) : 0;
                const float y = 26 + t * 24;
                debugLine(8, y, 0.5f, have ? kWhite : kDim, "%s", gags::kTrackNames[t]);
                for (int l = 0; l < gags::kLevels; l++) {
                    if (l < levels) {
                        debugLine(80 + l * 22, y, 0.5f, c.gagCount[t][l] ? kWhite : kDim, "%u",
                                  unsigned(c.gagCount[t][l]));
                    } else {
                        debugLine(80 + l * 22, y, 0.5f, kDim, "-");
                    }
                }
                const uint16_t next = levels < gags::kLevels ? gags::kLevelExp[t][levels] : 0;
                if (have && next) {
                    debugLine(236, 28 + t * 24, 0.4f, kDim, "%u/%u", unsigned(c.gagExp[t]), unsigned(next));
                } else if (have) {
                    debugLine(236, 28 + t * 24, 0.4f, kDim, "%u max", unsigned(c.gagExp[t]));
                }
            }
        }
    }
    for (int i = 0; i < numDebugLines_; i++) {
        const CachedLine& l = debugLines_[i];
        C2D_DrawText(&l.text, C2D_WithColor, l.x, l.y, 0.5f, l.scale, l.scale, l.color);
    }
    if (c.gagCount) {
        for (int t = 0; t < gags::kTracks; t++) {
            if (!c.trackAccess[t]) continue;
            const int levels = gags::levelsUnlocked(t, c.gagExp[t]);
            const float y = 26 + t * 24;
            for (int l = 0; l < levels; l++) {
                world::GagAtlas::instance().drawIcon(t, l, 68 + l * 22, y + 2, 14.0f, 0.52f,
                                                     c.gagCount[t][l] ? 1.0f : 0.35f);
            }
        }
    }
}

void Ui::drawDebug(const Context& c) {
    if (numDebugLines_ == 0 || frame_ % kDebugRefreshFrames == 0) {
        const u32 good = C2D_Color32(0x8C, 0xF0, 0x8C, 0xFF);
        const u32 bad = C2D_Color32(0xFF, 0x80, 0x80, 0xFF);
        C2D_TextBufClear(debugBuf_);
        numDebugLines_ = 0;
        debugLine(8, 4, 0.5f, kWhite, "Debug  (%s)", c.isNew3ds ? "New 3DS" : "Old 3DS");
        debugLine(8, 24, 0.5f, c.fps >= 29.5f ? good : bad, "%.1f fps   cpu %.1f ms  gpu %.1f ms", c.fps, c.cpuMs,
                  c.gpuMs);
        debugLine(8, 42, 0.5f, kWhite, "draws %u  tris %u  models %u (+%u culled)", c.drawCalls, c.triangles,
                  c.modelsDrawn, c.modelsCulled);
        debugLine(8, 60, 0.5f, c.heapUsedKB < c.heapBudgetKB ? kWhite : bad, "heap  %u / %u KB used", c.heapUsedKB,
                  c.heapBudgetKB);
        debugLine(8, 78, 0.5f, kWhite, "linear %u / %u KB free", c.linearFreeKB, c.linearBudgetKB);
        debugLine(8, 96, 0.5f, kWhite, "vram  %u KB free", c.vramFreeKB);
        if (c.netState) {
            debugLine(8, 114, 0.45f, kWhite, "net %s  zone %u  in %u B/s out %u B/s  toons %d", c.netState, c.zoneId,
                      c.netInBps, c.netOutBps, c.remoteToons);
        } else {
            debugLine(8, 114, 0.45f, kDim, "offline  zone %u", c.zoneId);
        }
        // Main-thread CPU breakdown (core/profile.h): mean ms per frame over the last second.
        // "wait" is the vblank/GPU sync, not CPU work; busy = total - wait.
        debugLine(8, 130, 0.4f, prof::busyAvgMs() > 25.0f ? bad : good, "busy %.1f ms  (frame %.1f, wait %.1f)",
                  static_cast<double>(prof::busyAvgMs()), static_cast<double>(prof::totalAvgMs()),
                  static_cast<double>(prof::avgMs(prof::kWait)));
        constexpr int kProfRows = 6;
        for (int i = 0; i < prof::kCount; i++) {
            const float ms = prof::avgMs(static_cast<prof::Section>(i));
            const bool hot = ms > 4.0f && i != prof::kWait;
            debugLine(8 + (i / kProfRows) * 106, 143 + (i % kProfRows) * 10, 0.34f, hot ? bad : kWhite, "%-6s %5.2f",
                      prof::name(static_cast<prof::Section>(i)), static_cast<double>(ms));
        }
        debugLine(8, 199, 0.34f, kDim, "%s", log::recent(0));
    }
    for (int i = 0; i < numDebugLines_; i++) {
        const CachedLine& l = debugLines_[i];
        C2D_DrawText(&l.text, C2D_WithColor, l.x, l.y, 0.5f, l.scale, l.scale, l.color);
    }
}

} // namespace tt::ui
