#include "world/interior_life.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "core/log.h"
#include "ui/projection.h"

namespace tt::world {

namespace {

constexpr float kDegToRad = 3.14159265f / 180.0f;

// Toon HQ Leaderboard dimensions (docs/design/interior_life.md section 1.3)
// Empty board anchor in HQ_interior: (-47.4427, 29.6598, 8.15647)
// Board local transform: pos (0.1, 0, 4.5), hpr (90, 0, 0), scale (0.9, 0.9, 0.9)
constexpr float kBoardAnchorX = -47.4427f;
constexpr float kBoardAnchorY = 29.6598f;
constexpr float kBoardAnchorZ = 8.15647f;

void getBoardMtx(C3D_Mtx* out) {
    Mtx_Identity(out);
    Mtx_Translate(out, kBoardAnchorX, kBoardAnchorY, kBoardAnchorZ, false);
    Mtx_Translate(out, 0.1f, 0.0f, 4.5f, false);
    Mtx_RotateZ(out, 90.0f * kDegToRad, false);
    Mtx_Scale(out, 0.9f, 0.9f, 0.9f);
}

void boardLocalToWorld(const C3D_Mtx& bMtx, float lx, float ly, float lz, float* wx, float* wy, float* wz) {
    *wx = bMtx.r[0].x * lx + bMtx.r[1].x * ly + bMtx.r[2].x * lz + bMtx.r[3].x;
    *wy = bMtx.r[0].y * lx + bMtx.r[1].y * ly + bMtx.r[2].y * lz + bMtx.r[3].y;
    *wz = bMtx.r[0].z * lx + bMtx.r[1].z * ly + bMtx.r[2].z * lz + bMtx.r[3].z;
}

} // namespace

InteriorLife::~InteriorLife() {
    shutdown();
}

bool InteriorLife::init() {
    TT_LOG("interior_life: initializing assets");
    starModel_.load("romfs:/baked/name_star.ttm");
    fishModel_.load("romfs:/baked/interiorfish.ttm");
    fishAnim_.load("romfs:/baked/interiorfish_swim.tta");
    periscopeModel_.load("romfs:/baked/periscope.ttm");
    periscopeAnim_.load("romfs:/baked/periscope_anim.tta");

    sfxKnock_ = audio::loadSfx("romfs:/baked/audio/gui_knock.tsnd");
    sfxJoke_ = audio::loadSfx("romfs:/baked/audio/tell_joke.tsnd");

    textBuf_ = C2D_TextBufNew(4096);
    updateLeaderboardText();

    return true;
}

void InteriorLife::shutdown() {
    starModel_.unload();
    fishModel_.unload();
    fishAnim_.unload();
    periscopeModel_.unload();
    periscopeAnim_.unload();

    if (textBuf_) {
        C2D_TextBufDelete(textBuf_);
        textBuf_ = nullptr;
    }
    if (sfxKnock_ != audio::kInvalidSfx) {
        audio::freeSfx(sfxKnock_);
        sfxKnock_ = audio::kInvalidSfx;
    }
    if (sfxJoke_ != audio::kInvalidSfx) {
        audio::freeSfx(sfxJoke_);
        sfxJoke_ = audio::kInvalidSfx;
    }
    textValid_ = false;
}

void InteriorLife::setZone(uint32_t zoneId, const char* ptsPath) {
    zoneId_ = zoneId;
    const uint32_t local = zoneId % 1000;
    const bool isInterior = (local >= 500);

    // HQ interior is typically block 20 (e.g. 2520, or zoneId % 100 == 20)
    hqActive_ = isInterior && ((local % 100) == 20);
    // Pet shop interior is block 22 (e.g. 2522)
    petShopActive_ = isInterior && ((local % 100) == 22);
    // Street zone
    streetActive_ = !isInterior && (local >= 100);

    knockActive_ = false;
    knockBeat_ = -1;
    knockTimer_ = 0.0f;
    fishTime_ = 0.0f;
    periscopeTime_ = 0.0f;

    numStreetDoors_ = 0;
    if (streetActive_ && ptsPath) {
        parsePtsPoints(ptsPath);
    }

    TT_LOG("interior_life: zone %lu (hq=%d pet=%d street=%d doors=%zu)",
           static_cast<unsigned long>(zoneId), hqActive_, petShopActive_, streetActive_, numStreetDoors_);
}

void InteriorLife::parsePtsPoints(const char* ptsPath) {
    if (!ptsPath || !ptsPath[0]) return;
    FILE* f = fopen(ptsPath, "r");
    if (!f) return;

    numStreetDoors_ = 0;
    char line[128];
    while (fgets(line, sizeof(line), f) && numStreetDoors_ < kMaxStreetDoors) {
        char name[64];
        float x, y, z, h;
        if (sscanf(line, "%63s %f %f %f %f", name, &x, &y, &z, &h) == 5) {
            // Street door anchors: e.g. "tb1:toon_landmark_TT_A3_DNARoot_door_origin" or "door_origin_1"
            uint16_t propId = 0;
            const char* tb = strstr(name, "tb");
            if (tb && (tb == name || *(tb - 1) == ':' || *(tb - 1) == '/')) {
                propId = static_cast<uint16_t>(atoi(tb + 2));
            } else {
                const char* do_str = strstr(name, "door_origin_");
                if (do_str) propId = static_cast<uint16_t>(atoi(do_str + 12));
            }
            if (propId > 0 && strstr(name, "door_origin")) {
                streetDoors_[numStreetDoors_].propId = propId;
                streetDoors_[numStreetDoors_].x = x;
                streetDoors_[numStreetDoors_].y = y;
                streetDoors_[numStreetDoors_].z = z;
                streetDoors_[numStreetDoors_].h = h;
                numStreetDoors_++;
            }
        }
    }
    fclose(f);
}

void InteriorLife::handleEvent(const net::Event& ev, const net::Client& client, uint32_t nowMs) {
    (void)nowMs;
    if (ev.type == net::EventType::HQLeaderboard) {
        hqState_ = client.hqLeaderboard();
        hqRevision_++;
        updateLeaderboardText();
        TT_LOG("interior_life: leaderboard updated from net (%d leaders)", hqState_.numLeaders);
    } else if (ev.type == net::EventType::HQTutorial) {
        hqState_.tutorial = client.hqLeaderboard().tutorial;
        TT_LOG("interior_life: hq tutorial set to %u", unsigned(hqState_.tutorial));
    } else if (ev.type == net::EventType::KnockKnockInteract || ev.type == net::EventType::KnockKnockState) {
        const net::Client::KnockKnockDoorState* door = client.findKnockKnockDoor(ev.arg);
        if (door && door->propId > 0) {
            triggerKnockKnock(zoneId_, door->propId);
            knockDoId_ = door->doId;
        }
    } else if (ev.type == net::EventType::KnockKnockGone) {
        if (knockDoId_ == static_cast<uint32_t>(ev.arg)) {
            knockActive_ = false;
            knockDoId_ = 0;
        }
    }
}

void InteriorLife::setTestLeaderboard() {
    hqState_.active = true;
    hqState_.numLeaders = 4;
    hqState_.leaders[0].avId = 1001;
    strncpy(hqState_.leaders[0].name, "Mickey Mouse", sizeof(hqState_.leaders[0].name));
    hqState_.leaders[0].score = 120; // Gold star, spinning fast (15 deg/s)

    hqState_.leaders[1].avId = 1002;
    strncpy(hqState_.leaders[1].name, "Donald Duck", sizeof(hqState_.leaders[1].name));
    hqState_.leaders[1].score = 80;  // Gold star

    hqState_.leaders[2].avId = 1003;
    strncpy(hqState_.leaders[2].name, "Goofy", sizeof(hqState_.leaders[2].name));
    hqState_.leaders[2].score = 35;  // Silver star

    hqState_.leaders[3].avId = 1004;
    strncpy(hqState_.leaders[3].name, "Pluto", sizeof(hqState_.leaders[3].name));
    hqState_.leaders[3].score = 15;  // Bronze star

    updateLeaderboardText();
    TT_LOG("interior_life: populated test leaderboard with %d leaders", hqState_.numLeaders);
}

void InteriorLife::updateLeaderboardText() {
    if (!textBuf_) return;
    C2D_TextBufClear(textBuf_);

    // Upstream TTLocalizer.LeaderboardTitle = "Toon Platoon"
    C2D_TextParse(&titleText_, textBuf_, "Toon Platoon");
    C2D_TextOptimize(&titleText_);

    for (int i = 0; i < 10; ++i) {
        char nameBuf[64] = "-";
        char scoreBuf[32] = "-";
        rows_[i].hasStar = false;
        rows_[i].starScale = 0.6f;
        rows_[i].spinRate = 0.0f;

        if (hqState_.active && i < hqState_.numLeaders && hqState_.leaders[i].name[0]) {
            snprintf(nameBuf, sizeof(nameBuf), "%s", hqState_.leaders[i].name);
            snprintf(scoreBuf, sizeof(scoreBuf), "%lu", static_cast<unsigned long>(hqState_.leaders[i].score));

            const uint32_t s = hqState_.leaders[i].score;
            if (s >= 10) {
                rows_[i].hasStar = true;
                if (s >= 75) {
                    // Gold star
                    rows_[i].starColor[0] = 1.0f; rows_[i].starColor[1] = 1.0f; rows_[i].starColor[2] = 0.0f; rows_[i].starColor[3] = 1.0f;
                    rows_[i].starScale = 0.8f;
                    if (s >= 100) rows_[i].spinRate = 15.0f;
                } else if (s >= 30) {
                    // Silver star
                    rows_[i].starColor[0] = 0.8f; rows_[i].starColor[1] = 0.8f; rows_[i].starColor[2] = 0.8f; rows_[i].starColor[3] = 1.0f;
                    rows_[i].starScale = 0.6f;
                    if (s >= 50) rows_[i].spinRate = 10.0f;
                } else {
                    // Bronze star
                    rows_[i].starColor[0] = 0.9f; rows_[i].starColor[1] = 0.6f; rows_[i].starColor[2] = 0.2f; rows_[i].starColor[3] = 1.0f;
                    rows_[i].starScale = 0.6f;
                    if (s >= 20) rows_[i].spinRate = 8.0f;
                }
            }
        }

        C2D_TextParse(&rows_[i].nameText, textBuf_, nameBuf);
        C2D_TextOptimize(&rows_[i].nameText);
        C2D_TextParse(&rows_[i].scoreText, textBuf_, scoreBuf);
        C2D_TextOptimize(&rows_[i].scoreText);
    }

    textValid_ = true;
}

bool InteriorLife::triggerKnockKnock(uint32_t zoneId, uint16_t propId) {
    currentJoke_ = game::getKnockKnockJoke(zoneId, propId);
    snprintf(jokeSetupWho_, sizeof(jokeSetupWho_), "%s who?", currentJoke_.setup);

    knockPropId_ = propId;
    knockActive_ = true;
    knockTimer_ = 0.0f;
    knockBeat_ = 0;

    // Locate door anchor coordinates
    knockDoorX_ = 0.0f; knockDoorY_ = 0.0f; knockDoorZ_ = 0.0f;
    for (size_t i = 0; i < numStreetDoors_; ++i) {
        if (streetDoors_[i].propId == propId) {
            knockDoorX_ = streetDoors_[i].x;
            knockDoorY_ = streetDoors_[i].y;
            knockDoorZ_ = streetDoors_[i].z;
            break;
        }
    }

    TT_LOG("interior_life: knock-knock started on prop %u: '%s' / '%s'",
           propId, currentJoke_.setup, currentJoke_.punchline);
    return true;
}

void InteriorLife::update(float dt, float playerX, float playerY, float playerZ, net::Client& client, uint32_t nowMs) {
    (void)nowMs;

    if (petShopActive_) {
        fishTime_ += dt * 0.7f; // Upstream play rate: 0.7
    }

    if (hqActive_) {
        periscopeTime_ += dt;
        for (int i = 0; i < 10; ++i) {
            starAngles_[i] += rows_[i].spinRate * dt;
            if (starAngles_[i] >= 360.0f) starAngles_[i] -= 360.0f;
        }
    }

    if (streetActive_) {
        if (knockActive_) {
            knockTimer_ += dt;
            if (knockTimer_ >= 0.5f && knockBeat_ == 0) {
                knockBeat_ = 1;
                audio::playSfx(sfxKnock_);
                TT_LOG("interior_life: knock-knock beat 0.5s SFX");
            } else if (knockTimer_ >= 1.0f && knockBeat_ == 1) {
                knockBeat_ = 2;
                TT_LOG("interior_life: knock-knock beat 1.0s: Who's there?");
            } else if (knockTimer_ >= 2.0f && knockBeat_ == 2) {
                knockBeat_ = 3;
                TT_LOG("interior_life: knock-knock beat 2.0s: %s", currentJoke_.setup);
            } else if (knockTimer_ >= 3.0f && knockBeat_ == 3) {
                knockBeat_ = 4;
                TT_LOG("interior_life: knock-knock beat 3.0s: %s", jokeSetupWho_);
            } else if (knockTimer_ >= 4.0f && knockBeat_ == 4) {
                knockBeat_ = 5;
                audio::playSfx(sfxJoke_);
                TT_LOG("interior_life: knock-knock beat 4.0s: %s (SFX)", currentJoke_.punchline);
            } else if (knockTimer_ >= 8.0f) {
                TT_LOG("interior_life: knock-knock sequence complete at 8.0s");
                knockActive_ = false;
                knockBeat_ = -1;
                if (knockDoId_ != 0) {
                    client.sendKnockKnockExit(knockDoId_);
                    knockDoId_ = 0;
                }
            }
        } else {
            // Check proximity to street doors (within 6.0 units)
            for (size_t i = 0; i < numStreetDoors_; ++i) {
                const float dx = playerX - streetDoors_[i].x;
                const float dy = playerY - streetDoors_[i].y;
                const float dz = playerZ - streetDoors_[i].z;
                const float distSq = dx * dx + dy * dy + dz * dz;
                if (distSq < 36.0f) { // 6.0 units squared
                    const net::Client::KnockKnockDoorState* netDoor = client.findKnockKnockDoorByPropId(streetDoors_[i].propId);
                    if (netDoor && netDoor->active) {
                        client.sendKnockKnockInteract(netDoor->doId);
                    }
                    triggerKnockKnock(zoneId_, streetDoors_[i].propId);
                    break;
                }
            }
        }
    }
}

void InteriorLife::draw(render::Renderer& renderer, const C3D_FVec& eye) {
    (void)eye;

    // 1. Pet Shop fish: anchor (0, 6, -4), scale 0.8, color (0.8, 0.9, 1, 0.8)
    if (petShopActive_ && fishModel_.loaded() && fishAnim_.loaded()) {
        const float frame = fishTime_ * fishAnim_.fps();
        const uint32_t nf = fishAnim_.numFrames();
        if (nf > 0) {
            const uint32_t fA = static_cast<uint32_t>(frame) % nf;
            const uint32_t fB = (fA + 1) % nf;
            const float blend = frame - static_cast<float>(static_cast<uint32_t>(frame));

            C3D_Mtx mtx;
            Mtx_Identity(&mtx);
            Mtx_Translate(&mtx, 0.0f, 6.0f, -4.0f, false);
            Mtx_Scale(&mtx, 0.8f, 0.8f, 0.8f);

            renderer.setBaseTint(0.8f, 0.9f, 1.0f, 0.8f);
            renderer.drawMorph(fishModel_, fishAnim_, fA, fB, blend, mtx);
            renderer.setBaseTint(1.0f, 1.0f, 1.0f, 1.0f);
        }
    }

    // 2. HQ interior: periscope and trophy stars
    if (hqActive_) {
        // Periscope: anchor (-6.196, 31.649, 7.899). Hidden in tutorial mode.
        if (periscopeModel_.loaded() && periscopeAnim_.loaded() && hqState_.tutorial == 0) {
            const float frame = periscopeTime_ * periscopeAnim_.fps();
            const uint32_t nf = periscopeAnim_.numFrames();
            if (nf > 0) {
                const uint32_t fA = static_cast<uint32_t>(frame) % nf;
                const uint32_t fB = (fA + 1) % nf;
                const float blend = frame - static_cast<float>(static_cast<uint32_t>(frame));

                C3D_Mtx mtx;
                Mtx_Identity(&mtx);
                Mtx_Translate(&mtx, -6.196f, 31.649f, 7.899f, false);

                renderer.drawMorph(periscopeModel_, periscopeAnim_, fA, fB, blend, mtx);
            }
        }

        // Leaderboard trophy stars
        if (starModel_.loaded()) {
            C3D_Mtx bMtx;
            getBoardMtx(&bMtx);

            for (int i = 0; i < 10; ++i) {
                if (!rows_[i].hasStar) continue;
                const float lz = -(i + 1) * 1.0f + 0.3f;
                float wx, wy, wz;
                boardLocalToWorld(bMtx, -6.6f, 0.0f, lz, &wx, &wy, &wz);

                C3D_Mtx sMtx;
                Mtx_Identity(&sMtx);
                Mtx_Translate(&sMtx, wx, wy, wz, false);
                Mtx_RotateY(&sMtx, starAngles_[i] * kDegToRad, false);
                Mtx_Scale(&sMtx, rows_[i].starScale, rows_[i].starScale, rows_[i].starScale);

                renderer.setBaseTint(rows_[i].starColor[0], rows_[i].starColor[1],
                                     rows_[i].starColor[2], rows_[i].starColor[3]);
                renderer.drawModel(starModel_, sMtx);
            }
            renderer.setBaseTint(1.0f, 1.0f, 1.0f, 1.0f);
        }
    }
}

void InteriorLife::draw2D(const C3D_Mtx& proj, const C3D_Mtx& view) {
    if (!hqActive_ || !textValid_) return;

    C3D_Mtx bMtx;
    getBoardMtx(&bMtx);

    // Title: local (0, 0, 0)
    float wx, wy, wz, sx, sy, depth;
    boardLocalToWorld(bMtx, 0.0f, 0.0f, 0.0f, &wx, &wy, &wz);
    if (ui::projectToScreen(proj, view, wx, wy, wz, &sx, &sy, &depth) && depth > 0.0f) {
        // Draw centered title: "Toon Platoon" in cyan/green (0xFFB3C080)
        C2D_DrawText(&titleText_, C2D_WithColor, sx - 40.0f, sy - 10.0f, 0.5f, 0.5f, 0.5f, 0xFFB3C080);
    }

    // Rows 1..10
    for (int i = 0; i < 10; ++i) {
        const float lz = -(i + 1) * 1.0f;

        // Name: local (-4.0, 0, lz)
        boardLocalToWorld(bMtx, -4.0f, 0.0f, lz, &wx, &wy, &wz);
        if (ui::projectToScreen(proj, view, wx, wy, wz, &sx, &sy, &depth) && depth > 0.0f) {
            C2D_DrawText(&rows_[i].nameText, C2D_WithColor, sx, sy - 6.0f, 0.5f, 0.4f, 0.4f, 0xB3FFFFFF);
        }

        // Score: local (-4.6, 0, lz)
        boardLocalToWorld(bMtx, -4.6f, 0.0f, lz, &wx, &wy, &wz);
        if (ui::projectToScreen(proj, view, wx, wy, wz, &sx, &sy, &depth) && depth > 0.0f) {
            C2D_DrawText(&rows_[i].scoreText, C2D_WithColor, sx, sy - 6.0f, 0.5f, 0.4f, 0.4f, 0xB31AE6FF);
        }
    }
}

void InteriorLife::addBalloons(ui::Balloons& balloons, const C3D_Mtx& proj, const C3D_Mtx& view,
                               float playerX, float playerY, float playerZ, float nametagH, const char* playerName) {
    if (!knockActive_) return;

    float sx, sy, depth;

    // Door speaks on beats 0, 2, 4
    if (knockTimer_ < 1.0f || (knockTimer_ >= 2.0f && knockTimer_ < 3.0f) || knockTimer_ >= 4.0f) {
        const char* phrase = "Knock, knock.";
        if (knockTimer_ >= 4.0f) phrase = currentJoke_.punchline;
        else if (knockTimer_ >= 2.0f) phrase = currentJoke_.setup;

        if (ui::projectToScreen(proj, view, knockDoorX_, knockDoorY_, knockDoorZ_ + 4.0f, &sx, &sy, &depth) && depth > 0.0f) {
            balloons.addSpeaker(sx, sy, "Door", phrase, 0.0f, depth, 0xFF4080C0);
        }
    }

    // Avatar speaks on beats 1, 3
    if ((knockTimer_ >= 1.0f && knockTimer_ < 2.0f) || (knockTimer_ >= 3.0f && knockTimer_ < 4.0f)) {
        const char* phrase = (knockTimer_ < 2.0f) ? "Who's there?" : jokeSetupWho_;
        if (ui::projectToScreen(proj, view, playerX, playerY, playerZ + nametagH, &sx, &sy, &depth) && depth > 0.0f) {
            balloons.addSpeaker(sx, sy, playerName ? playerName : "Toon", phrase, 0.0f, depth, ui::Balloons::kNameColorToon);
        }
    }
}

} // namespace tt::world
