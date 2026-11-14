#include "world/anim_props.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "core/log.h"
#include "render/quality.h"
#include "render/renderer.h"

namespace tt::world {

namespace {

constexpr float kSourceFps = 24.0f; // HQ_periscope-chan / HQ_telescope-chan

// HQPeriscopeAnimatedProp / HQTelescopeAnimatedProp.track, looped: Wait(wait) then
// actorInterval('anim', startFrame=from, endFrame=to); a negative 'from' is the final Wait.
struct Segment {
    float wait;
    int from, to;
};
constexpr Segment kPeriscope[] = {{2.0f, 0, 40},   {0.7f, 40, 90}, {0.7f, 91, 121}, {0.7f, 121, 91},
                                  {0.7f, 90, 40},  {0.7f, 40, 90}, {0.7f, 91, 121}, {0.5f, 121, 148},
                                  {3.0f, -1, -1}};
constexpr Segment kTelescope[] = {{5.0f, 0, 32},   {0.5f, 32, 78}, {0.5f, 79, 112}, {0.5f, 112, 79},
                                  {0.5f, 78, 32},  {0.5f, 32, 78}, {0.5f, 79, 112}, {0.5f, 112, 148},
                                  {4.0f, -1, -1}};

// ActorInterval duration: (|to - from| + 1) frames at the anim's rate.
float segmentSeconds(const Segment& s) {
    return s.wait + (s.from < 0 ? 0.0f : float(std::abs(s.to - s.from) + 1) / kSourceFps);
}

// The source frame the track shows at time t (seconds).
float trackFrame(const Segment* segs, int n, float t) {
    float total = 0.0f;
    for (int i = 0; i < n; ++i) total += segmentSeconds(segs[i]);
    t = std::fmod(t, total);
    float held = 0.0f; // a Wait holds the last frame shown
    for (int i = 0; i < n; ++i) {
        const Segment& s = segs[i];
        if (t < s.wait) return held;
        t -= s.wait;
        if (s.from < 0) return held;
        const float span = float(std::abs(s.to - s.from));
        const float dur = (span + 1.0f) / kSourceFps;
        if (t < dur) {
            const float f = t * kSourceFps < span ? t * kSourceFps : span;
            return s.to >= s.from ? s.from + f : s.from - f;
        }
        t -= dur;
        held = float(s.to);
    }
    return held;
}

} // namespace

void AnimProps::shutdown() {
    for (Kind& k : kinds_) {
        k.model.unload();
        k.anim.unload();
        k.stem[0] = 0;
    }
    numKinds_ = count_ = 0;
    zone_ = 0;
}

void AnimProps::setZone(uint32_t zoneId) {
    if (zoneId == zone_) return;
    shutdown();
    zone_ = zoneId;
    char path[96];
    snprintf(path, sizeof(path), "sdmc:/3ds/toontown3ds/data/zones/%lu.tap", static_cast<unsigned long>(zoneId));
    FILE* f = std::fopen(path, "r");
    if (!f) return;
    char line[512];
    while (count_ < kMaxInstances && std::fgets(line, sizeof(line), f)) {
        char stem[64];
        int track = 0, used = 0;
        float m[12];
        if (std::sscanf(line, "%63s %d%n", stem, &track, &used) != 2) continue;
        const char* p = line + used;
        bool ok = true;
        for (int i = 0; i < 12 && ok; ++i) {
            char* end = nullptr;
            m[i] = std::strtof(p, &end);
            ok = end != p;
            p = end;
        }
        if (!ok) continue;
        int kind = -1;
        for (int i = 0; i < numKinds_; ++i)
            if (!std::strcmp(kinds_[i].stem, stem)) kind = i;
        if (kind < 0) {
            if (numKinds_ == kMaxKinds) continue;
            Kind& k = kinds_[numKinds_];
            char file[128];
            snprintf(file, sizeof(file), "sdmc:/3ds/toontown3ds/data/animprops/%s.ttm", stem);
            bool loaded = k.model.load(file);
            snprintf(file, sizeof(file), "sdmc:/3ds/toontown3ds/data/animprops/%s.tta", stem);
            loaded = k.anim.load(file) && loaded;
            if (!loaded || k.anim.numVertices() != k.model.numVertices()) {
                TT_LOG("animprops: cannot load %s", stem);
                k.model.unload();
                k.anim.unload();
                continue;
            }
            std::snprintf(k.stem, sizeof(k.stem), "%s", stem);
            kind = numKinds_++;
        }
        Instance& in = instances_[count_++];
        Mtx_Identity(&in.world);
        for (int r = 0; r < 3; ++r) {
            in.world.r[r].x = m[r * 4 + 0];
            in.world.r[r].y = m[r * 4 + 1];
            in.world.r[r].z = m[r * 4 + 2];
            in.world.r[r].w = m[r * 4 + 3];
        }
        in.x = m[3];
        in.y = m[7];
        const render::Anim& a = kinds_[kind].anim;
        float c[3], r2 = 0.0f, s2 = 1.0f;
        for (int i = 0; i < 3; ++i) {
            c[i] = a.posOffset()[i];
            r2 += 32767.0f * a.posScale()[i] * 32767.0f * a.posScale()[i];
            const float col2 = m[i] * m[i] + m[4 + i] * m[4 + i] + m[8 + i] * m[8 + i];
            if (col2 > s2) s2 = col2;
        }
        in.centre = Mtx_MultiplyFVec4(&in.world, FVec4_New(c[0], c[1], c[2], 1.0f));
        in.radius = std::sqrt(r2 * s2);
        in.kind = uint8_t(kind);
        in.track = uint8_t(track);
    }
    std::fclose(f);
    TT_LOG("animprops: zone %lu, %d props of %d kinds", static_cast<unsigned long>(zoneId), count_, numKinds_);
}

void AnimProps::draw(render::Renderer& renderer, float eyeX, float eyeY, uint32_t nowMs) {
    if (!count_) return;
    const float maxDist = render::activeQuality().drawDistance;
    // Nearest first, kMaxDrawn at most: a long Dreamland street holds 22 trash cans.
    int order[kMaxDrawn];
    float dist[kMaxDrawn];
    int n = 0;
    for (int i = 0; i < count_; ++i) {
        const float dx = instances_[i].x - eyeX, dy = instances_[i].y - eyeY;
        const float d2 = dx * dx + dy * dy;
        if (d2 > maxDist * maxDist || !renderer.inView(instances_[i].centre, instances_[i].radius)) continue;
        int at = n < kMaxDrawn ? n++ : kMaxDrawn;
        if (at == kMaxDrawn) {
            if (d2 >= dist[kMaxDrawn - 1]) continue;
            at = kMaxDrawn - 1;
        }
        while (at > 0 && dist[at - 1] > d2) {
            dist[at] = dist[at - 1];
            order[at] = order[at - 1];
            --at;
        }
        dist[at] = d2;
        order[at] = i;
    }
    const float t = float(nowMs % 3600000u) * 0.001f;
    u32 triangles = 0;
    for (int j = 0; j < n; ++j) {
        const Instance& in = instances_[order[j]];
        Kind& k = kinds_[in.kind];
        triangles += k.model.numTriangles();
        if (triangles > kTriangleBudget) break;
        const u32 frames = k.anim.numFrames();
        float pos; // in baked (12 fps) frames
        if (in.track == 1 || in.track == 2) {
            const float src = in.track == 1 ? trackFrame(kPeriscope, int(sizeof(kPeriscope) / sizeof(kPeriscope[0])), t)
                                            : trackFrame(kTelescope, int(sizeof(kTelescope) / sizeof(kTelescope[0])), t);
            pos = src * k.anim.fps() / kSourceFps;
            if (pos > float(frames - 1)) pos = float(frames - 1);
        } else {
            pos = std::fmod(t * k.anim.fps(), float(frames)); // Actor.loop('anim')
        }
        const u32 fa = u32(pos) % frames;
        const u32 fb = in.track ? (fa + 1 < frames ? fa + 1 : fa) : (fa + 1) % frames;
        renderer.drawMorph(k.model, k.anim, fa, fb, pos - std::floor(pos), in.world);
    }
}

} // namespace tt::world
