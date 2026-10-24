#include "world/toon_avatar.h"

#include <cstdio>

namespace tt::world {

const char* const kToonAnimNames[kAnimCount] = {"neutral", "walk", "run", "jump-idle", "wave"};

void ToonAvatar::play(ToonAnim anim, float rate) {
    if (anim != anim_ || extra_) {
        anim_ = anim;
        time_ = 0.0f;
    }
    extra_ = nullptr;
    extraIndex_ = -1;
    rate_ = rate;
}

void ToonAvatar::playClip(const ToonClipSet* set, int index, bool loop) {
    if (set != extra_ || index != extraIndex_) time_ = 0.0f;
    extra_ = set;
    extraIndex_ = index;
    extraLoop_ = loop;
    rate_ = 1.0f;
}

void ToonClipSet::unload() {
    for (auto& c : clip)
        for (auto& part : c) part.unload();
    count = 0;
    look = nullptr;
}

void ToonClipSet::load(const ToonLook& l, const char* const* names, int n) {
    unload();
    look = &l;
    const ToonPart* parts[3] = {l.legs, l.torso, l.head};
    for (int i = 0; i < n && i < kMax; i++) {
        for (int p = 0; p < 3; p++) {
            if (!parts[p]) continue;
            char path[112];
            snprintf(path, sizeof(path), "sdmc:/3ds/toontown3ds/data/toon/%s_%s.tta", parts[p]->key, names[i]);
            FILE* f = fopen(path, "rb"); // static heads have no clips: skip quietly
            if (!f) continue;
            fclose(f);
            clip[i][p].load(path);
        }
    }
    count = n < kMax ? n : kMax;
}

void ToonAvatar::draw(render::Renderer& renderer, const C3D_Mtx& world, const render::Tints* tintOverride) const {
    const ToonLook* look = look_ && look_->ready() ? look_ : fallback_;
    if (!look || !look->ready()) return;
    const render::Tints* tints = tintOverride ? tintOverride : &look->tints;
    const C3D_Tex* slotTex[render::kTexSlotCount];
    look->slotTextures(slotTex);
    C3D_Mtx partWorld = world;
    Mtx_Scale(&partWorld, look->scale, look->scale, look->scale);

    ToonPart* const parts[3] = {look->legs, look->torso, look->head};
    for (int p = 0; p < 3; p++) {
        ToonPart& part = *parts[p];
        const bool useExtra = extra_ && extra_->look == look && extraIndex_ >= 0 && extraIndex_ < extra_->count &&
                              extra_->clip[extraIndex_][p].loaded() &&
                              extra_->clip[extraIndex_][p].numVertices() == part.model.numVertices();
        const render::Anim& clip = useExtra ? extra_->clip[extraIndex_][p] : part.anim[anim_];
        float t = time_;
        if (useExtra && !extraLoop_) { // one-shot: hold the last frame
            const float last = (clip.numFrames() - 1) / clip.fps();
            if (t > last) t = last;
        }
        if (!clip.loaded()) {
            // Static part (non-dog heads have no clips upstream).
            renderer.drawModel(part.model, partWorld, tints, slotTex);
        } else {
            u32 a, b;
            float blend;
            render::sampleClip(clip, t, &a, &b, &blend);
            renderer.drawMorph(part.model, clip, a, b, blend, partWorld, tints, slotTex);
            if (clip.hasAttach()) {
                // Next part hangs off this part's attach joint (legs -> hips -> torso -> head).
                C3D_Mtx attach, next;
                clip.attach(a, b, blend, &attach);
                Mtx_Multiply(&next, &partWorld, &attach);
                partWorld = next;
            }
        }
        if (p == 2 && look->lashes && look->lashes->state == SlotState::Ready) {
            renderer.drawModel(look->lashes->model, partWorld, tints);
        }
    }
}

} // namespace tt::world
