#pragma once
#include "render/model.h"
#include "render/renderer.h"
#include "world/asset_loader.h"
#include <cstdint>

namespace tt::world {
// The town of the urban race tracks (DistributedRace.loadUrbanTrack): City Circuit and
// Blizzard Boulevard load urban_track_town(_B).dna over the track. bake_race_town.py writes
// one model per stock building group plus the TRT1 index; showBuildings' window (five groups
// per side around the kart, Blizzard Boulevard's side streets near it) is streamed through the
// shared loader, one group further out on each side preloaded. The polySurface8 skyline
// (`<track>_bg.ttm`) follows the kart at world z -1, drawn without depth after the sky.
class RaceTown {
  public:
    static constexpr int kMaxGroups = 160;
    static constexpr int kSlots = 22; // 7 inner + 7 outer + 8 side streets
    // Track id as RaceGlobals; not urban -> nothing loaded.
    void load(uint16_t track, const char *trackName);
    void unload();
    bool urban() const { return groups_ > 0; }
    // Kart position and its curve index fraction (getNearestT / maxT) on the race's curve.
    void update(float x, float y, float t);
    void pump(); // after the GPU fence (Racing::pumpAssets)
    void drawSkyline(render::Renderer &renderer, float x, float y);
    void draw(render::Renderer &renderer);

  private:
    struct Group {
        float c[3], r;
        uint8_t side;
    };
    struct Slot {
        render::Model model;
        int group = -1;   // whose mesh is in `model`
        int want = -1;    // group to load next (-1: none)
        LoadTicket ticket = kNoTicket;
        bool drop = false; // unload `model` at the next pump
    };
    bool wanted(int g) const;
    bool shown(int g) const;
    Group group_[kMaxGroups];
    int first_[4] = {}, count_[4] = {};
    int groups_ = 0;
    bool reversed_ = false, nearestWalk_ = false;
    int cur_[2] = {-1, -1};
    int want_[kSlots] = {}, numWant_ = 0, numShown_ = 0;
    bool failed_[kMaxGroups] = {};
    char key_ = 0;
    Slot slots_[kSlots];
    render::Model skyline_;
};
} // namespace tt::world
