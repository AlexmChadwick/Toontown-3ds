#include "world/tutorial_char.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/log.h"
#include "render/renderer.h"

namespace tt::world {

namespace {

constexpr const char* kDir = "sdmc:/3ds/toontown3ds/data/chars/";
// The clips tutorial_mickey plays on the character.
constexpr const char* kClipNames[] = {"neutral", "run", "left-point-start", "left-point", "right-point-start",
                                      "right-point"};
constexpr float kDegToRad = 3.14159265f / 180.0f;

} // namespace

bool TutorialChar::ensure(bool female) {
    if (model_.loaded() && female == female_) return true;
    if (tried_ && female == female_) return false;
    unload();
    tried_ = true;
    female_ = female;
    const char* asset = female ? "minnie_800" : "mickey_800";
    char path[96];
    snprintf(path, sizeof(path), "%s%s.ttm", kDir, asset);
    if (!model_.load(path)) {
        TT_LOG("tutorial: no %s", path);
        return false;
    }
    for (int i = 0; i < kClips; ++i) {
        snprintf(path, sizeof(path), "%s%s_%s.tta", kDir, asset, kClipNames[i]);
        if (!clips_[i].load(path)) TT_LOG("tutorial: no %s", path);
    }
    TT_LOG("tutorial: %s loaded for the intro", name());
    return true;
}

void TutorialChar::unload() {
    model_.unload();
    for (render::Anim& c : clips_) c.unload();
    tried_ = false;
}

void TutorialChar::draw(render::Renderer& renderer, const TutorialFlow& flow) {
    if (!model_.loaded() || !flow.charShown()) return;
    const quest::ScriptXform& x = flow.xform(quest::ScriptVar::ClassicChar);
    C3D_Mtx m;
    Mtx_Identity(&m);
    Mtx_Translate(&m, x.pos[0], x.pos[1], x.pos[2], true);
    Mtx_RotateZ(&m, x.hpr[0] * kDegToRad, true);
    const TutorialFlow::ActorAnim& a = flow.charAnim();
    const render::Anim* clip = &clips_[0];
    for (int i = 0; i < kClips; ++i)
        if (!strcmp(a.name, kClipNames[i]) && clips_[i].loaded()) clip = &clips_[i];
    if (!clip->loaded() || clip->numVertices() != model_.numVertices()) {
        renderer.drawModel(model_, m);
        return;
    }
    // Actor.loop / play(rate): a negative rate plays backwards; a played clip holds its last frame.
    const float dur = clip->duration();
    float t = (flow.time() - a.start) * fabsf(a.rate);
    if (!a.loop && t > dur - 0.001f) t = dur - 0.001f;
    if (a.rate < 0.0f) t = (a.loop ? -t : dur - 0.001f - t);
    u32 fa, fb;
    float blend;
    render::sampleClip(*clip, t, &fa, &fb, &blend);
    if (!a.loop && fb < fa) fb = fa; // no wrap back to frame 0 at the end of a one-shot clip
    renderer.drawMorph(model_, *clip, fa, fb, blend, m);
}

} // namespace tt::world
