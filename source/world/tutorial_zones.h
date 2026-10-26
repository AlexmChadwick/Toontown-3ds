// The Toontorial's zones (docs/design/tutorial.md section 2). Pure: tools/tutorial_test checks it.
//
// TutorialManagerAI.__createTutorial allocates four dynamic zones per player
// (ToontownAIRepository.allocateZone: DynamicZonesBegin 61000 .. DynamicZonesEnd 1 << 20) and sends
// them in TutorialManager.enterTutorial(branchZone, streetZone, shopZone, hqZone). Upstream maps
// them with ZoneUtil.overrideOn(branch, exteriorList=[street], interiorList=[shop, hq]); the town
// loader then renames the DNA visgroup "20001" to the dynamic street zone
// (TutorialTownLoader.alterDictionaries).
//
// This client keeps every place, transition and door in canonical (baked) zone ids and talks to
// the server in the dynamic ones:
//   street  -> 20001  phase_3.5/dna/tutorial_street.dna visgroup "20001", baked as zones/20000.tz
//   shop    -> 20502  Tutorial Tom's interior: block 2 (tb2:toon_landmark_TT_A1, TutorialManagerAI's
//                     "gagBlock"), interior zone = branch 20000 + 500 + block
//   hq      -> 20501  Toon HQ: block 1 (tb1:toon_landmark_hqTT)
//   branch  -> 20000  nothing generates there (TutorialManagerAI creates no object in it)
// Any other zone (Toontown Central after the tunnel) passes through unchanged.
#pragma once

#include <cstdint>

namespace tt::world {

constexpr uint32_t kTutorialBranch = 20000; // TTLocalizer.GlobalStreetNames[20000] "Tutorial Terrace"
constexpr uint32_t kTutorialStreet = 20001; // tutorial_street.dna: visgroup "20001"
constexpr uint32_t kTutorialHqBlock = 1;    // tb1:toon_landmark_hqTT_DNARoot
constexpr uint32_t kTutorialShopBlock = 2;  // tb2:toon_landmark_TT_A1_DNARoot
constexpr uint32_t kTutorialHq = kTutorialBranch + 500 + kTutorialHqBlock;     // 20501
constexpr uint32_t kTutorialShop = kTutorialBranch + 500 + kTutorialShopBlock; // 20502
// HoodMgr.addLinkTunnelHooks: in the tutorial the tunnel "linktunnel_tt_0" sends the toon to
// WelcomeValleyToken (Place.enterDFACallback, zoneId 22000), which getCanonicalZoneId maps to
// Toontown Central.
constexpr uint32_t kTutorialExitTunnelZone = 0;
constexpr uint32_t kToontownCentral = 2000;
// ToontownGlobals.DynamicZonesBegin / DynamicZonesEnd.
constexpr uint32_t kDynamicZonesBegin = 61000;
constexpr uint32_t kDynamicZonesEnd = 1u << 20;

struct TutorialZones {
    uint32_t branch = 0;
    uint32_t street = 0;
    uint32_t shop = 0;
    uint32_t hq = 0;

    bool active() const { return street != 0; }
    void clear() { branch = street = shop = hq = 0; }

    // All four are dynamic zones and distinct (anything else is not an enterTutorial we can use).
    static bool valid(uint32_t b, uint32_t s, uint32_t sh, uint32_t h) {
        const uint32_t z[4] = {b, s, sh, h};
        for (int i = 0; i < 4; i++) {
            if (z[i] < kDynamicZonesBegin || z[i] >= kDynamicZonesEnd) return false;
            for (int j = 0; j < i; j++) {
                if (z[i] == z[j]) return false;
            }
        }
        return true;
    }

    // Server zone -> baked zone.
    uint32_t toCanonical(uint32_t zone) const {
        if (!active() || zone == 0) return zone;
        if (zone == street) return kTutorialStreet;
        if (zone == shop) return kTutorialShop;
        if (zone == hq) return kTutorialHq;
        if (zone == branch) return kTutorialBranch;
        return zone;
    }

    // Baked zone -> server zone.
    uint32_t toNetwork(uint32_t zone) const {
        if (!active()) return zone;
        if (zone == kTutorialStreet) return street;
        if (zone == kTutorialShop) return shop;
        if (zone == kTutorialHq) return hq;
        if (zone == kTutorialBranch) return branch;
        return zone;
    }

    // A baked tutorial zone (20000-20999).
    static bool isTutorialPlace(uint32_t canonicalZone) {
        return canonicalZone >= kTutorialBranch && canonicalZone < kTutorialBranch + 1000;
    }
};

} // namespace tt::world
