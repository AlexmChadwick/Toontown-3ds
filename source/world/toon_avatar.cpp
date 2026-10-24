#include "world/toon_avatar.h"

namespace tt::world {

const char* const kToonAnimNames[kAnimCount] = {"neutral", "walk", "run", "jump-idle", "wave"};

void ToonAvatar::play(ToonAnim anim, float rate) {
    if (anim != anim_) {
        anim_ = anim;
        time_ = 0.0f;
    }
    rate_ = rate;
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
        const render::Anim& clip = part.anim[anim_];
        if (!clip.loaded()) {
            // Static part (non-dog heads have no clips upstream).
            renderer.drawModel(part.model, partWorld, tints, slotTex);
        } else {
            u32 a, b;
            float blend;
            render::sampleClip(clip, time_, &a, &b, &blend);
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
