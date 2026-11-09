#include "world/gs_leaderboard.h"

#include <cmath>
#include <cstdio>

#include "net/client.h"
#include "render/ui_font.h"
#include "ui/projection.h"

namespace tt::world {

namespace {

constexpr float kDegToRad = 3.14159265f / 180.0f;
constexpr float kFontLinePx = 30.0f; // citro2d line height at scale 1 (render/ui_font.h)
constexpr float kBaseline = 0.8f;    // baseline, as a fraction of the line, from its top
constexpr float kMinLinePx = 2.0f;   // thinner rows are not legible at 400x240

// TextNode colours from DistributedLeaderBoard.build*Row.
const u32 kTrackColor = C2D_Color32f(0.5f, 0.75f, 0.7f, 1.0f);
const u32 kPeriodColor = C2D_Color32f(0.3f, 0.75f, 0.6f, 1.0f);
const u32 kNameColor = C2D_Color32f(0.125f, 0.0f, 0.5f, 1.0f);
const u32 kTimeColor = C2D_Color32f(0.0f, 0.0f, 0.0f, 1.0f);
const u32 kPlaceColor = C2D_Color32f(1.0f, 1.0f, 0.1f, 1.0f);

enum class Align { Left, Center, Right };

// The board's text surface: setPosHpr(x, y, z, h, p, r) on a node under render.
struct Surface {
    float pos[3];
    float m[3][3]; // local -> world rotation (Panda HPR)

    explicit Surface(const net::GsLeaderBoard& b) {
        for (int i = 0; i < 3; ++i) pos[i] = b.pos[i];
        const float ch = std::cos(b.hpr[0] * kDegToRad), sh = std::sin(b.hpr[0] * kDegToRad);
        const float cp = std::cos(b.hpr[1] * kDegToRad), sp = std::sin(b.hpr[1] * kDegToRad);
        const float cr = std::cos(b.hpr[2] * kDegToRad), sr = std::sin(b.hpr[2] * kDegToRad);
        // Rz(h) * Rx(p) * Ry(r)
        m[0][0] = ch * cr - sh * sp * sr; m[0][1] = -sh * cp; m[0][2] = ch * sr + sh * sp * cr;
        m[1][0] = sh * cr + ch * sp * sr; m[1][1] = ch * cp;  m[1][2] = sh * sr - ch * sp * cr;
        m[2][0] = -cp * sr;               m[2][1] = sp;       m[2][2] = cp * cr;
    }
    void world(float lx, float ly, float lz, float* w) const {
        for (int i = 0; i < 3; ++i) w[i] = pos[i] + m[i][0] * lx + m[i][1] * ly + m[i][2] * lz;
    }
};

// Draws one TextNode at surface-local (lx, 1.6, lz) with TextNode scale s: the row's baseline
// point is projected, and the text is scaled by how long one local unit along x and z is on
// screen, so a board seen at an angle is narrower. Text faces -y: from behind it is skipped.
void drawRow(const Surface& sf, const C3D_Mtx& proj, const C3D_Mtx& view, const C2D_Text& text, float lx,
             float lz, float s, Align align, u32 color) {
    constexpr float kY = 1.6f;
    float w[3], sx, sy, depth;
    sf.world(lx, kY, lz, w);
    if (!ui::projectToScreen(proj, view, w[0], w[1], w[2], &sx, &sy, &depth, 200.0f) || depth <= 0.0f) return;
    float ux, uy, vx, vy;
    sf.world(lx, kY, lz + s, w);
    if (!ui::projectToScreen(proj, view, w[0], w[1], w[2], &ux, &uy, nullptr, 400.0f)) return;
    sf.world(lx + s, kY, lz, w);
    if (!ui::projectToScreen(proj, view, w[0], w[1], w[2], &vx, &vy, nullptr, 400.0f)) return;
    const float linePx = std::sqrt((ux - sx) * (ux - sx) + (uy - sy) * (uy - sy));
    const float widthPx = vx - sx; // negative: seen from behind
    if (linePx < kMinLinePx || widthPx <= 0.0f) return;
    const float scaleY = linePx / kFontLinePx;
    const float scaleX = std::sqrt(widthPx * widthPx + (vy - sy) * (vy - sy)) / kFontLinePx;
    float textW = 0.0f;
    C2D_TextGetDimensions(&text, scaleX, scaleY, &textW, nullptr);
    float x = sx;
    if (align == Align::Center) x -= textW * 0.5f;
    if (align == Align::Right) x -= textW;
    C2D_DrawText(&text, C2D_WithColor, x, sy - kBaseline * kFontLinePx * scaleY, 0.5f, scaleX, scaleY, color);
}

} // namespace

bool GsLeaderBoards::init() {
    buf_ = C2D_TextBufNew(4096);
    return buf_ != nullptr;
}

void GsLeaderBoards::shutdown() {
    if (buf_) C2D_TextBufDelete(buf_);
    buf_ = nullptr;
}

void GsLeaderBoards::rebuild(const net::Client& client) {
    C2D_TextBufClear(buf_);
    for (int i = 0; i < kBoards; ++i) {
        const net::GsLeaderBoard& b = client.leaderBoardSlot(i);
        BoardText& t = text_[i];
        t.valid = b.active;
        t.doId = b.doId;
        t.version = b.version;
        if (!b.active) continue;
        render::textParseSign(&t.track, buf_, b.track);
        render::textParseSign(&t.period, buf_, b.period);
        C2D_TextOptimize(&t.track);
        C2D_TextOptimize(&t.period);
        for (int r = 0; r < kRows; ++r) {
            char place[8];
            std::snprintf(place, sizeof(place), "%d.", r + 1);
            render::textParseSign(&t.place[r], buf_, place);
            render::textParse(&t.name[r], buf_, b.names[r]);
            render::textParse(&t.time[r], buf_, b.times[r]);
            C2D_TextOptimize(&t.place[r]);
            C2D_TextOptimize(&t.name[r]);
            C2D_TextOptimize(&t.time[r]);
        }
    }
}

void GsLeaderBoards::draw2D(const net::Client& client, const C3D_Mtx& proj, const C3D_Mtx& view,
                            C3D_RenderTarget* top) {
    if (!active_ || !buf_) return;
    bool stale = false;
    for (int i = 0; i < kBoards; ++i) {
        const net::GsLeaderBoard& b = client.leaderBoardSlot(i);
        const BoardText& t = text_[i];
        if (b.active != t.valid || (b.active && (b.doId != t.doId || b.version != t.version))) stale = true;
    }
    if (stale) rebuild(client);
    C2D_SceneBegin(top);
    // The world left its depth buffer on this target; text is not depth tested (as the HQ's).
    C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
    for (int i = 0; i < kBoards; ++i) {
        if (!text_[i].valid) continue;
        const Surface sf(client.leaderBoardSlot(i));
        const BoardText& t = text_[i];
        // DistributedLeaderBoard.buildListParts
        drawRow(sf, proj, view, t.track, 0.0f, 7.7f, 0.55f, Align::Center, kTrackColor);
        drawRow(sf, proj, view, t.period, 0.0f, 7.3f, 0.4f, Align::Center, kPeriodColor); // DLBbuildTitleRow
        for (int r = 0; r < kRows; ++r) {
            const float x = r < 5 ? -3.7f : 0.35f;
            const float z = 6.9f - 0.4f * (r % 5);
            drawRow(sf, proj, view, t.name[r], x + 1.1f, z, 0.23f, Align::Left, kNameColor);
            drawRow(sf, proj, view, t.time[r], x + 1.0f, z, 0.23f, Align::Right, kTimeColor);
            drawRow(sf, proj, view, t.place[r], x - 0.1f, z - 0.05f, 0.3f, Align::Right, kPlaceColor);
        }
    }
}

} // namespace tt::world
