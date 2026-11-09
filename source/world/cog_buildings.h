#pragma once

#include <3ds.h>

#include "net/client.h"
#include "render/model.h"
#include "render/renderer.h"

namespace tt::world {
class Place;

class CogBuildings {
public:
    void setZone(uint32_t zone);
    // Also registers the zone draw hook that hides Cog-owned Toon blocks.
    void syncAnchors(Place& place);
    void handleEvent(const net::Event& ev, const net::Client& client);
    void update(net::Client& client, float x, float y, float z, uint32_t nowMs);
    void draw(render::Renderer& renderer, const net::Client& client) const;
    void setAnchor(uint32_t block, float x, float y, float z, float h);
    void shutdown();
    const char* popMessage();
    // Autotest: the boarding point of a Cog-owned building's elevator in this zone (the DNA
    // anchor the contact test uses); false if none.
    bool suitElevatorPoint(const net::Client& client, float* x, float* y, float* z, float* h) const;
    // The elevator anchor of building block `block` (false when this street has none).
    bool blockPoint(uint32_t block, float* x, float* y, float* z, float* h) const;
    // True while the Cogs own `block` on this street (its Toon building and door are gone).
    bool blockOwned(const net::Client& client, uint32_t block) const;
    // Where the elevator we rode is taking us (a Cog building interior or a factory / mint /
    // DA office zone), once; 0 when not leaving. The caller changes zone.
    uint32_t popDestination() {
        const uint32_t d = pendingDest_;
        pendingDest_ = 0;
        return d;
    }

private:
    struct Anchor { uint32_t block = 0; float x = 0, y = 0, z = 0, h = 0; bool used = false; };
    static constexpr int kMaxAnchors = 64;   // buildings with an anchor in one zone (streets have ~40)
    static constexpr uint32_t kMaxBlock = 128; // DNA block numbers searched for anchors
    Anchor anchors_[kMaxAnchors] = {};

    // ---- street look of a Cog building (tools/pipeline/bake_cog_exterior.py) ----
    // <zone>.tsb: per takeable block the suit landmark origin, the Cog flats and the boxes
    // that hide the Toon block; data/cogbldg holds the shared models.
    struct Box { float rows[12]; float lo[3], hi[3]; };
    struct Flat { float rows[12]; uint8_t wall, door; };
    struct Block {
        uint16_t block = 0, signBatch = 0;
        uint16_t firstFlat = 0, firstBox = 0;
        uint16_t numFlats = 0, numHide = 0, numKeep = 0;
        float landmark[12] = {};
        float lo[3] = {}, hi[3] = {};
    };
    static constexpr int kDepts = 4;   // c l m s
    static constexpr int kFieldOffice = kDepts; // a Cogdominium's index in the per-block signs
    static constexpr int kWalls = 10;  // storage_town.dna suit_wall codes
    static constexpr int kMaxHidden = 16;
    void loadExterior(uint32_t zone);
    void unloadExterior();
    const Block* findBlock(uint32_t block) const;
    void elevatorMatrix(const Block& b, int dept, float out[12]) const;
    void ensureModels(int dept, const Block& b);
    void drawSuit(render::Renderer& renderer, const net::Client& client, const net::CogBuilding& b,
                  const Block& bk) const;
    void drawCogdo(render::Renderer& renderer, const Block& bk) const;
    static int deptIndex(int8_t track);
    static void zoneDrawHook(void* user, render::Model& model, bool far);
    static bool hideTest(void* user, const float* a, const float* b, const float* c);
    Block* blocks_ = nullptr;
    Flat* flats_ = nullptr;
    Box* boxes_ = nullptr;
    int numBlocks_ = 0;
    bool exteriorOk_ = false;            // suit_exterior.bin read
    float signOrigin_[kDepts][12] = {};
    float doorOrigin_[kDepts][12] = {};
    render::Model landmarks_[kDepts], icons_[kDepts], walls_[kWalls], cogdoWalls_[kWalls];
    render::Model suitDoor_, signs_, cab_, leftDoor_, rightDoor_, lights_[5];
    uint32_t triedModels_ = 0;           // bits: a load was attempted (not retried)
    uint32_t cogdoTried_ = 0;            // the same for cogdoWalls_
    const Block* hidden_[kMaxHidden] = {};
    int hiddenDept_[kMaxHidden] = {};
    int numHidden_ = 0;
    uint32_t hideRevision_ = 0;
    float hideLo_[3] = {}, hideHi_[3] = {};
    bool hideFar_ = false;               // the model being tested is a far mesh
    // ---- end street look ----
    // data/cogdo: the field office with elevatorB (bake_cogdo.py "shell", suit-origin space)
    // and its door in that space (points.bin).
    render::Model cogdoShell_;
    float cogdoDoor_[3] = {15.463f, 0, 0};
    // ---- Cog HQ elevators (tools/pipeline/bake_hq_elevators.py) ----
    // Upstream's setupElevator loads these at run time (the zone bake holds only the HQ
    // model): <zone>.thq places them, data/cogbldg/hqelev_<type>_* are frame and doors.
    struct HqElevator {
        uint8_t type = 0, sign = 255;
        int8_t origin = -1;
        float rows[12] = {};
        float width = 0, openTime = 1, closeTime = 1;
    };
    static constexpr int kHqTypes = 6;     // factory mint vp cfo cj bb
    static constexpr int kMaxHq = 8;
    void loadHqElevators(uint32_t zone);
    void drawHqElevators(render::Renderer& renderer, const net::Client& client) const;
    HqElevator hq_[kMaxHq] = {};
    int numHq_ = 0;
    render::Model hqFrame_[kHqTypes], hqLeft_[kHqTypes], hqRight_[kHqTypes], hqSigns_;
    // ---- end Cog HQ elevators ----
    render::Model kart_; // DistributedCogKart (Coggolf_cart3)
    uint32_t zone_ = 0;
    uint32_t requestedElevator_ = 0;
    // Cog HQ facility elevators: elevator_origin_<n> anchors of the HQ zone (.pts).
    struct Spot { float x = 0, y = 0, z = 0; bool used = false; };
    static constexpr int kMaxSpots = 8;
    Spot spots_[kMaxSpots] = {};
    uint32_t pendingDest_ = 0, lastDest_ = 0;
    uint32_t requestMs_ = 0;
    const char* message_ = nullptr;
};

} // namespace tt::world
