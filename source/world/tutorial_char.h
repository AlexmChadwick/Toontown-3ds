// The classic character of tutorial_mickey (Mickey, or Minnie for a girl Toon) in Tutorial Tom's
// room: the baked character model and its clips, posed and animated from world/tutorial.*
// (QuestParser LOAD_CLASSIC_CHAR / LOOP_ANIM / PLAY_ANIM / LERP_POS on classicChar).
#pragma once

#include <3ds.h>
#include <citro3d.h>

#include "render/anim.h"
#include "render/model.h"
#include "world/tutorial.h"

namespace tt::render { class Renderer; }

namespace tt::world {

class TutorialChar {
public:
    // Loads the character on first use (the files of the playground's DistributedMickey /
    // DistributedMinnie); false when they are missing.
    bool ensure(bool female);
    void unload();
    void draw(render::Renderer& renderer, const TutorialFlow& flow);
    bool loaded() const { return model_.loaded(); }
    const char* name() const { return female_ ? "Minnie" : "Mickey"; }
    static constexpr float kHeight = 3.0f; // CharInfo height of both (nametag above it)

private:
    static constexpr int kClips = 6;
    render::Model model_;
    render::Anim clips_[kClips];
    bool female_ = false;
    bool tried_ = false;
};

} // namespace tt::world
