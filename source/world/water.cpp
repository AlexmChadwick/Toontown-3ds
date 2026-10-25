#include "world/water.h"

#include <initializer_list>

#include "core/log.h"
#include "net/client.h"
#include "render/renderer.h"

namespace tt::world {

namespace {

constexpr const char* kSfxStroke = "sdmc:/3ds/toontown3ds/data/audio/swim_stroke.tsnd";
constexpr const char* kSfxUnderwater = "sdmc:/3ds/toontown3ds/data/audio/underwater.tsnd";
constexpr const char* kSfxSubmerge = "sdmc:/3ds/toontown3ds/data/audio/submerge.tsnd";

// DDHood / OZHood.setUnderwaterFog: Vec4(0, 0, 0.6, 1), setLinearRange(0.1, 100).
constexpr float kUnderwaterFog[3] = {0.0f, 0.0f, 0.6f};
constexpr float kUnderwaterFogStart = 0.1f;
constexpr float kUnderwaterFogEnd = 100.0f;
// DDHood.setWhiteFog: Vec4(0.8, 0.8, 0.8, 1), setLinearRange(0, 400).
constexpr float kWhiteFogStart = 0.0f;
constexpr float kWhiteFogEnd = 400.0f;

} // namespace

Water::~Water() {
    unloadSounds();
}

void Water::unloadSounds() {
    audio::stopVoice(strokeVoice_);
    audio::stopVoice(underwaterVoice_);
    strokeVoice_ = underwaterVoice_ = audio::kNoVoice;
    for (audio::SfxId* id : {&sfxStroke_, &sfxUnderwater_, &sfxSubmerge_}) {
        if (*id != audio::kInvalidSfx) audio::freeSfx(*id);
        *id = audio::kInvalidSfx;
    }
}

void Water::applyFog(render::Renderer& renderer) const {
    if (cameraSubmerged_) {
        renderer.setFog(kUnderwaterFog[0], kUnderwaterFog[1], kUnderwaterFog[2], kUnderwaterFogStart,
                        kUnderwaterFogEnd);
    } else if (hasWhiteFog_) {
        renderer.setFog(whiteFogColor_[0], whiteFogColor_[1], whiteFogColor_[2], kWhiteFogStart, kWhiteFogEnd);
    } else {
        renderer.clearFog();
    }
}

void Water::setZone(uint32_t zoneId, render::Renderer& renderer) {
    zoneId_ = zoneId;
    toonSubmerged_ = cameraSubmerged_ = false;
    audio::stopVoice(strokeVoice_);
    audio::stopVoice(underwaterVoice_);
    strokeVoice_ = underwaterVoice_ = audio::kNoVoice;
    if (zoneId == 1000) {
        // DDPlayground: __checkToonUnderwater (z < -2.3314585), __checkCameraUnderwater (z < 1),
        // splash at z 1.675.
        active_ = true;
        toonDepth_ = -2.3314585f;
        cameraDepth_ = 1.0f;
        splashZ_ = 1.675f;
        hasWhiteFog_ = true;
        whiteFogColor_[0] = whiteFogColor_[1] = whiteFogColor_[2] = 0.8f;
    } else if (zoneId == 6000) {
        // OZPlayground: waterLevel -0.53, toon below -4.0, no fog above water.
        active_ = true;
        toonDepth_ = -4.0f;
        cameraDepth_ = -0.53f;
        splashZ_ = -0.53f;
        hasWhiteFog_ = false;
    } else {
        active_ = false;
        hasWhiteFog_ = false;
    }
    if (active_ && sfxStroke_ == audio::kInvalidSfx) {
        sfxStroke_ = audio::loadSfx(kSfxStroke);
        sfxUnderwater_ = audio::loadSfx(kSfxUnderwater);
        sfxSubmerge_ = audio::loadSfx(kSfxSubmerge);
    } else if (!active_) {
        unloadSounds();
    }
    applyFog(renderer);
}

bool Water::update(float toonX, float toonY, float toonZ, float cameraZ, bool moving, net::Client* net,
                   render::Renderer& renderer) {
    if (!active_) return false;
    const bool toonUnder = toonZ < toonDepth_;
    if (toonUnder != toonSubmerged_) {
        toonSubmerged_ = toonUnder;
        if (toonUnder) {
            // __submergeToon: the splash sound, 'swimming', and the splash for everyone.
            audio::playSfx(sfxSubmerge_);
            if (net) net->sendSplashEffect(toonX, toonY, splashZ_);
            TT_LOG("water: swimming at %.1f,%.1f,%.2f", double(toonX), double(toonY), double(toonZ));
        } else {
            TT_LOG("water: back on land");
        }
    }
    const bool cameraUnder = cameraZ < cameraDepth_;
    if (cameraUnder != cameraSubmerged_) {
        cameraSubmerged_ = cameraUnder;
        // __submergeCamera / __emergeCamera: underwater fog and the looping water ambience.
        audio::stopVoice(underwaterVoice_);
        underwaterVoice_ = cameraUnder ? audio::playSfx(sfxUnderwater_, 0.8f, 0.0f, true) : audio::kNoVoice;
        applyFog(renderer);
    }
    // Walk.__swim: the stroke loop plays while moving, and only with the camera underwater
    // (setSwimSoundAudible).
    const bool stroke = toonSubmerged_ && cameraSubmerged_ && moving;
    if (stroke && strokeVoice_ == audio::kNoVoice) {
        strokeVoice_ = audio::playSfx(sfxStroke_, 1.0f, 0.0f, true);
    } else if (!stroke && strokeVoice_ != audio::kNoVoice) {
        audio::stopVoice(strokeVoice_);
        strokeVoice_ = audio::kNoVoice;
    }
    return toonSubmerged_;
}

} // namespace tt::world
