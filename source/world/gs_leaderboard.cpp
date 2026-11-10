#include "world/gs_leaderboard.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "net/client.h"
#include "net/net_platform.h"
#include "render/ui_font.h"
#include "ui/projection.h"
#include "world/gs_tunnel_signs.h"

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

// A TextNode's frame in world space: its origin (first line's baseline) and one text unit
// along its x and z axes.
struct TextFrame {
    float o[3], x[3], z[3];
};

// Same projection as drawRow for a general (scaled, rotated) TextNode frame. Line n of a
// multi-line text sits lineHeight * n below the first, as TextNode lays it out.
void drawFramed(const TextFrame& f, const C3D_Mtx& proj, const C3D_Mtx& view, const C2D_Text& text,
                float line, Align align, u32 color) {
    float w[3], sx, sy, depth, ux, uy, vx, vy;
    for (int i = 0; i < 3; ++i) w[i] = f.o[i] - f.z[i] * line;
    if (!ui::projectToScreen(proj, view, w[0], w[1], w[2], &sx, &sy, &depth, 200.0f) || depth <= 0.0f) return;
    if (!ui::projectToScreen(proj, view, w[0] + f.z[0], w[1] + f.z[1], w[2] + f.z[2], &ux, &uy, nullptr, 400.0f))
        return;
    if (!ui::projectToScreen(proj, view, w[0] + f.x[0], w[1] + f.x[1], w[2] + f.x[2], &vx, &vy, nullptr, 400.0f))
        return;
    const float linePx = std::sqrt((ux - sx) * (ux - sx) + (uy - sy) * (uy - sy));
    const float widthPx = vx - sx;
    if (linePx < kMinLinePx || widthPx <= 0.0f) return;
    const float scaleY = linePx / kFontLinePx;
    const float scaleX = std::sqrt(widthPx * widthPx + (vy - sy) * (vy - sy)) / kFontLinePx;
    float textW = 0.0f;
    C2D_TextGetDimensions(&text, scaleX, scaleY, &textW, nullptr);
    float x = sx;
    if (align == Align::Center) x -= textW * 0.5f;
    C2D_DrawText(&text, C2D_WithColor, x, sy - kBaseline * kFontLinePx * scaleY, 0.5f, scaleX, scaleY, color);
}

// DistributedRacePad.getSignTextNodes: a TextNode under the sign at pos, H 165, scale s.
TextFrame signFrame(const world::gs_signs::Sign& sg, float px, float py, float pz, float s) {
    const float c = std::cos(165.0f * kDegToRad), n = std::sin(165.0f * kDegToRad);
    const float lx[3] = {c * s, n * s, 0.0f}, lz[3] = {0.0f, 0.0f, s}, lp[3] = {px, py, pz};
    TextFrame f;
    for (int i = 0; i < 3; ++i) {
        f.o[i] = sg.m[i][3] + sg.m[i][0] * lp[0] + sg.m[i][1] * lp[1] + sg.m[i][2] * lp[2];
        f.x[i] = sg.m[i][0] * lx[0] + sg.m[i][1] * lx[1] + sg.m[i][2] * lx[2];
        f.z[i] = sg.m[i][0] * lz[0] + sg.m[i][1] * lz[1] + sg.m[i][2] * lz[2];
    }
    return f;
}

const world::gs_signs::Track* findTrack(int id) {
    for (const auto& t : world::gs_signs::kTrack)
        if (t.id == id) return &t;
    return nullptr;
}

// RaceGlobals.RaceInfo2RacePadId
int racePadId(int trackId, int trackType) {
    const bool rev = trackId % 2;
    if (!rev) return trackType == 0 ? 0 : 2;
    return trackType == 0 ? 1 : 3;
}

const u32 kSignGrey = C2D_Color32f(0.5f, 0.5f, 0.5f, 1.0f);
const u32 kSignBlack = C2D_Color32f(0.0f, 0.0f, 0.0f, 1.0f);
constexpr float kToonFontLine = 1.2375f; // ImpressBT line height (TextFont.getLineHeight)
constexpr uint32_t kCountdownMs = 30000;  // KartGlobals.COUNTDOWN_TIME

} // namespace

bool GsLeaderBoards::init() {
    buf_ = C2D_TextBufNew(4096);
    signBuf_ = C2D_TextBufNew(1024);
    clockBuf_ = C2D_TextBufNew(256);
    return buf_ != nullptr && signBuf_ != nullptr && clockBuf_ != nullptr;
}

void GsLeaderBoards::shutdown() {
    if (buf_) C2D_TextBufDelete(buf_);
    if (signBuf_) C2D_TextBufDelete(signBuf_);
    if (clockBuf_) C2D_TextBufDelete(clockBuf_);
    buf_ = signBuf_ = clockBuf_ = nullptr;
}

void GsLeaderBoards::drawTunnelSigns(const net::Client& client, const C3D_Mtx& proj, const C3D_Mtx& view) {
    namespace S = world::gs_signs;
    static_assert(S::kSigns == kTunnelSigns, "gs_tunnel_signs.h sign count");
    // Which pad's track is on each sign this frame.
    int track[kTunnelSigns], type[kTunnelSigns], countdown[kTunnelSigns];
    for (int i = 0; i < kTunnelSigns; ++i) track[i] = type[i] = countdown[i] = -1;
    const uint32_t now = net::plat::nowMs();
    for (int i = 0; i < net::kMaxRacingPads; ++i) {
        const net::RacingPad& p = client.racingPadSlot(i);
        if (!p.active || p.viewing || p.trackCount < 2) continue;
        const S::Track* t = findTrack(p.trackIds[0]);
        if (!t) continue;
        const int pad = racePadId(t->id, p.trackIds[1]);
        for (int k = 0; k < kTunnelSigns; ++k) {
            if (S::kSign[k].genre != t->genre || S::kSign[k].padId != pad) continue;
            track[k] = t->id;
            type[k] = p.trackIds[1];
            // startCountdown: int(COUNTDOWN_TIME - elapsed), while the pad waits for racers.
            if (!strcmp(p.state, "WaitCountdown")) {
                const uint32_t el = now - p.stateMs;
                countdown[k] = el < kCountdownMs ? int((kCountdownMs - el) / 1000) : 0;
            }
        }
    }
    bool stale = false, clockStale = false;
    for (int k = 0; k < kTunnelSigns; ++k) {
        stale = stale || track[k] != sign_[k].track || type[k] != sign_[k].type;
        clockStale = clockStale || countdown[k] != sign_[k].countdown;
    }
    if (stale) {
        C2D_TextBufClear(signBuf_);
        for (int k = 0; k < kTunnelSigns; ++k) {
            SignText& st = sign_[k];
            st.track = track[k];
            st.type = type[k];
            const S::Track* t = track[k] >= 0 ? findTrack(track[k]) : nullptr;
            if (!t) continue;
            // DistributedRacePad.updateTunnelSignText
            char fee[16], qual[24];
            std::snprintf(fee, sizeof(fee), "%d", type[k] ? int(t->fee) : 0);
            const int secs = int(t->qualify), hundredths = int((t->qualify - secs) * 100.0f);
            std::snprintf(qual, sizeof(qual), "%02d:%02d:%02d", secs / 60, secs % 60, hundredths);
            render::textParseSign(&st.name, signBuf_, t->name);
            render::textParseSign(&st.raceType, signBuf_, S::kRaceNames[type[k] >= 0 && type[k] < 3 ? type[k] : 0]);
            render::textParse(&st.deposit[0], signBuf_, S::kDepositLabel);
            render::textParse(&st.deposit[1], signBuf_, fee);
            render::textParse(&st.qualify[0], signBuf_, S::kQualifyLabel);
            render::textParse(&st.qualify[1], signBuf_, qual);
            C2D_Text* all[] = {&st.name, &st.raceType, &st.deposit[0], &st.deposit[1], &st.qualify[0], &st.qualify[1]};
            for (C2D_Text* x : all) C2D_TextOptimize(x);
        }
    }
    if (clockStale) {
        C2D_TextBufClear(clockBuf_);
        for (int k = 0; k < kTunnelSigns; ++k) {
            sign_[k].countdown = countdown[k];
            if (countdown[k] < 0) continue;
            char c[12];
            std::snprintf(c, sizeof(c), "%d", countdown[k]);
            render::textParseSign(&sign_[k].clock, clockBuf_, c);
            C2D_TextOptimize(&sign_[k].clock);
        }
    }
    for (int k = 0; k < kTunnelSigns; ++k) {
        const SignText& st = sign_[k];
        if (st.track < 0) continue;
        const S::Sign& sg = S::kSign[k];
        // makeTextNodes / makeClockGui
        drawFramed(signFrame(sg, 0.0f, 0.25f, 0.7f, 0.875f), proj, view, st.name, 0, Align::Center, kSignGrey);
        drawFramed(signFrame(sg, 0.0f, 0.25f, -0.35f, 0.875f), proj, view, st.raceType, 0, Align::Center, kSignGrey);
        const TextFrame dep = signFrame(sg, 4.0f, -1.0f, -2.0f, 0.75f);
        const TextFrame qua = signFrame(sg, -4.0f, 1.2f, -2.0f, 0.75f);
        for (int l = 0; l < 2; ++l) {
            drawFramed(dep, proj, view, st.deposit[l], l * kToonFontLine, Align::Center, kSignBlack);
            drawFramed(qua, proj, view, st.qualify[l], l * kToonFontLine, Align::Center, kSignBlack);
        }
        if (st.countdown >= 0)
            drawFramed(signFrame(sg, 0.0f, 0.125f, -3.0f, 2.5f), proj, view, st.clock, 0, Align::Center, kSignGrey);
    }
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
    drawTunnelSigns(client, proj, view);
}

} // namespace tt::world
