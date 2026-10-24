#include "ui/balloons.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace tt::ui {

namespace {

constexpr float kZ = 0.5f;
constexpr float kNameScale = 0.45f;
constexpr float kTextScale = 0.45f;
constexpr float kNamePad = 3.0f, kNameRadius = 4.0f;
constexpr float kBubblePadX = 7.0f, kBubblePadY = 5.0f, kBubbleRadius = 7.0f;
constexpr float kTailH = 9.0f, kTailHalfW = 5.0f;
constexpr float kScreenPad = 2.0f;
constexpr int kMaxLines = 6;

// Quarter-circle offsets (cos, sin) for rounded corners, 3 segments.
constexpr int kArcSegs = 3;
const float kArc[kArcSegs + 1][2] = {{1.0f, 0.0f}, {0.866025f, 0.5f}, {0.5f, 0.866025f}, {0.0f, 1.0f}};

u32 withAlpha(u32 abgr, float a) {
    const u32 base = abgr >> 24;
    const u32 na = static_cast<u32>(base * a + 0.5f);
    return (abgr & 0x00FFFFFFu) | (na << 24);
}

// Rounded rectangle from triangles: citro2d circles need the proctex unit, which did not
// render after the 3D pass in Azahar (AGENTS.md Phase 1 follow-ups).
void roundRect(float x, float y, float w, float h, float r, u32 c) {
    if (r * 2 > w) r = w * 0.5f;
    if (r * 2 > h) r = h * 0.5f;
    C2D_DrawRectSolid(x + r, y, kZ, w - 2 * r, h, c);
    C2D_DrawRectSolid(x, y + r, kZ, r, h - 2 * r, c);
    C2D_DrawRectSolid(x + w - r, y + r, kZ, r, h - 2 * r, c);
    const float cx[4] = {x + w - r, x + r, x + r, x + w - r};
    const float cy[4] = {y + r, y + r, y + h - r, y + h - r};
    const float sx[4] = {1, -1, -1, 1};
    const float sy[4] = {-1, -1, 1, 1};
    for (int k = 0; k < 4; k++) {
        for (int i = 0; i < kArcSegs; i++) {
            C2D_DrawTriangle(cx[k], cy[k], c, cx[k] + sx[k] * kArc[i][0] * r, cy[k] + sy[k] * kArc[i][1] * r, c,
                             cx[k] + sx[k] * kArc[i + 1][0] * r, cy[k] + sy[k] * kArc[i + 1][1] * r, c, kZ);
        }
    }
}

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

} // namespace

bool Balloons::init() {
    if (R_FAILED(fontEnsureMapped())) return false;
    const TGLP_s* g = fontGetGlyphInfo(fontGetSystemFont());
    fontScale_ = g && g->cellHeight ? 30.0f / g->cellHeight : 1.0f;
    buf_ = C2D_TextBufNew(kGlyphCapacity);
    cache_ = static_cast<Entry*>(calloc(kCacheSlots, sizeof(Entry)));
    return buf_ && cache_;
}

void Balloons::shutdown() {
    if (buf_) C2D_TextBufDelete(buf_);
    buf_ = nullptr;
    free(cache_);
    cache_ = nullptr;
}

void Balloons::begin() { numSpeakers_ = 0; }

void Balloons::addSpeaker(float sx, float sy, const char* name, const char* phrase, float ageSec, float depth,
                          u32 nameColor) {
    if (numSpeakers_ >= kMaxSpeakers) return;
    Speaker& s = speakers_[numSpeakers_++];
    s.sx = sx;
    s.sy = sy;
    s.age = ageSec;
    s.depth = depth;
    s.name = name && name[0] ? name : nullptr;
    s.phrase = phrase && phrase[0] && ageSec < kHoldSec + kFadeSec ? phrase : nullptr;
    s.nameColor = nameColor;
    s.nameEntry = s.phraseEntry = -1;
}

void Balloons::flush() {
    C2D_TextBufClear(buf_);
    for (int i = 0; i < kCacheSlots; i++) cache_[i].used = 0;
}

// Greedy word wrap at kMaxTextW using the system font's advance widths (cheap: no text
// buffer). Runs only on a cache miss.
void Balloons::wrapInto(const char* src, char* dst, int cap) const {
    const float maxW = kMaxTextW / (kTextScale * fontScale_); // in font units
    int n = 0, lastSpace = -1, lines = 1;
    float lineW = 0.0f, sinceSpace = 0.0f;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(src); *p && n < cap - 1;) {
        u32 code = *p;
        int units = 1;
        if (code >= 0xC0) { // minimal UTF-8 decode; invalid bytes count as one glyph
            units = code >= 0xF0 ? 4 : code >= 0xE0 ? 3 : 2;
            code &= (0x3F >> (units - 1));
            for (int k = 1; k < units; k++) {
                if ((p[k] & 0xC0) != 0x80) {
                    units = k;
                    break;
                }
                code = (code << 6) | (p[k] & 0x3F);
            }
        }
        if (n + units >= cap) break;
        if (code == '\n') {
            if (++lines > kMaxLines) break;
            lineW = sinceSpace = 0.0f;
            lastSpace = -1;
            dst[n++] = '\n';
            p++;
            continue;
        }
        const charWidthInfo_s* cw = C2D_FontGetCharWidthInfo(nullptr, C2D_FontGlyphIndexFromCodePoint(nullptr, code));
        const float adv = cw ? cw->charWidth : 0.0f;
        if (code == ' ') {
            lastSpace = n;
            sinceSpace = 0.0f;
        } else {
            sinceSpace += adv;
        }
        lineW += adv;
        for (int k = 0; k < units; k++) dst[n++] = static_cast<char>(p[k]);
        p += units;
        if (lineW > maxW && lastSpace >= 0 && code != ' ') {
            if (++lines > kMaxLines) {
                n = lastSpace;
                break;
            }
            dst[lastSpace] = '\n';
            lineW = sinceSpace;
            lastSpace = -1;
        }
    }
    dst[n] = '\0';
}

int Balloons::resolve(const char* s, bool wrap) {
    int freeSlot = -1;
    for (int i = 0; i < kCacheSlots; i++) {
        Entry& e = cache_[i];
        if (!e.used) {
            if (freeSlot < 0) freeSlot = i;
            continue;
        }
        if (e.wrapped == wrap && strncmp(e.str, s, kMaxStr - 1) == 0) {
            return i;
        }
    }
    char tmp[kMaxStr + kMaxLines];
    if (wrap) {
        wrapInto(s, tmp, sizeof(tmp));
    } else {
        strncpy(tmp, s, kMaxStr - 1);
        tmp[kMaxStr - 1] = '\0';
    }
    const size_t need = strlen(tmp);
    if (freeSlot < 0 || C2D_TextBufGetNumGlyphs(buf_) + need > static_cast<size_t>(kGlyphCapacity)) return -1;
    Entry& e = cache_[freeSlot];
    strncpy(e.str, s, kMaxStr - 1);
    e.str[kMaxStr - 1] = '\0';
    e.wrapped = wrap;
    e.used = 1;
    C2D_TextParse(&e.text, buf_, tmp);
    C2D_TextOptimize(&e.text);
    const float sc = wrap ? kTextScale : kNameScale;
    C2D_TextGetDimensions(&e.text, sc, sc, &e.w, &e.h);
    return freeSlot;
}

void Balloons::draw(C3D_RenderTarget* top, float parallaxPx) {
    if (!buf_ || !cache_) return;

    // Resolve text. A miss that does not fit flushes the cache and restarts once; one
    // frame's strings (<= 2 * kMaxSpeakers) always fit an empty cache.
    for (int pass = 0; pass < 2; pass++) {
        bool ok = true;
        for (int i = 0; i < numSpeakers_ && ok; i++) {
            Speaker& s = speakers_[i];
            s.nameEntry = s.phraseEntry = -1;
            if (s.phrase) ok = (s.phraseEntry = resolve(s.phrase, true)) >= 0;
            else if (s.name) ok = (s.nameEntry = resolve(s.name, false)) >= 0;
        }
        if (ok) break;
        flush();
    }

    // Far to near, so nearer speakers overlap farther ones (insertion sort, n <= 12).
    int order[kMaxSpeakers];
    for (int i = 0; i < numSpeakers_; i++) {
        int j = i;
        while (j > 0 && speakers_[order[j - 1]].depth < speakers_[i].depth) {
            order[j] = order[j - 1];
            j--;
        }
        order[j] = i;
    }

    C2D_SceneBegin(top);
    // The world left its depth buffer on this target; overlays must not be depth tested.
    C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);

    const u32 kWhiteA = C2D_Color32(0xFF, 0xFF, 0xFF, 0xA0);
    const u32 kBubble = C2D_Color32(0xFF, 0xFF, 0xFF, 0xF2);
    const u32 kOutline = C2D_Color32(0x50, 0x50, 0x50, 0xC0);
    const u32 kInk = C2D_Color32(0x00, 0x00, 0x00, 0xFF);

    for (int oi = 0; oi < numSpeakers_; oi++) {
        const Speaker& s = speakers_[order[oi]];
        const float headX = s.sx + parallaxPx;
        if (s.phraseEntry >= 0) {
            const Entry& e = cache_[s.phraseEntry];
            const float a = s.age <= kHoldSec ? 1.0f : clampf(1.0f - (s.age - kHoldSec) / kFadeSec, 0.0f, 1.0f);
            const float bw = e.w + 2 * kBubblePadX, bh = e.h + 2 * kBubblePadY;
            const float tipX = clampf(headX, kScreenPad + kBubbleRadius + kTailHalfW,
                                      kTopW - kScreenPad - kBubbleRadius - kTailHalfW);
            const float tipY = clampf(s.sy, bh + kTailH + kScreenPad, kTopH);
            // Bubble sits up-and-right of the tail like Toontown's balloon, clamped on screen.
            const float bx = clampf(tipX - bw * 0.35f, kScreenPad, kTopW - kScreenPad - bw);
            const float by = tipY - kTailH - bh;
            const float tailX = clampf(tipX, bx + kBubbleRadius + kTailHalfW, bx + bw - kBubbleRadius - kTailHalfW);
            const u32 ol = withAlpha(kOutline, a), fill = withAlpha(kBubble, a);
            roundRect(bx - 1, by - 1, bw + 2, bh + 2, kBubbleRadius + 1, ol);
            C2D_DrawTriangle(tailX - kTailHalfW - 1.5f, by + bh - 1, ol, tailX + kTailHalfW + 1.5f, by + bh - 1, ol,
                             tipX, tipY + 1.5f, ol, kZ);
            roundRect(bx, by, bw, bh, kBubbleRadius, fill);
            C2D_DrawTriangle(tailX - kTailHalfW, by + bh - 1, fill, tailX + kTailHalfW, by + bh - 1, fill, tipX, tipY,
                             fill, kZ);
            C2D_DrawText(&e.text, C2D_WithColor, bx + kBubblePadX, by + kBubblePadY, kZ, kTextScale, kTextScale,
                         withAlpha(kInk, a));
        } else if (s.nameEntry >= 0) {
            const Entry& e = cache_[s.nameEntry];
            const float bw = e.w + 2 * kNamePad, bh = e.h + kNamePad;
            const float bx = clampf(headX - bw * 0.5f, kScreenPad, kTopW - kScreenPad - bw);
            const float by = clampf(s.sy - bh, kScreenPad, kTopH - kScreenPad - bh);
            roundRect(bx, by, bw, bh, kNameRadius, kWhiteA);
            C2D_DrawText(&e.text, C2D_WithColor, bx + kNamePad, by + kNamePad * 0.5f, kZ, kNameScale, kNameScale,
                         s.nameColor);
        }
    }

    C2D_Flush();
    C3D_DepthTest(true, GPU_GEQUAL, GPU_WRITE_ALL); // citro2d's C2D_Prepare default
}

} // namespace tt::ui
