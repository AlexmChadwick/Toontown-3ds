// Definitions for the host citro stubs and the GagAtlas stub: one translation unit, linked
// into the test binary next to the compiled panel sources.
#include "3ds.h"
#include "citro2d.h"
#include "host_draw_recorder.h"
#include "world/battle_fx.h"

// Inactive-path partners of the compiled panels: the battle / clerk / closet / tailor /
// purchase panels are linked (the Ui object owns them with the tab panels) but are never
// active in these tests, so their off-device partners (the net sender methods, audio
// volumes, the ToonCache) get inert host definitions instead of pulling in the whole game.
// Every definition matches the real declaration; none of them is on a tested path.
#include "core/memory.h"
#include "ui/clerk_rules.h"
#include "world/battle.h"
#include "world/place.h"
#include "world/toon_cache.h"
#include "world/tutorial_zones.h"
#include "audio/audio.h"
#include "net/client.h"

namespace host {
std::vector<HostRecord> gRecords;
HostScissor gScissor;
bool gRecordEnabled = true;
FontMetrics gFont;
std::string gFontName = "impress";
} // namespace host

u64 gHostNowMs = 0;

namespace tt::mem {
void linearLockInit() {}
void linearLock() {}
void linearUnlock() {}
} // namespace tt::mem

namespace tt::clerks {
bool generatePet(uint32_t seed, uint32_t safezone, PetOffer* output) {
    (void)seed;
    (void)safezone;
    if (output) *output = PetOffer{};
    return false;
}
const char* petName(uint32_t index) {
    (void)index;
    return "";
}
const float* petColor(int body, int color) {
    (void)body;
    (void)color;
    return nullptr;
}
bool canBuyKart(int body, int currentBody, uint32_t tickets) {
    (void)currentBody;
    (void)tickets;
    return body == 0;
}
bool canBuyAccessory(int accessory, int body, const int8_t owned[16], uint32_t tickets) {
    (void)accessory;
    (void)body;
    (void)owned;
    (void)tickets;
    return false;
}
} // namespace tt::clerks

namespace tt::world {
// placeKind mirrors world/place.cpp's mapping exactly (the tutorial street, Cog HQ
// courtyards, the %1000 splits). The real place.cpp pulls in the whole zone / collision /
// renderer stack for these tests, whose panels only need this pure id -> kind mapping.
PlaceKind placeKind(uint32_t zoneId) {
    if (zoneId == kTutorialBranch || zoneId == kTutorialStreet)
        return PlaceKind::Street;
    if (zoneId >= 10000 && zoneId < 14000) return PlaceKind::Playground;
    const uint32_t local = zoneId % 1000;
    if (local < 100) return PlaceKind::Playground;
    if (local >= 500) return PlaceKind::Interior;
    return PlaceKind::Street;
}
bool attackAffectsGroup(int track, int level) {
    (void)track;
    (void)level;
    return false;
}
bool ToonCache::acquire(const ToonDna&, ToonLod, ToonLook* out) {
    if (out) *out = ToonLook{};
    return false;
}
} // namespace tt::world

namespace tt::net {
bool Client::sendClosetDNA(uint32_t, const uint8_t*, int8_t, uint8_t) { return false; }
bool Client::sendClosetExit(uint32_t) { return false; }
bool Client::sendTailorDNA(uint32_t, const uint8_t*, int8_t, uint8_t) { return false; }
bool Client::sendPurchaseExit() { return false; }
bool Client::sendPetAdopt(uint8_t, uint32_t) { return false; }
bool Client::sendPetReturn() { return false; }
bool Client::sendServiceFishSale(bool) { return false; }
bool Client::sendClerkKartBody(uint8_t) { return false; }
bool Client::sendClerkKartAccessory(uint8_t) { return false; }
bool Client::sendServiceClerkDone() { return false; }
bool Client::sendClerkSetInventory(uint32_t, const uint8_t inv[7][7], int16_t, bool) {
    (void)inv;
    return false;
}
bool Client::pollClerkMovie(ClerkMovie& movie) {
    movie = ClerkMovie{};
    return false;
}
int Client::clerkFishTotal() const { return 0; }
const NpcToon* Client::findNpc(uint32_t doId) const {
    (void)doId;
    return nullptr;
}
} // namespace tt::net

namespace tt::audio {
// Audio is off-device in these tests; the Options page only writes the volumes.
void setMusicVolume(float) {}
void setMasterVolume(float) {}
} // namespace tt::audio

namespace tt::world {

GagAtlas& GagAtlas::instance() {
    static GagAtlas g;
    return g;
}
bool GagAtlas::init() {
    modelLoaded_ = true;
    return true;
}
void GagAtlas::shutdown() { modelLoaded_ = false; }
bool GagAtlas::loaded() const { return modelLoaded_; }
// The real atlas draws the icon quad from inventory_gui.ttm; on the host record it as a
// rectangle with the icon's cell as tag so bounds tests can find it.
void GagAtlas::drawIcon(int track, int level, float x, float y, float size, float depth, float alpha) {
    (void)alpha;
    HostRecord r;
    r.kind = HostRecord::Image;
    r.x = x;
    r.y = y;
    r.w = size;
    r.h = size;
    r.z = depth;
    char tag[48];
    snprintf(tag, sizeof tag, "gag-icon %d-%d", track, level);
    r.tag = tag;
    host::pushRecord(r);
}

} // namespace tt::world
