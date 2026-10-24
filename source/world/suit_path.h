// Street suit paths: the DNA suit point/edge graph of one street branch (.tsp), the
// breadth-first path search the AI and every client share (DNAStorage::get_suit_path) and
// the leg list that turns a path into timed legs (SuitLegList). No 3DS headers: this file
// is built for the host by tools/suit_test, which checks it against the real libtoontown.
//
// Spec: docs/design/combat.md section 3 ("Suits on streets") and "Client implementation
// status"; file format: tools/pipeline/bake_suit_paths.py (TSP1).
#pragma once

#include <cstddef>
#include <cstdint>

namespace tt::world {

// DNASuitPoint::DNASuitPointType
enum SuitPointType : uint8_t { kStreetPoint, kFrontDoorPoint, kSideDoorPoint, kCogHqInPoint, kCogHqOutPoint };

// SuitLeg::Type (same numeric values as libtoontown's enum).
enum SuitLegType : uint8_t {
    kLegWalkFromStreet,
    kLegWalkToStreet,
    kLegWalk,
    kLegFromSky,
    kLegToSky,
    kLegFromSuitBuilding,
    kLegToSuitBuilding,
    kLegToToonBuilding,
    kLegFromCogHq,
    kLegToCogHq,
    kLegOff,
};

// ToontownGlobals.SuitWalkSpeed and suit/SuitTimings.py.
constexpr float kSuitWalkSpeed = 4.8f;
constexpr float kSuitFromSky = 6.5f;
constexpr float kSuitToSky = 6.5f;
constexpr float kSuitFromSuitBuilding = 2.0f;
constexpr float kSuitToSuitBuilding = 2.5f;
constexpr float kSuitToToonBuilding = 2.5f;
constexpr float kSuitVictoryDance = 9.08f;
// DistributedSuit.STAND_OUTSIDE_DOOR
constexpr float kSuitStandOutsideDoor = 2.5f;

// Longest path the search will return (max_length is a u16 on the wire; the AI uses 300).
constexpr int kMaxSuitPathPoints = 512;

struct SuitCell {
    uint16_t zone;
    uint8_t width, height;
    float x, y, z;
};

class SuitGraph {
public:
    ~SuitGraph() { unload(); }
    bool load(const char* path);                          // sdmc:/.../zones/<branch>.tsp
    bool loadFromMemory(const uint8_t* data, size_t size); // copies what it needs
    void unload();
    bool loaded() const { return numPoints_ != 0; }

    uint16_t branch() const { return branch_; }
    int numPoints() const { return numPoints_; }
    int numEdges() const { return numEdges_; }
    int numCells() const { return numCells_; }
    // DNA point index -> dense index (0..numPoints-1), or -1 when the street has no such point.
    int dense(uint16_t dnaIndex) const {
        return dnaIndex <= maxIndex_ && denseOf_[dnaIndex] != 0xFFFF ? int(denseOf_[dnaIndex]) : -1;
    }
    uint16_t dnaIndex(int d) const { return dnaIdx_[d]; }
    SuitPointType type(int d) const { return static_cast<SuitPointType>(type_[d]); }
    const float* pos(int d) const { return &pos_[3 * d]; }
    uint16_t block(int d) const { return block_[d]; }
    bool terminal(int d) const { return type_[d] == kFrontDoorPoint || type_[d] == kSideDoorPoint; }
    const SuitCell& cell(int i) const { return cells_[i]; }
    // Zone of the accepted edge a -> b (dense indices), or 0 when there is no such edge.
    uint16_t edgeZone(int a, int b) const;
    // Edges of point d in DNAStorage order: first/last+1 into edgeEnd()/edgeZoneAt().
    int edgeBegin(int d) const { return edgeFirst_[d]; }
    int edgeEnd(int d) const { return edgeFirst_[d + 1]; }
    int edgeTo(int e) const { return edgeTo_[e]; }

    // DNAStorage::get_suit_path_breadth_first for DNA point indices (the values of
    // DistributedSuit.setPathEndpoints go through sp.pointIndexes, i.e. they are DNA
    // indices). Writes dense indices to out and returns the point count, or 0 when no
    // path exists (or the point/pool limits were hit).
    int genPath(uint16_t startDna, uint16_t endDna, int minLen, int maxLen, uint16_t* out, int cap) const;

private:
    uint16_t branch_ = 0;
    int numPoints_ = 0, numEdges_ = 0, numCells_ = 0;
    uint16_t maxIndex_ = 0;
    uint16_t* denseOf_ = nullptr;  // maxIndex_ + 1 entries, 0xFFFF = none
    uint16_t* dnaIdx_ = nullptr;
    uint8_t* type_ = nullptr;
    float* pos_ = nullptr;         // x y z per point
    uint16_t* block_ = nullptr;
    uint16_t* edgeFirst_ = nullptr; // numPoints_ + 1 (CSR)
    uint16_t* edgeTo_ = nullptr;    // dense end point, grouped by start point, file order within
    uint16_t* edgeZone_ = nullptr;
    SuitCell* cells_ = nullptr;
};

struct SuitLeg {
    uint8_t type;       // SuitLegType
    uint16_t zone;
    uint16_t block;     // CogHQ door legs: the landmark block
    uint16_t a, b;      // dense point indices (SuitLeg point A / B)
    float startTime;    // seconds since the start of the path
    float legTime;
};

// SuitLegList(path, dnaStore, walkSpeed, fromSky, toSky, fromSuitBuilding, toSuitBuilding,
// toToonBuilding) with the SuitTimings constants.
class SuitLegList {
public:
    ~SuitLegList() { clear(); }
    SuitLegList() = default;
    SuitLegList(const SuitLegList&) = delete;
    SuitLegList& operator=(const SuitLegList&) = delete;

    // path: dense point indices from SuitGraph::genPath (at least 2 points).
    bool build(const SuitGraph& graph, const uint16_t* path, int count, float walkSpeed = kSuitWalkSpeed);
    void clear();
    int numLegs() const { return numLegs_; }
    const SuitLeg& leg(int i) const { return legs_[i]; }
    float startTime(int i) const { return legs_[i].startTime; }
    // SuitLegList.getLegIndexAtTime(time, start)
    int legIndexAtTime(float time, int start) const;
    float duration() const { return numLegs_ ? legs_[numLegs_ - 1].startTime : 0.0f; }

private:
    SuitLeg* legs_ = nullptr;
    int numLegs_ = 0;
};

// Where a suit is and what it is doing `t` seconds into `leg`: the DistributedSuit.enter<Leg>
// states (positions, headings, fly-in/out heights) without the animation tracks.
enum SuitAnim : uint8_t { kSuitNeutral, kSuitWalk, kSuitLanding };
struct SuitPose {
    float x, y, z;
    float groundZ;    // z without the fly-in/out height (where the shadow goes)
    float h;          // degrees, Panda heading (DistributedSuit.calculateHeading)
    float alpha;      // fly-in fades in over 1 s, fly-out fades out over the last 1 s
    uint8_t anim;     // SuitAnim
    bool visible;     // false for FromCogHQ and the Off leg
};
void samplePose(const SuitGraph& graph, const SuitLeg& leg, float t, SuitPose* out);

// DistributedSuit.enterFlyAway / the end of DanceThenFlyAway: the cog stands at (x, y, z) facing
// h, waits for the landing anim to run backwards, then climbs SuitTimings.toSky *
// SuitWalkSpeed over timeTillLanding while fading out. t < 0: still standing (dancing).
void flyAwayPose(float x, float y, float z, float h, float t, SuitPose* out);

// DistributedSuit.calculateHeading(a, b), degrees.
float suitHeading(const float* a, const float* b);

} // namespace tt::world
