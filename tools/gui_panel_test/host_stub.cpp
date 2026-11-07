// Definitions for the host citro stubs and the GagAtlas stub: one translation unit, linked
// into the test binary next to the compiled panel sources.
#include "3ds.h"
#include "citro2d.h"
#include "host_draw_recorder.h"
#include "world/battle_fx.h"

// Inactive-path partners of the compiled panels: the shop / clerk / closet / tailor /
// purchase / battle panels are linked and driven by the suites below, so their off-device
// partners (the net senders, audio volumes, the ToonCache) get inert host definitions instead
// of pulling in the whole game. Where a panel needs client state the datagram parser would
// produce, the seam is explicit and lives in this file (Client::connect plus the
// host::stage* globals): no definition invents behaviour, they only serve staged test data.
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

// ---- fake offline net session (see Client::connect() at the bottom of this file): the
// gui_panel_test.cpp suites fill these, call client.connect(), and the staged panel openers
// (ShopPanel::onMovie, ServiceClerkPanel::update, TailorPanel::onMovie, ClosetPanel::open,
// MinigamePurchasePanel::open) see the same client state the parser would produce. ----
uint32_t stageAvatarId = 100000101;
tt::net::OwnStats stageStats;
uint8_t stageOwnDna[tt::net::kDnaSize] = {};
tt::net::ShopMovie stageShopMovie;
tt::net::TailorMovie stageTailorMovie;
tt::net::Closet stageCloset;
tt::net::ClerkProfile stageClerkProfile;
tt::net::ClerkSession stageClerkSession;
tt::net::OwnFish stageOwnFish;
tt::net::OwnBookStats stageOwnBook;
tt::net::NpcToon stageNpc;
std::vector<tt::net::ClerkMovie> stageClerkMovies;
size_t clerkMovieHead = 0;
int stageFishTotal = 0;

void stageReset() {
    stageAvatarId = 100000101;
    stageStats = tt::net::OwnStats{};
    for (int i = 0; i < tt::net::kDnaSize; i++) stageOwnDna[i] = 0;
    stageShopMovie = tt::net::ShopMovie{};
    stageTailorMovie = tt::net::TailorMovie{};
    stageCloset = tt::net::Closet{};
    stageClerkProfile = tt::net::ClerkProfile{};
    stageClerkSession = tt::net::ClerkSession{};
    stageOwnFish = tt::net::OwnFish{};
    stageOwnBook = tt::net::OwnBookStats{};
    stageOwnBook.initDefaults();
    stageNpc = tt::net::NpcToon{};
    stageClerkMovies.clear();
    clerkMovieHead = 0;
    stageFishTotal = 0;
}
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
    // Faithful mirror of AvPropTargetCat[AvPropTarget[track]][level] as re-stated beside its
    // only compiled caller (battle_panel.cpp's gagDetail table): only the entry below matters
    // for the panel's usable()/BattleAction target rules. The old stub said "never group",
    // which grounded the gag picker's group targeting for no reason.
    static constexpr uint8_t kGroup[7][7] = {
        {0, 1, 0, 1, 0, 1, 1}, {0, 0, 0, 0, 0, 0, 1}, {0, 1, 0, 1, 0, 1, 1}, {1, 1, 1, 1, 1, 1, 1},
        {0, 0, 0, 0, 0, 0, 1}, {0, 0, 0, 0, 0, 0, 1}, {0, 0, 0, 0, 0, 0, 1}};
    if (track < 0 || track > 6 || level < 0 || level > 6) return false;
    return kGroup[track][level] != 0;
}
bool ToonCache::acquire(const ToonDna&, ToonLod, ToonLook* out) {
    if (out) *out = ToonLook{};
    return false;
}
} // namespace tt::world

namespace tt::net {
// The senders below report "queued" while the fake session (Client::connect) is up: the
// real client would put the datagram on the socket, and several panels (clerk sale
// results, the tailor's close-on-send) branch on that. Nothing is written anywhere;
// the queues the parser fills (pollClerkMovie) stay under the test's control.
bool Client::sendClosetDNA(uint32_t, const uint8_t*, int8_t, uint8_t) { return true; }
bool Client::sendClosetExit(uint32_t) { return true; }
bool Client::sendTailorDNA(uint32_t, const uint8_t*, int8_t, uint8_t) { return true; }
bool Client::sendPurchaseExit() { return true; }
bool Client::sendPetAdopt(uint8_t, uint32_t) { return true; }
bool Client::sendPetReturn() { return true; }
bool Client::sendServiceFishSale(bool) { return true; }
bool Client::sendClerkKartBody(uint8_t) { return true; }
bool Client::sendClerkKartAccessory(uint8_t) { return true; }
bool Client::sendServiceClerkDone() { return true; }
bool Client::sendClerkSetInventory(uint32_t, const uint8_t inv[7][7], int16_t, bool) {
    (void)inv;
    return true;
}
bool Client::pollClerkMovie(ClerkMovie& movie) {
    // Test seam: the staged movie queue (see host::stageClerkMovies) instead of a datagram
    // parser. Same drain semantics as the real client_clerks.cpp queue.
    if (host::clerkMovieHead >= host::stageClerkMovies.size()) return false;
    movie = host::stageClerkMovies[host::clerkMovieHead++];
    return true;
}
int Client::clerkFishTotal() const { return host::stageFishTotal; }
const NpcToon* Client::findNpc(uint32_t doId) const {
    // Test seam: the single staged NPC (host::stageNpc) is the one the panels get to see; the
    // panel code only checks non-null / classId, never what the datagram parser would fill.
    if (host::stageNpc.active && host::stageNpc.doId == doId) return &host::stageNpc;
    return nullptr;
}
} // namespace tt::net

namespace tt::net {
bool Client::connect() {
    // Test seam: with no sockets in the host suite, connect() installs a fake offline session
    // from the stage globals the test fills first (see gui_panel_test.cpp). All the fields
    // below are (private) members and this is a member function, exactly like the previous
    // inert send* stubs: no source outside tools/gui_panel_test/ changes.
    state_ = State::InZone;
    avatarId_ = host::stageAvatarId;
    ownStats_ = host::stageStats;
    for (int i = 0; i < kDnaSize; i++) ownDna_[i] = host::stageOwnDna[i];
    shopMovie_ = host::stageShopMovie;
    tailorMovie_ = host::stageTailorMovie;
    closet_ = host::stageCloset;
    clerkProfile_ = host::stageClerkProfile;
    clerkSession_ = host::stageClerkSession;
    ownFish_ = host::stageOwnFish;
    ownBook_ = host::stageOwnBook;
    npcs_[0] = host::stageNpc;
    shopMovie_.avId = host::stageAvatarId;
    tailorMovie_.avId = host::stageAvatarId;
    return true;
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
