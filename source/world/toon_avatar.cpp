#include "world/toon_avatar.h"

#include <cstdio>

#include "core/log.h"

namespace tt::world {

const char* const kToonAnimNames[kAnimCount] = {"neutral", "walk", "run", "jump-idle", "wave"};

namespace {

const char* const kPartNames[ToonParts::kPartCount] = {"legs", "torso", "head"};

} // namespace

bool ToonParts::load(const char* dir, const char* legsCode, const char* torsoCode, const char* headCode,
                     const char* lod) {
    const char* codes[kPartCount] = {legsCode, torsoCode, headCode};
    char path[128];
    bool ok = true;
    for (int p = 0; p < kPartCount; p++) {
        snprintf(path, sizeof(path), "%s/%s_%s_%s.ttm", dir, kPartNames[p], codes[p], lod);
        ok = model[p].load(path) && ok;
        for (int a = 0; a < kAnimCount; a++) {
            snprintf(path, sizeof(path), "%s/%s_%s_%s_%s.tta", dir, kPartNames[p], codes[p], lod, kToonAnimNames[a]);
            ok = anim[p][a].load(path) && ok;
        }
    }
    if (ok) TT_LOG("toon: loaded %s/%s/%s (%lu KB linear)", legsCode, torsoCode, headCode,
                   static_cast<unsigned long>(linearBytes() / 1024));
    return ok;
}

void ToonParts::unload() {
    for (int p = 0; p < kPartCount; p++) {
        model[p].unload();
        for (auto& a : anim[p]) a.unload();
    }
}

u32 ToonParts::linearBytes() const {
    u32 total = 0;
    for (int p = 0; p < kPartCount; p++) {
        total += model[p].linearBytes();
        for (const auto& a : anim[p]) total += a.linearBytes();
    }
    return total;
}

void ToonAvatar::play(ToonAnim anim, float rate) {
    if (anim != anim_) {
        anim_ = anim;
        time_ = 0.0f;
    }
    rate_ = rate;
}

void ToonAvatar::draw(render::Renderer& renderer, const C3D_Mtx& world, const render::Tints* tints) const {
    if (!parts_) return;
    C3D_Mtx partWorld = world;
    for (int p = 0; p < ToonParts::kPartCount; p++) {
        const render::Anim& clip = parts_->anim[p][anim_];
        if (!clip.loaded()) {
            renderer.drawModel(parts_->model[p], partWorld, tints);
            continue;
        }
        u32 a, b;
        float blend;
        render::sampleClip(clip, time_, &a, &b, &blend);
        renderer.drawMorph(parts_->model[p], clip, a, b, blend, partWorld, tints);
        if (clip.hasAttach()) {
            // Next part hangs off this part's attach joint (legs -> hips -> torso -> head).
            C3D_Mtx attach, next;
            clip.attach(a, b, blend, &attach);
            Mtx_Multiply(&next, &partWorld, &attach);
            partWorld = next;
        }
    }
}

} // namespace tt::world
