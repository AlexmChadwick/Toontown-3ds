// Swimming and underwater view in the playgrounds with deep water (DDPlayground, OZPlayground):
// below a depth the toon swims (Walk 'swimming': the 'swim' anim state, a splash, stroke sounds
// while moving); with the camera under the surface the hood fog turns underwater blue and the
// ambient water sound loops. Donald's Dock keeps its white fog above water.
#pragma once

#include <cstdint>

#include "audio/audio.h"

namespace tt::render {
class Renderer;
}
namespace tt::net {
class Client;
}

namespace tt::world {

class Water {
public:
    ~Water();
    // On every zone load. Only Donald's Dock (1000) and Acorn Acres (6000) have deep water.
    void setZone(uint32_t zoneId, render::Renderer& renderer);
    // Once per frame with the toon's feet and the camera height. Returns true while swimming.
    bool update(float toonX, float toonY, float toonZ, float cameraZ, bool moving, net::Client* net,
                render::Renderer& renderer);
    bool swimming() const { return toonSubmerged_; }
    uint32_t zone() const { return zoneId_; }

private:
    void unloadSounds();
    void applyFog(render::Renderer& renderer) const;

    uint32_t zoneId_ = 0;
    bool active_ = false;
    float toonDepth_ = 0.0f;    // the toon swims below this z
    float cameraDepth_ = 0.0f;  // the view is underwater below this z
    float splashZ_ = 0.0f;      // d_playSplashEffect height
    bool hasWhiteFog_ = false;  // DDHood.setWhiteFog above water; OZ has none
    float whiteFogColor_[3] = {};
    bool toonSubmerged_ = false;
    bool cameraSubmerged_ = false;
    audio::SfxId sfxStroke_ = audio::kInvalidSfx;
    audio::SfxId sfxUnderwater_ = audio::kInvalidSfx;
    audio::SfxId sfxSubmerge_ = audio::kInvalidSfx;
    audio::VoiceHandle strokeVoice_ = audio::kNoVoice;
    audio::VoiceHandle underwaterVoice_ = audio::kNoVoice;
};

} // namespace tt::world
