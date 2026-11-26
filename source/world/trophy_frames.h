// DistributedToonInterior.buildTrophy: in a Toon building that was won back from the Cogs, one
// trophy_frame per Toon in setSavedBy over the interior's trophy_origin, each holding that Toon's
// head (a ToonHead squashed flat: scale 0.55, 0.02, 0.55) and its name in black ToonFont below.
// The heads come from the shared ToonCache (head part only); the names are drawn in the
// top-screen 2D pass, projected onto the frame like the HQ leader board text.
#pragma once

#include <citro2d.h>
#include <cstdint>

#include "render/model.h"
#include "render/renderer.h"
#include "world/toon_avatar.h"
#include "world/toon_cache.h"
#include "world/toon_dna.h"

namespace tt::net {
class Client;
}

namespace tt::world {

class TrophyFrames {
public:
    bool init(ToonCache* cache);
    void shutdown();
    // Every frame: follows the interior object's savedBy list. originFound is false outside an
    // interior with a trophy_origin (nothing is drawn, the heads are released).
    void update(const net::Client& client, bool originFound, float ox, float oy, float oz, float oh);
    void draw(render::Renderer& renderer);
    void draw2D(const C3D_Mtx& proj, const C3D_Mtx& view);

private:
    static constexpr int kMax = 8;
    void clear();
    ToonCache* cache_ = nullptr;
    render::Model frame_;
    bool frameTried_ = false;
    uint32_t doId_ = 0, version_ = 0;
    int count_ = 0;
    float origin_[4] = {}; // x, y, z, h
    struct Entry {
        ToonLook look;
        ToonAvatar avatar;
        bool hasLook = false;
        Species species = Species::Dog; // buildFrame lowers rabbits, raises horses, shrinks mice
        C2D_Text name = {};
    } entries_[kMax];
    C2D_TextBuf buf_ = nullptr;
};

} // namespace tt::world
