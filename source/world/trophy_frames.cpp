#include "world/trophy_frames.h"

#include <cmath>
#include <cstring>

#include "core/log.h"
#include "net/client.h"
#include "render/ui_font.h"
#include "ui/projection.h"

namespace tt::world {

namespace {

constexpr float kDegToRad = 3.14159265f / 180.0f;
constexpr float kFrameScale = 1.4f;   // DistributedToonInterior.FrameScale
constexpr float kNameScale = 0.186f;  // namePath.setScale(0.186 / FrameScale) under the frame
constexpr float kWordwrap = 5.36f * kFrameScale;
constexpr float kFontLinePx = 30.0f;  // citro2d line height at scale 1 (render/ui_font.h)
constexpr float kBaseline = 0.8f;     // baseline, as a fraction of the line, from its top
constexpr float kMinLinePx = 2.0f;

// The frame's world matrix: trophy_origin (pos, H), then setPos(pos, 0, 0) and
// setScale(FrameScale, 1, FrameScale).
void frameMatrix(const float* o, float x, C3D_Mtx* out) {
    Mtx_Identity(out);
    Mtx_Translate(out, o[0], o[1], o[2], true);
    Mtx_RotateZ(out, o[3] * kDegToRad, true);
    Mtx_Translate(out, x, 0.0f, 0.0f, true);
    Mtx_Scale(out, kFrameScale, 1.0f, kFrameScale);
}

// Frame position of Toon i of n: pos = 1.25 - 1.25 * n, then += 2.5 per frame.
float frameX(int i, int n) { return 1.25f - 1.25f * n + 2.5f * i; }

} // namespace

bool TrophyFrames::init(ToonCache* cache) {
    cache_ = cache;
    buf_ = C2D_TextBufNew(512);
    return buf_ != nullptr;
}

void TrophyFrames::clear() {
    for (Entry& e : entries_) {
        if (e.hasLook && cache_) cache_->release(&e.look);
        e.hasLook = false;
    }
    count_ = 0;
    doId_ = version_ = 0;
    if (buf_) C2D_TextBufClear(buf_);
}

void TrophyFrames::shutdown() {
    clear();
    frame_.unload();
    if (buf_) C2D_TextBufDelete(buf_);
    buf_ = nullptr;
}

void TrophyFrames::update(const net::Client& client, bool originFound, float ox, float oy, float oz, float oh) {
    const net::ToonInteriorTrophies* t = originFound ? client.toonInterior() : nullptr;
    if (!t || !t->count) {
        if (count_ || doId_) clear();
        return;
    }
    origin_[0] = ox;
    origin_[1] = oy;
    origin_[2] = oz;
    origin_[3] = oh;
    for (int i = 0; i < count_; ++i) entries_[i].avatar.update(0.0f);
    if (t->doId == doId_ && t->version == version_) return;
    clear();
    if (!frameTried_) {
        frameTried_ = true;
        if (!frame_.load("romfs:/baked/trophy_frame.ttm")) TT_LOG("trophy: no trophy_frame.ttm");
    }
    doId_ = t->doId;
    version_ = t->version;
    for (int i = 0; i < t->count && i < kMax; ++i) {
        const auto& s = t->saved[i];
        Entry& e = entries_[count_++];
        ToonDna dna;
        if (!s.hasDna || !parseNetString(s.dna, net::kDnaSize, &dna)) dna = defaultToonDna();
        e.species = species(dna);
        e.hasLook = cache_ && cache_->acquire(dna, ToonLod::High, &e.look);
        e.avatar.setLook(&e.look);
        e.avatar.setHeadOnly(true);
        e.avatar.setBlink(false); // a ToonHead built for a frame never starts blinking
        e.avatar.hold(kAnimNeutral, 0.0f);
        render::textParse(&e.name, buf_, s.name);
        C2D_TextOptimize(&e.name);
    }
    TT_LOG("trophy: %d frame(s) at %.1f %.1f %.1f", count_, double(ox), double(oy), double(oz));
}

void TrophyFrames::draw(render::Renderer& renderer) {
    if (!count_ || !frame_.loaded()) return;
    for (int i = 0; i < count_; ++i) {
        Entry& e = entries_[i];
        C3D_Mtx frame;
        frameMatrix(origin_, frameX(i, count_), &frame);
        renderer.drawModel(frame_, frame);
        // head.setPosHprScale(0, -0.05, -0.05, 180, 0, 0, 0.55, 0.02, 0.55) under the frame;
        // rabbits drop to z -0.15, horses rise to 0.05, mice shrink to 0.45.
        float z = -0.05f, s = 0.55f;
        if (e.species == Species::Rabbit) z = -0.15f;
        if (e.species == Species::Horse) z = 0.05f;
        if (e.species == Species::Mouse) s = 0.45f;
        // Only this Toon's own head: the default toon's would be the wrong face while it loads.
        const ToonLook* look = &e.look;
        if (!e.hasLook || !look->ready()) continue;
        C3D_Mtx head = frame;
        Mtx_Translate(&head, 0.0f, -0.05f, z, true);
        Mtx_RotateZ(&head, 180.0f * kDegToRad, true);
        Mtx_Scale(&head, s, 0.02f, s);
        // A ToonHead has no body scale; the head-only draw applies the look's.
        const float inv = look->scale > 0.0f ? 1.0f / look->scale : 1.0f;
        Mtx_Scale(&head, inv, inv, inv);
        e.avatar.draw(renderer, head);
    }
}

void TrophyFrames::draw2D(const C3D_Mtx& proj, const C3D_Mtx& view) {
    if (!count_ || !frame_.loaded()) return;
    const float c = std::cos(origin_[3] * kDegToRad), n = std::sin(origin_[3] * kDegToRad);
    for (int i = 0; i < count_; ++i) {
        // namePath at frame (0, -0.03, -0.6): world offset (x, -0.03, -0.6 * FrameScale) from the
        // origin, one text unit = 0.186 along the frame's x and z.
        const float lx = frameX(i, count_), ly = -0.03f, lz = -0.6f * kFrameScale;
        const float o[3] = {origin_[0] + c * lx - n * ly, origin_[1] + n * lx + c * ly, origin_[2] + lz};
        float sx, sy, depth, ux, uy, vx, vy;
        if (!ui::projectToScreen(proj, view, o[0], o[1], o[2], &sx, &sy, &depth, 200.0f) || depth <= 0.0f) continue;
        if (!ui::projectToScreen(proj, view, o[0], o[1], o[2] + kNameScale, &ux, &uy, nullptr, 400.0f)) continue;
        if (!ui::projectToScreen(proj, view, o[0] + c * kNameScale, o[1] + n * kNameScale, o[2], &vx, &vy, nullptr,
                                 400.0f))
            continue;
        const float linePx = std::sqrt((ux - sx) * (ux - sx) + (uy - sy) * (uy - sy));
        const float widthPx = vx - sx; // negative: seen from behind
        if (linePx < kMinLinePx || widthPx <= 0.0f) continue;
        const float unitX = std::sqrt(widthPx * widthPx + (vy - sy) * (vy - sy));
        const float scaleY = linePx / kFontLinePx, scaleX = unitX / kFontLinePx;
        C2D_DrawText(&entries_[i].name, C2D_WithColor | C2D_AlignCenter | C2D_WordWrap, sx,
                     sy - kBaseline * kFontLinePx * scaleY, 0.5f, scaleX, scaleY, C2D_Color32(0, 0, 0, 255),
                     kWordwrap * unitX);
    }
}

} // namespace tt::world
