#include "render/ui_font.h"
#include "world/interior_life.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/log.h"
#include "ui/projection.h"
#include "world/collision.h"

namespace tt::world {

namespace {

constexpr float kDegToRad = 3.14159265f / 180.0f;

// --- Toon HQ leaderboard (DistributedHQInterior.buildLeaderBoard) ---
// empty_board in phase_3.5/models/modules/HQ_interior, in interior space.
constexpr float kBoardAnchorX = -47.4427f;
constexpr float kBoardAnchorY = 29.6598f;
constexpr float kBoardAnchorZ = 8.15647f;
constexpr int kNumLeaders = 10;                   // DistributedHQInterior.numLeaders
constexpr float kNamePathX = -4.0f;               // TTLocalizer.DHQInamePathPos
constexpr float kScorePathX = -4.6f;              // TTLocalizer.DHQIscorePathPos
constexpr float kStarX = -6.6f;                   // buildTrophyStar: setPos(-6.6, 0, 0.3)
constexpr float kStarZ = 0.3f;
constexpr u32 kTitleColor = 0xFFB3C080;           // setTextColor(0.5, 0.75, 0.7, 1), ABGR
constexpr u32 kNameColor = 0xB3FFFFFF;            // setTextColor(1, 1, 1, 0.7)
constexpr u32 kScoreColor = 0xB31AFFFF;           // setTextColor(1, 1, 0.1, 0.7)
constexpr float kFontLinePx = 30.0f;              // citro2d system font line height at scale 1

// ToontownGlobals.TrophyStarLevels / TrophyStarColors; DistributedHQInterior.updateTrophyStar
// spins the star from the second level of each colour.
constexpr uint32_t kStarLevels[6] = {10, 20, 30, 50, 75, 100};

// --- Pet shop fish (DistributedPetshopInterior.setup) ---
constexpr float kFishX = 0.0f;
constexpr float kFishY = 6.0f;
constexpr float kFishZ = -4.0f;
constexpr float kFishScale = 0.8f;
constexpr float kFishPlayRate = 0.7f;
constexpr const char* kFishModel = "sdmc:/3ds/toontown3ds/data/interiors/props/interiorfish.ttm";
constexpr const char* kFishAnim = "sdmc:/3ds/toontown3ds/data/interiors/props/interiorfish_swim.tta";
constexpr const char* kStarModel = "sdmc:/3ds/toontown3ds/data/interiors/props/name_star.ttm";

// --- Knock-knock (DistributedKnockKnockDoor.knockKnockTrack, enterPlaying: duration 8) ---
// d = duration * 0.125 = 1 s per beat. The rimshot is baked from its 2.0 s startTime.
constexpr float kBeatSeconds = 1.0f;
constexpr float kJokeSeconds = 8.0f;
constexpr const char* kSfxKnock = "sdmc:/3ds/toontown3ds/data/audio/gui_knock.tsnd";
constexpr const char* kSfxRimshot = "sdmc:/3ds/toontown3ds/data/audio/tell_joke.tsnd";
constexpr const char* kDoorKnockKnock = "Knock, knock.";   // TTLocalizer.DoorKnockKnock
constexpr const char* kDoorWhosThere = "Who's there?";     // TTLocalizer.DoorWhosThere
constexpr const char* kDoorWhoAppendix = " who?";          // TTLocalizer.DoorWhoAppendix
constexpr const char* kDoorNametag = "Door";               // TTLocalizer.DoorNametag

void getBoardMtx(C3D_Mtx* out) {
    // leaderBoard.setPosHprScale(0.1, 0, 4.5, 90, 0, 0, 0.9, 0.9, 0.9) under empty_board.
    Mtx_Identity(out);
    Mtx_Translate(out, kBoardAnchorX, kBoardAnchorY, kBoardAnchorZ, false);
    Mtx_Translate(out, 0.1f, 0.0f, 4.5f, false);
    Mtx_RotateZ(out, 90.0f * kDegToRad, false);
    Mtx_Scale(out, 0.9f, 0.9f, 0.9f);
}

void boardLocalToWorld(const C3D_Mtx& m, float lx, float ly, float lz, float* wx, float* wy, float* wz) {
    *wx = m.r[0].x * lx + m.r[1].x * ly + m.r[2].x * lz + m.r[3].x;
    *wy = m.r[0].y * lx + m.r[1].y * ly + m.r[2].y * lz + m.r[3].y;
    *wz = m.r[0].z * lx + m.r[1].z * ly + m.r[2].z * lz + m.r[3].z;
}

// Projects a board-local point; *scale is the text scale for one board line (TextNode line
// height 1, times the board's 0.9) at that depth.
bool projectBoardText(const C3D_Mtx& proj, const C3D_Mtx& view, const C3D_Mtx& board, float lx, float lz,
                      float* sx, float* sy, float* scale) {
    float wx, wy, wz, depth;
    boardLocalToWorld(board, lx, 0.0f, lz, &wx, &wy, &wz);
    if (!ui::projectToScreen(proj, view, wx, wy, wz, sx, sy, &depth) || depth <= 0.0f) return false;
    float tx, ty, tz, tsx, tsy, tdepth;
    boardLocalToWorld(board, lx, 0.0f, lz + 1.0f, &tx, &ty, &tz);
    if (!ui::projectToScreen(proj, view, tx, ty, tz, &tsx, &tsy, &tdepth)) return false;
    *scale = std::fabs(*sy - tsy) / kFontLinePx;
    return *scale > 0.05f;
}

} // namespace

InteriorLife::~InteriorLife() {
    shutdown();
}

bool InteriorLife::init() {
    textBuf_ = C2D_TextBufNew(1024);
    return textBuf_ != nullptr;
}

void InteriorLife::shutdown() {
    setHqResident(false);
    setPetshopResident(false);
    if (textBuf_) {
        C2D_TextBufDelete(textBuf_);
        textBuf_ = nullptr;
    }
    if (sfxKnock_ != audio::kInvalidSfx) {
        audio::freeSfx(sfxKnock_);
        sfxKnock_ = audio::kInvalidSfx;
    }
    if (sfxRimshot_ != audio::kInvalidSfx) {
        audio::freeSfx(sfxRimshot_);
        sfxRimshot_ = audio::kInvalidSfx;
    }
}

void InteriorLife::setHqResident(bool resident) {
    if (resident && !starModel_.loaded()) {
        starModel_.load(kStarModel);
    } else if (!resident && starModel_.loaded()) {
        C3D_FrameSync(); // the GPU may still read last frame's star
        starModel_.unload();
    }
}

void InteriorLife::setPetshopResident(bool resident) {
    if (resident && !fishModel_.loaded()) {
        fishModel_.load(kFishModel);
        fishAnim_.load(kFishAnim);
        fishTime_ = 0.0f;
    } else if (!resident && fishModel_.loaded()) {
        C3D_FrameSync();
        fishModel_.unload();
        fishAnim_.unload();
    }
}

void InteriorLife::setZone(uint32_t zoneId) {
    zoneId_ = zoneId;
    joke_ = Joke{};
    insideSphere_ = -1;
    requestPending_ = false;
    requestedDoId_ = 0;
    loadKnockSpheres(zoneId);
    if (numSpheres_ > 0 && sfxKnock_ == audio::kInvalidSfx) {
        sfxKnock_ = audio::loadSfx(kSfxKnock);
        sfxRimshot_ = audio::loadSfx(kSfxRimshot);
    }
}

void InteriorLife::loadKnockSpheres(uint32_t zoneId) {
    numSpheres_ = 0;
    // Spheres are per street (branch zone); interiors and playgrounds have none.
    const uint32_t local = zoneId % 1000;
    if (zoneId >= 10000 || local < 100 || local >= 500) return;
    char path[96];
    std::snprintf(path, sizeof(path), "sdmc:/3ds/toontown3ds/data/zones/%u.kkd",
                  static_cast<unsigned>(zoneId - zoneId % 100));
    FILE* f = std::fopen(path, "r");
    if (!f) {
        TT_LOG("interior_life: no knock-knock spheres (%s)", path);
        return;
    }
    char line[96];
    if (!std::fgets(line, sizeof(line), f) || std::strncmp(line, "KKD1", 4)) {
        TT_LOG("interior_life: %s is not a KKD1 file", path);
        std::fclose(f);
        return;
    }
    while (numSpheres_ < kMaxKnockSpheres && std::fgets(line, sizeof(line), f)) {
        unsigned propId = 0;
        KnockSphere s;
        if (std::sscanf(line, "%u %f %f %f %f", &propId, &s.x, &s.y, &s.z, &s.radius) == 5) {
            s.propId = static_cast<uint16_t>(propId);
            spheres_[numSpheres_++] = s;
        }
    }
    std::fclose(f);
    TT_LOG("interior_life: zone %u has %d knock-knock doors", static_cast<unsigned>(zoneId), numSpheres_);
}

void InteriorLife::updateLeaderboardText(const net::Client::HQLeaderboardState& hq) {
    if (!textBuf_) return;
    C2D_TextBufClear(textBuf_);
    tt::render::textParse(&titleText_, textBuf_, "Toon Platoon"); // TTLocalizer.LeaderboardTitle
    C2D_TextOptimize(&titleText_);
    for (int i = 0; i < kNumLeaders; ++i) {
        RowText& row = rows_[i];
        // updateLeaderBoard: rows past the server's list show '-' and no star.
        char name[64] = "-";
        char score[16] = "-";
        row.used = i < hq.numLeaders;
        row.hasStar = false;
        row.spinRate = 0.0f;
        if (row.used) {
            std::snprintf(name, sizeof(name), "%s", hq.leaders[i].name);
            std::snprintf(score, sizeof(score), "%lu", static_cast<unsigned long>(hq.leaders[i].score));
            const uint32_t s = hq.leaders[i].score;
            int level = -1;
            for (int l = 0; l < 6; ++l) {
                if (s >= kStarLevels[l]) level = l;
            }
            if (level >= 0) {
                row.hasStar = true;
                const bool gold = level >= 4;
                const bool silver = level >= 2 && level < 4;
                row.starColor[0] = gold ? 1.0f : silver ? 0.8f : 0.9f;
                row.starColor[1] = gold ? 1.0f : silver ? 0.8f : 0.6f;
                row.starColor[2] = gold ? 0.0f : silver ? 0.8f : 0.2f;
                row.starColor[3] = 1.0f;
                row.starScale = gold ? 0.8f : 0.6f;
                // Odd levels (20, 50, 100) spin; faster for the higher colours.
                if (level % 2 == 1) row.spinRate = gold ? 15.0f : silver ? 10.0f : 8.0f;
            }
        }
        tt::render::textParse(&row.nameText, textBuf_, name);
        C2D_TextOptimize(&row.nameText);
        tt::render::textParse(&row.scoreText, textBuf_, score);
        C2D_TextOptimize(&row.scoreText);
    }
    textValid_ = true;
}

bool InteriorLife::update(float dt, float playerX, float playerY, float playerZ, net::Client& client,
                          uint32_t nowMs, ToonLine* line) {
    // Toon HQ: the board follows DistributedHQInterior while it is generated in our zone.
    const net::Client::HQLeaderboardState& hq = client.hqLeaderboard();
    if (hq.active != hqActive_) {
        hqActive_ = hq.active;
        setHqResident(hqActive_);
        TT_LOG("interior_life: HQ leaderboard %s", hqActive_ ? "shown" : "hidden");
    }
    if (hqActive_ && hq.revision != hqRevision_) {
        hqRevision_ = hq.revision;
        hqTutorial_ = hq.tutorial;
        updateLeaderboardText(hq);
        TT_LOG("interior_life: leaderboard has %d leaders, tutorial %u", hq.numLeaders, unsigned(hqTutorial_));
    }
    if (hqActive_) {
        for (int i = 0; i < kNumLeaders; ++i) {
            starAngles_[i] = std::fmod(starAngles_[i] + rows_[i].spinRate * dt, 360.0f);
        }
    }

    // Pet shop: the fish swims while DistributedPetshopInterior is here.
    if (client.petshopInterior() != petshopActive_) {
        petshopActive_ = client.petshopInterior();
        setPetshopResident(petshopActive_);
        TT_LOG("interior_life: pet shop fish %s", petshopActive_ ? "swimming" : "gone");
    }
    if (petshopActive_) fishTime_ += dt * kFishPlayRate;

    // Knock-knock: our toon's event sphere (GravityWalker: centre r - 0.1, radius r * 1.04)
    // entering a door sphere sends requestInteract once per entry; leaving sends requestExit.
    const float cz = playerZ + kToonRadius - 0.1f;
    const float reach = kToonRadius * 1.04f;
    int inside = -1;
    for (int i = 0; i < numSpheres_ && inside < 0; ++i) {
        const KnockSphere& s = spheres_[i];
        const float dx = playerX - s.x;
        const float dy = playerY - s.y;
        const float dz = cz - s.z;
        const float r = s.radius + reach;
        if (dx * dx + dy * dy + dz * dz < r * r) inside = i;
    }
    if (inside != insideSphere_) {
        if (requestedDoId_) {
            client.sendKnockKnockExit(requestedDoId_);
            requestedDoId_ = 0;
        }
        insideSphere_ = inside;
        requestPending_ = inside >= 0;
    }
    // The door object can arrive after we are already in its sphere (its visgroup comes into
    // interest with the street cell change), so keep trying until it is here, once per entry.
    if (requestPending_) {
        const net::Client::KnockKnockDoorState* door = client.findKnockKnockDoorByPropId(spheres_[inside].propId);
        if (door && strcmp(door->state, "playing")) {
            requestPending_ = false;
            if (client.sendKnockKnockInteract(door->doId)) {
                requestedDoId_ = door->doId;
                TT_LOG("interior_life: knock-knock door %u (block %u): requestInteract", unsigned(door->doId),
                       unsigned(spheres_[inside].propId));
            }
        }
    }

    // The AI's 'playing' state starts the joke on every client, for the toon it names.
    for (int i = 0; i < numSpheres_ && !joke_.active; ++i) {
        const net::Client::KnockKnockDoorState* door = client.findKnockKnockDoorByPropId(spheres_[i].propId);
        if (!door || strcmp(door->state, "playing") || door->avatarId == 0) continue;
        if (door->doId == joke_.doId && door->serial == joke_.serial) continue; // already told
        if (int32_t(nowMs - door->stateMs) > int32_t(kJokeSeconds * 1000.0f)) continue;
        joke_.active = true;
        joke_.doId = door->doId;
        joke_.serial = door->serial;
        joke_.avatarId = door->avatarId;
        joke_.startMs = door->stateMs;
        joke_.beat = -1;
        joke_.sphere = i;
        joke_.text = game::getKnockKnockJoke(zoneId_, door->propId);
        std::snprintf(joke_.setupWho, sizeof(joke_.setupWho), "%s%s", joke_.text.setup, kDoorWhoAppendix);
        TT_LOG("interior_life: knock-knock block %u for %u: '%s' / '%s'", unsigned(door->propId),
               unsigned(joke_.avatarId), joke_.text.setup, joke_.text.punchline);
    }
    if (!joke_.active) return false;

    // Beats at d = 1 s: 0 door "Knock, knock." (knock SFX at 0.5), 1 toon "Who's there?",
    // 2 door setup, 3 toon "<setup> who?", 4 door punchline + rimshot, 8 cleanup.
    const float t = int32_t(nowMs - joke_.startMs) / 1000.0f;
    bool spoke = false;
    while (joke_.beat < 6) {
        const int next = joke_.beat + 1;
        // Beat 1 of the timeline is the knock SFX at d * 0.5; the rest follow on whole beats.
        const float at = next == 0 ? 0.0f : next == 1 ? 0.5f * kBeatSeconds : (next - 1) * kBeatSeconds;
        if (t < at) break;
        joke_.beat = next;
        switch (next) {
        case 0:
            joke_.doorPhrase = kDoorKnockKnock;
            break;
        case 1:
            audio::playSfx(sfxKnock_);
            break;
        case 2:
            line->avatarId = joke_.avatarId;
            line->phrase = kDoorWhosThere;
            spoke = true;
            break;
        case 3:
            joke_.doorPhrase = joke_.text.setup;
            break;
        case 4:
            line->avatarId = joke_.avatarId;
            line->phrase = joke_.setupWho;
            spoke = true;
            break;
        case 5:
            joke_.doorPhrase = joke_.text.punchline;
            audio::playSfx(sfxRimshot_);
            break;
        default:
            break;
        }
        if (spoke) break;
    }
    if (t >= kJokeSeconds) {
        TT_LOG("interior_life: knock-knock on %u finished", unsigned(joke_.doId));
        joke_.active = false;
        joke_.doorPhrase = nullptr;
    }
    return spoke;
}

void InteriorLife::draw(render::Renderer& renderer) {
    if (petshopActive_ && fishModel_.loaded() && fishAnim_.loaded() && fishAnim_.numFrames() > 0) {
        const float frame = fishTime_ * fishAnim_.fps();
        const uint32_t nf = fishAnim_.numFrames();
        const uint32_t fA = static_cast<uint32_t>(frame) % nf;
        const uint32_t fB = (fA + 1) % nf;
        const float blend = frame - std::floor(frame);
        C3D_Mtx mtx;
        Mtx_Identity(&mtx);
        Mtx_Translate(&mtx, kFishX, kFishY, kFishZ, false);
        Mtx_Scale(&mtx, kFishScale, kFishScale, kFishScale);
        renderer.setBaseTint(0.8f, 0.9f, 1.0f, 0.8f); // fish.setColorScale(0.8, 0.9, 1, 0.8)
        renderer.drawMorph(fishModel_, fishAnim_, fA, fB, blend, mtx);
        renderer.setBaseTint(1.0f, 1.0f, 1.0f, 1.0f);
    }

    if (hqActive_ && starModel_.loaded()) {
        C3D_Mtx board;
        getBoardMtx(&board);
        for (int i = 0; i < kNumLeaders; ++i) {
            if (!rows_[i].hasStar) continue;
            float wx, wy, wz;
            boardLocalToWorld(board, kStarX, 0.0f, -(i + 1) + kStarZ, &wx, &wy, &wz);
            C3D_Mtx m;
            Mtx_Identity(&m);
            Mtx_Translate(&m, wx, wy, wz, false);
            Mtx_RotateZ(&m, 90.0f * kDegToRad, false); // face out of the board like its rows
            Mtx_RotateY(&m, starAngles_[i] * kDegToRad, false);
            const float s = rows_[i].starScale * 0.9f;
            Mtx_Scale(&m, s, s, s);
            renderer.setBaseTint(rows_[i].starColor[0], rows_[i].starColor[1], rows_[i].starColor[2],
                                 rows_[i].starColor[3]);
            renderer.drawModel(starModel_, m);
        }
        renderer.setBaseTint(1.0f, 1.0f, 1.0f, 1.0f);
    }
}

void InteriorLife::draw2D(const C3D_Mtx& proj, const C3D_Mtx& view) {
    if (!hqActive_ || !textValid_) return;
    C3D_Mtx board;
    getBoardMtx(&board);
    float sx, sy, scale;
    if (projectBoardText(proj, view, board, 0.0f, 0.0f, &sx, &sy, &scale)) {
        float w = 0.0f;
        C2D_TextGetDimensions(&titleText_, scale, scale, &w, nullptr);
        C2D_DrawText(&titleText_, C2D_WithColor, sx - w * 0.5f, sy - kFontLinePx * scale, 0.5f, scale, scale,
                     kTitleColor);
    }
    for (int i = 0; i < kNumLeaders; ++i) {
        const float lz = -(i + 1);
        // Seen from the front, a row reads star | score | name: the score is right-aligned at
        // DHQIscorePathPos and the name left-aligned at DHQInamePathPos (scale DHQInamePath).
        if (projectBoardText(proj, view, board, kNamePathX, lz, &sx, &sy, &scale)) {
            const float s = scale * 0.9f;
            C2D_DrawText(&rows_[i].nameText, C2D_WithColor, sx, sy - kFontLinePx * s, 0.5f, s, s, kNameColor);
        }
        if (projectBoardText(proj, view, board, kScorePathX, lz, &sx, &sy, &scale)) {
            float w = 0.0f;
            C2D_TextGetDimensions(&rows_[i].scoreText, scale, scale, &w, nullptr);
            C2D_DrawText(&rows_[i].scoreText, C2D_WithColor, sx - w, sy - kFontLinePx * scale, 0.5f, scale, scale,
                         kScoreColor);
        }
    }
}

void InteriorLife::addBalloons(ui::Balloons& balloons, const C3D_Mtx& proj, const C3D_Mtx& view,
                               float nametagH) const {
    if (!joke_.active || !joke_.doorPhrase || joke_.sphere < 0) return;
    // nametagNP at the sphere centre + (0, 0, avatar.getHeight() + 2).
    const KnockSphere& s = spheres_[joke_.sphere];
    float sx, sy, depth;
    if (ui::projectToScreen(proj, view, s.x, s.y, s.z + nametagH + 2.0f, &sx, &sy, &depth) && depth > 0.0f) {
        // nametag.setChat(..., CFSpeech): no timeout, the phrase stays until the next one.
        balloons.addSpeaker(sx, sy, kDoorNametag, joke_.doorPhrase, 0.0f, depth);
    }
}

} // namespace tt::world
