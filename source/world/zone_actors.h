// Non-player objects in a zone, driven by tt::net::Client: NPC toons standing at their
// DNA origins, and treasures to grab. Positions of named anchors come from the zone's
// .pts sidecar (tools/pipeline/bake_zone.py write_points).
#pragma once

#include <3ds.h>
#include <citro3d.h>

#include "net/client.h"
#include "render/model.h"
#include "render/renderer.h"
#include "ui/balloons.h"
#include "world/toon_avatar.h"
#include "world/toon_cache.h"

namespace tt::world {

class ZoneActors {
public:
    static constexpr int kMaxPoints = 256; // streets: every door and tunnel anchor (up to 177 so far)

    bool init(ToonCache* cache, const char* pointsPath, const char* treasureModelPath);
    // New zone: reload anchor points (NPCs of the old zone are dropped by the client).
    void loadPoints(const char* pointsPath);
    void shutdown();
    void handleEvent(const net::Event& ev, const net::Client& client);
    void update(float dt);
    void draw(render::Renderer& renderer, const C3D_FVec& cameraEye);
    void addBalloons(ui::Balloons& balloons, const C3D_Mtx& proj, const C3D_Mtx& view) const;
    // What an NPC is saying (quest dialog), shown in its balloon; null clears.
    void setSpeech(u32 npcDoId, const char* text) {
        speechNpc_ = text ? npcDoId : 0;
        speech_ = text;
    }
    // Treasure the toon at (x, y, z) is touching, or 0 (DistributedTreasure sphere r = 2).
    u32 touchingTreasure(float x, float y, float z) const;
    // NPC of class `classId` whose collision tube (DistributedNPCToonBase.__initCollisions: tube
    // (0,1,0)-(0,1,5), radius 3.25, in its own frame) the toon's event sphere touches, else 0.
    u32 touchingNpc(float x, float y, float z, const net::Client& client, uint16_t classId) const;
    // nametagHeight < 0: the NPC's own (DistributedNPCClerk/Tailor use self.getHeight());
    // the pet, kart, fisherman and party clerks pass the local toon's.
    bool talkCamera(u32 npc, bool choice, C3D_FVec* eye, C3D_FVec* target, float nametagHeight = -1) const;
    int npcCount() const;
    int shadowPositions(float* xyzs, int maxOut) const; // NPC feet (x, y, z, scale)
    // Nearest live treasure (autotest helper). False when there is none.
    bool nearestTreasure(float x, float y, float* tx, float* ty, float* tz) const;

private:
    struct Point {
        char name[64];
        float x, y, z, h;
    };
    struct Npc {
        bool active = false;
        u32 doId = 0;
        char name[32] = {};
        float x = 0, y = 0, z = 0, h = 0;
        ToonLook look;
        ToonAvatar avatar;
    };
    struct Treasure {
        bool active = false;
        u32 doId = 0;
        float x = 0, y = 0, z = 0;
        float fade = 1.0f; // grab fade-out (treasureFlyTrack)
        bool grabbed = false;
    };
    const Point* findPoint(const char* name) const;

    ToonCache* cache_ = nullptr;
    Point points_[kMaxPoints];
    int numPoints_ = 0;
    Npc npcs_[net::kMaxNpcs];
    Treasure treasures_[net::kMaxTreasures];
    render::Model treasureModel_;
    float time_ = 0.0f;
    u32 speechNpc_ = 0;
    const char* speech_ = nullptr;
};

} // namespace tt::world
