#include "world/battle_fx.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef __3DS__
#include "core/log.h"
#include "world/battle.h"
#endif

namespace tt::world {

#ifdef __3DS__
namespace {

constexpr float kDegToRad = 3.14159265f / 180.0f;
constexpr float kPi = 3.14159265f;

} // namespace
#endif

// ----------------------------------------------------------------------
// Splat Configuration
// ----------------------------------------------------------------------
SplatConfig getSplatConfig(int track, int level) {
    SplatConfig cfg;
    if (track == 4) { // Throw
        if (level == 0) {
            // Tart: purple, scale 0.3
            cfg.r = 0.215686f; cfg.g = 0.156863f; cfg.b = 0.580392f; cfg.a = 1.0f;
            cfg.scale = 0.3f;
        } else if (level == 1) {
            // Fruit pie slice: purple, scale 0.5
            cfg.r = 0.215686f; cfg.g = 0.156863f; cfg.b = 0.580392f; cfg.a = 1.0f;
            cfg.scale = 0.5f;
        } else {
            // Cream pie slice / whole: yellow, scale 0.5
            cfg.r = 0.980392f; cfg.g = 0.945098f; cfg.b = 0.094118f; cfg.a = 1.0f;
            cfg.scale = 0.5f;
        }
    } else if (track == 5) { // Squirt splash
        cfg.r = 0.75f; cfg.g = 0.75f; cfg.b = 1.0f; cfg.a = 1.0f;
        cfg.scale = 1.0f;
    } else {
        cfg.r = 1.0f; cfg.g = 1.0f; cfg.b = 1.0f; cfg.a = 1.0f;
        cfg.scale = 0.5f;
    }
    return cfg;
}

// ----------------------------------------------------------------------
// Movie Timing Specifications
// ----------------------------------------------------------------------
MovieTiming getMovieTiming(int track, int level) {
    (void)level;
    MovieTiming t;
    if (track == 4) { // Throw
        t.releaseMs = 2700;
        t.hitMs = 3000;
        t.durMs = 3500;
    } else if (track == 5) { // Squirt
        t.releaseMs = 2500;
        t.hitMs = 2700;
        t.durMs = 3300;
    } else if (track == 3) { // Sound
        t.releaseMs = 2500;
        t.hitMs = 2800;
        t.durMs = 3500;
    } else if (track == 6) { // Drop
        t.releaseMs = 3000;
        t.hitMs = 3800;
        t.durMs = 4500;
    } else if (track == 1) { // Trap
        t.releaseMs = 2500;
        t.hitMs = 2500;
        t.durMs = 3500;
    } else if (track == 2) { // Lure
        t.releaseMs = 3000;
        t.hitMs = 3200;
        t.durMs = 4500;
    } else if (track == 0) { // Heal
        t.releaseMs = 2000;
        t.hitMs = 2800;
        t.durMs = 3200;
    } else { // Suit attack or generic
        t.releaseMs = 1000;
        t.hitMs = 1200;
        t.durMs = 2200;
    }
    return t;
}

// ----------------------------------------------------------------------
// Gag Atlas Subtexture Coordinates
// ----------------------------------------------------------------------
const GagSubTex kGagSubTextures[7][7] = {
    { // track 0 (Toon-Up)
        { 28, 28, 0.8847656f, 0.8652344f, 0.9902344f, 0.7597656f }, // inventory_feather
        { 28, 28, 0.5097656f, 0.6152344f, 0.6152344f, 0.5097656f }, // inventory_megaphone
        { 28, 28, 0.2597656f, 0.6152344f, 0.3652344f, 0.5097656f }, // inventory_lipstick
        { 28, 28, 0.6347656f, 0.9902344f, 0.7402344f, 0.8847656f }, // inventory_bamboo_cane
        { 28, 28, 0.8847656f, 0.6152344f, 0.9902344f, 0.5097656f }, // inventory_pixiedust
        { 28, 28, 0.0097656f, 0.6152344f, 0.1152344f, 0.5097656f }, // inventory_juggling_cubes
        { 28, 28, 0.1347656f, 0.6152344f, 0.2402344f, 0.5097656f }, // inventory_ladder
    },
    { // track 1 (Trap)
        { 28, 28, 0.7597656f, 0.9902344f, 0.8652344f, 0.8847656f }, // inventory_bannana_peel
        { 28, 28, 0.1347656f, 0.4902344f, 0.2402344f, 0.3847656f }, // inventory_rake
        { 28, 28, 0.3847656f, 0.6152344f, 0.4902344f, 0.5097656f }, // inventory_marbles
        { 28, 28, 0.0097656f, 0.4902344f, 0.1152344f, 0.3847656f }, // inventory_quicksand_icon
        { 28, 28, 0.6347656f, 0.3652344f, 0.7402344f, 0.2597656f }, // inventory_trapdoor
        { 28, 28, 0.3847656f, 0.3652344f, 0.4902344f, 0.2597656f }, // inventory_tnt
        { 28, 28, 0.5097656f, 0.3652344f, 0.6152344f, 0.2597656f }, // inventory_traintracks
    },
    { // track 2 (Lure)
        { 28, 28, 0.1347656f, 0.9902344f, 0.2402344f, 0.8847656f }, // inventory_1dollarbill
        { 28, 28, 0.8847656f, 0.4902344f, 0.9902344f, 0.3847656f }, // inventory_small_magnet
        { 28, 28, 0.2597656f, 0.9902344f, 0.3652344f, 0.8847656f }, // inventory_5dollarbill
        { 28, 28, 0.8847656f, 0.9902344f, 0.9902344f, 0.8847656f }, // inventory_big_magnet
        { 28, 28, 0.0097656f, 0.9902344f, 0.1152344f, 0.8847656f }, // inventory_10dollarbill
        { 28, 28, 0.8847656f, 0.7402344f, 0.9902344f, 0.6347656f }, // inventory_hypno_goggles
        { 28, 28, 0.5097656f, 0.4902344f, 0.6152344f, 0.3847656f }, // inventory_screen
    },
    { // track 3 (Sound)
        { 28, 28, 0.0097656f, 0.8652344f, 0.1152344f, 0.7597656f }, // inventory_bikehorn
        { 28, 28, 0.1347656f, 0.2402344f, 0.2402344f, 0.1347656f }, // inventory_whistle
        { 28, 28, 0.1347656f, 0.8652344f, 0.2402344f, 0.7597656f }, // inventory_bugle
        { 28, 28, 0.5097656f, 0.9902344f, 0.6152344f, 0.8847656f }, // inventory_aoogah
        { 28, 28, 0.7597656f, 0.8652344f, 0.8652344f, 0.7597656f }, // inventory_elephant
        { 28, 28, 0.2597656f, 0.7402344f, 0.3652344f, 0.6347656f }, // inventory_fog_horn
        { 28, 28, 0.6347656f, 0.6152344f, 0.7402344f, 0.5097656f }, // inventory_opera_singer
    },
    { // track 4 (Throw)
        { 28, 28, 0.2597656f, 0.3652344f, 0.3652344f, 0.2597656f }, // inventory_tart
        { 28, 28, 0.3847656f, 0.7402344f, 0.4902344f, 0.6347656f }, // inventory_fruit_pie_slice
        { 28, 28, 0.3847656f, 0.8652344f, 0.4902344f, 0.7597656f }, // inventory_cream_pie_slice
        { 28, 28, 0.5097656f, 0.7402344f, 0.6152344f, 0.6347656f }, // inventory_fruitpie
        { 28, 28, 0.5097656f, 0.8652344f, 0.6152344f, 0.7597656f }, // inventory_creampie
        { 28, 28, 0.2597656f, 0.8652344f, 0.3652344f, 0.7597656f }, // inventory_cake
        { 28, 28, 0.8847656f, 0.3652344f, 0.9902344f, 0.2597656f }, // inventory_wedding
    },
    { // track 5 (Squirt)
        { 28, 28, 0.0097656f, 0.3652344f, 0.1152344f, 0.2597656f }, // inventory_squirt_flower
        { 28, 28, 0.7597656f, 0.7402344f, 0.8652344f, 0.6347656f }, // inventory_glass_of_water
        { 28, 28, 0.7597656f, 0.3652344f, 0.8652344f, 0.2597656f }, // inventory_water_gun
        { 28, 28, 0.6347656f, 0.4902344f, 0.7402344f, 0.3847656f }, // inventory_seltzer_bottle
        { 28, 28, 0.0097656f, 0.7402344f, 0.1152344f, 0.6347656f }, // inventory_firehose
        { 28, 28, 0.1347656f, 0.3652344f, 0.2402344f, 0.2597656f }, // inventory_storm_cloud
        { 28, 28, 0.6347656f, 0.7402344f, 0.7402344f, 0.6347656f }, // inventory_geyser
    },
    { // track 6 (Drop)
        { 28, 28, 0.1347656f, 0.7402344f, 0.2402344f, 0.6347656f }, // inventory_flower_pot
        { 28, 28, 0.3847656f, 0.4902344f, 0.4902344f, 0.3847656f }, // inventory_sandbag
        { 28, 28, 0.3847656f, 0.9902344f, 0.4902344f, 0.8847656f }, // inventory_anvil
        { 28, 28, 0.0097656f, 0.2402344f, 0.1152344f, 0.1347656f }, // inventory_weight
        { 28, 28, 0.2597656f, 0.4902344f, 0.3652344f, 0.3847656f }, // inventory_safe_box
        { 28, 28, 0.7597656f, 0.6152344f, 0.8652344f, 0.5097656f }, // inventory_piano
        { 28, 28, 0.7597656f, 0.4902344f, 0.8652344f, 0.3847656f }, // inventory_ship
    },
};

#ifdef __3DS__

// ----------------------------------------------------------------------
// JNT1 Loader Implementation
// ----------------------------------------------------------------------
bool JntTrack::load(const char* path) {
    unload();
    FILE* f = fopen(path, "rb");
    if (!f) return false;

    struct Header {
        char magic[4];
        uint16_t version;
        uint16_t jointCount;
        uint32_t frameCount;
        float fps;
        uint32_t dataOffset;
        uint32_t frameStride;
        uint32_t nameTableOffset;
        uint32_t pad;
    } h;

    if (fread(&h, 1, sizeof(h), f) != sizeof(h) || memcmp(h.magic, "JNT1", 4) != 0 || h.version != 1) {
        fclose(f);
        return false;
    }

    jointCount = h.jointCount;
    frameCount = h.frameCount;
    fps = h.fps;
    dataOffset = h.dataOffset;
    frameStride = h.frameStride;

    if (jointCount > 4) jointCount = 4;
    fseek(f, h.nameTableOffset, SEEK_SET);
    for (uint16_t i = 0; i < jointCount; ++i) {
        fread(names[i], 1, 32, f);
        names[i][31] = '\0';
    }

    const uint32_t dataBytes = frameCount * frameStride;
    data = static_cast<float*>(malloc(dataBytes));
    if (!data) {
        fclose(f);
        return false;
    }

    fseek(f, dataOffset, SEEK_SET);
    const size_t read = fread(data, 1, dataBytes, f);
    fclose(f);
    if (read != dataBytes) {
        unload();
        return false;
    }
    return true;
}

void JntTrack::unload() {
    if (data) free(data);
    data = nullptr;
    jointCount = 0;
    frameCount = 0;
}

int JntTrack::findJoint(const char* name) const {
    for (int i = 0; i < jointCount; ++i) {
        if (!strncmp(names[i], name, 31)) return i;
    }
    return -1;
}

bool JntTrack::sample(int jointIdx, float t, C3D_Mtx* out) const {
    if (!data || jointIdx < 0 || jointIdx >= jointCount || frameCount == 0 || !out) return false;
    float f = t * fps;
    uint32_t a = uint32_t(f);
    uint32_t b = a + 1;
    float blend = f - float(a);
    if (a >= frameCount) a = frameCount - 1;
    if (b >= frameCount) b = frameCount - 1;

    const float* ma = data + a * (jointCount * 12) + jointIdx * 12;
    const float* mb = data + b * (jointCount * 12) + jointIdx * 12;

    for (int row = 0; row < 3; row++) {
        const float* ra = ma + row * 4;
        const float* rb = mb + row * 4;
        out->r[row].x = ra[0] + (rb[0] - ra[0]) * blend;
        out->r[row].y = ra[1] + (rb[1] - ra[1]) * blend;
        out->r[row].z = ra[2] + (rb[2] - ra[2]) * blend;
        out->r[row].w = ra[3] + (rb[3] - ra[3]) * blend;
    }
    out->r[3] = FVec4_New(0.0f, 0.0f, 0.0f, 1.0f);
    return true;
}

// ----------------------------------------------------------------------
// GagAtlas Implementation
// ----------------------------------------------------------------------
GagAtlas& GagAtlas::instance() {
    static GagAtlas atlas;
    return atlas;
}

bool GagAtlas::init() {
    if (model_.loaded()) return true;
    return model_.load("sdmc:/3ds/toontown3ds/data/props/inventory_atlas.ttm");
}

void GagAtlas::shutdown() {
    model_.unload();
}

void GagAtlas::drawIcon(int track, int level, float x, float y, float size, float depth, float alpha) {
    if (!model_.loaded() || track < 0 || track >= 7 || level < 0 || level >= 7) return;
    C3D_Tex* tex = model_.texture(0);
    if (!tex) return;

    const GagSubTex& st = kGagSubTextures[track][level];
    Tex3DS_SubTexture sub;
    sub.width = st.width;
    sub.height = st.height;
    sub.left = st.left;
    sub.top = st.top;
    sub.right = st.right;
    sub.bottom = st.bottom;

    C2D_Image img;
    img.tex = tex;
    img.subtex = &sub;

    const float scale = size / float(st.width);
    if (alpha < 0.99f) {
        C2D_ImageTint tint;
        C2D_AlphaImageTint(&tint, alpha);
        C2D_DrawImageAt(img, x, y, depth, &tint, scale, scale);
    } else {
        C2D_DrawImageAt(img, x, y, depth, nullptr, scale, scale);
    }
}

// ----------------------------------------------------------------------
// BattleFx Implementation
// ----------------------------------------------------------------------
bool BattleFx::init() {
    GagAtlas::instance().init();

    // Load prop models from SDMC
    static const struct {
        const char* path;
        render::Model* model;
    } kModels[] = {
        {"sdmc:/3ds/toontown3ds/data/props/tart.ttm", &tartModel_},
        {"sdmc:/3ds/toontown3ds/data/props/fruitpie-slice.ttm", &fruitpieModel_},
        {"sdmc:/3ds/toontown3ds/data/props/creampie-slice.ttm", &creampieModel_},
        {"sdmc:/3ds/toontown3ds/data/props/splat_full.ttm", &splatModel_},
        {"sdmc:/3ds/toontown3ds/data/props/squirting-flower.ttm", &flowerModel_},
        {"sdmc:/3ds/toontown3ds/data/props/water-gun.ttm", &watergunModel_},
        {"sdmc:/3ds/toontown3ds/data/props/bottle.ttm", &bottleModel_},
        {"sdmc:/3ds/toontown3ds/data/props/splash.ttm", &splashModel_},
        {"sdmc:/3ds/toontown3ds/data/props/megaphone.ttm", &megaphoneModel_},
        {"sdmc:/3ds/toontown3ds/data/props/button.ttm", &buttonModel_},
        {"sdmc:/3ds/toontown3ds/data/props/feather_full.ttm", &featherModel_},
        {"sdmc:/3ds/toontown3ds/data/props/lipstick.ttm", &lipstickModel_},
        {"sdmc:/3ds/toontown3ds/data/props/phone.ttm", &phoneModel_},
        {"sdmc:/3ds/toontown3ds/data/props/receiver.ttm", &receiverModel_},
        {"sdmc:/3ds/toontown3ds/data/props/explosion.ttm", &explosionModel_},
    };

    for (const auto& item : kModels) {
        if (!item.model->load(item.path)) {
            TT_LOG("battle_fx: could not load %s", item.path);
        }
    }

    splatAnim_.load("sdmc:/3ds/toontown3ds/data/props/splat_full_action.tta");
    featherAnim_.load("sdmc:/3ds/toontown3ds/data/props/feather_full_action.tta");

    movies_.init();
    // [cog attacks]
    cogAttacks_.init();

    modelsLoaded_ = true;
    reset();
    return true;
}

void BattleFx::shutdown() {
    reset();
    movies_.shutdown();
    // [cog attacks]
    cogAttacks_.shutdown();
    tartModel_.unload();
    fruitpieModel_.unload();
    creampieModel_.unload();
    splatModel_.unload();
    splatAnim_.unload();
    flowerModel_.unload();
    watergunModel_.unload();
    bottleModel_.unload();
    splashModel_.unload();
    megaphoneModel_.unload();
    buttonModel_.unload();
    featherModel_.unload();
    featherAnim_.unload();
    lipstickModel_.unload();
    phoneModel_.unload();
    receiverModel_.unload();
    explosionModel_.unload();

    for (int i = 0; i < kMaxJntSlots; ++i) jntSlots_[i].track.unload();
    for (int i = 0; i < kMaxSfxSlots; ++i) {
        if (sfxSlots_[i].id != audio::kInvalidSfx) {
            audio::freeSfx(sfxSlots_[i].id);
            sfxSlots_[i].id = audio::kInvalidSfx;
        }
    }
    GagAtlas::instance().shutdown();
    modelsLoaded_ = false;
}

void BattleFx::reset() {
    movies_.reset();
    // [cog attacks]
    cogAttacks_.reset();
    for (int i = 0; i < kMaxProps; ++i) props_[i].active = false;
    for (int i = 0; i < kMaxProjectiles; ++i) projectiles_[i].active = false;
    for (int i = 0; i < kMaxSplats; ++i) splats_[i].active = false;
    for (int i = 0; i < kMaxKapows; ++i) kapows_[i].active = false;
}

render::Model* BattleFx::getPropModel(int track, int level) {
    if (render::Model* m = movies_.getPropModel(track, level)) {
        return m;
    }
    if (track == 4) { // Throw
        if (level == 0) return &tartModel_;
        if (level == 1) return &fruitpieModel_;
        return &creampieModel_;
    } else if (track == 5) { // Squirt
        if (level == 0) return &flowerModel_;
        if (level == 2) return &watergunModel_;
        if (level >= 3) return &bottleModel_;
        return &watergunModel_;
    } else if (track == 3) { // Sound
        return &megaphoneModel_;
    } else if (track == 6) { // Drop
        return &buttonModel_;
    } else if (track == 0) { // Heal
        if (level >= 2) return &lipstickModel_;
        return &featherModel_;
    }
    return &tartModel_;
}

JntTrack* BattleFx::getJnt(const char* path) {
    int best = -1;
    for (int i = 0; i < kMaxJntSlots; ++i) {
        if (jntSlots_[i].track.loaded() && !strcmp(jntSlots_[i].path, path)) {
            jntSlots_[i].lastUsed = nowMs_;
            return &jntSlots_[i].track;
        }
        if (best == -1 || (!jntSlots_[i].track.loaded() && jntSlots_[best].track.loaded()) ||
            jntSlots_[i].lastUsed < jntSlots_[best].lastUsed) {
            best = i;
        }
    }
    if (best < 0) return nullptr;
    jntSlots_[best].track.unload();
    strncpy(jntSlots_[best].path, path, sizeof(jntSlots_[best].path) - 1);
    if (!jntSlots_[best].track.load(path)) {
        return nullptr;
    }
    jntSlots_[best].lastUsed = nowMs_;
    return &jntSlots_[best].track;
}

void BattleFx::playCue(const char* name, float volume) {
    int best = -1;
    for (int i = 0; i < kMaxSfxSlots; ++i) {
        if (sfxSlots_[i].id != audio::kInvalidSfx && !strcmp(sfxSlots_[i].name, name)) {
            sfxSlots_[i].lastUsed = nowMs_;
            sfxSlots_[i].voice = audio::playSfx(sfxSlots_[i].id, volume);
            return;
        }
        if (best == -1) {
            best = i;
        } else if (sfxSlots_[i].id == audio::kInvalidSfx && sfxSlots_[best].id != audio::kInvalidSfx) {
            best = i;
        } else if (!audio::voicePlaying(sfxSlots_[i].voice) && audio::voicePlaying(sfxSlots_[best].voice)) {
            best = i;
        } else if (sfxSlots_[i].lastUsed < sfxSlots_[best].lastUsed) {
            best = i;
        }
    }

    if (best >= 0) {
        if (sfxSlots_[best].id != audio::kInvalidSfx) {
            audio::freeSfx(sfxSlots_[best].id);
            sfxSlots_[best].id = audio::kInvalidSfx;
        }
        char path[80];
        snprintf(path, sizeof(path), "sdmc:/3ds/toontown3ds/data/audio/%s.tsnd", name);
        sfxSlots_[best].id = audio::loadSfx(path);
        if (sfxSlots_[best].id != audio::kInvalidSfx) {
            strncpy(sfxSlots_[best].name, name, sizeof(sfxSlots_[best].name) - 1);
            sfxSlots_[best].lastUsed = nowMs_;
            sfxSlots_[best].voice = audio::playSfx(sfxSlots_[best].id, volume);
        }
    }
}

void BattleFx::onAttack(uint32_t attackerId, uint32_t targetId, int track, int level,
                        uint32_t startMs, uint32_t durMs, uint32_t hitMs, const Battle* battle) {
    if (track < 0) {
        // [cog attacks] Cog attack movie & prop playback
        cogAttacks_.onAttack(attackerId, targetId, level, startMs, durMs, hitMs, battle);
        return;
    }
    if (battle) {
        movies_.onAttack(attackerId, targetId, track, level, startMs, durMs, hitMs, *this, *battle);
    }
    const MovieTiming timing = getMovieTiming(track, level);

    // Spawn held prop for the attacker
    for (int i = 0; i < kMaxProps; ++i) {
        if (!props_[i].active) {
            props_[i].active = true;
            props_[i].actorId = attackerId;
            props_[i].isToon = (track >= 0);
            props_[i].track = track;
            props_[i].level = level;
            props_[i].startMs = startMs;
            props_[i].releaseMs = startMs + timing.releaseMs;
            props_[i].endMs = startMs + durMs;
            props_[i].targetId = targetId;
            break;
        }
    }

    // Throw: queue projectile from releaseMs to hitMs
    if (track == 4) {
        for (int i = 0; i < kMaxProjectiles; ++i) {
            if (!projectiles_[i].active) {
                projectiles_[i].active = true;
                projectiles_[i].track = track;
                projectiles_[i].level = level;
                projectiles_[i].targetId = targetId;
                projectiles_[i].attackerId = attackerId;
                projectiles_[i].arc.startMs = startMs + timing.releaseMs;
                projectiles_[i].arc.hitMs = hitMs;
                projectiles_[i].arc.maxHeight = 2.8f;
                projectiles_[i].soundPlayed = false;
                break;
            }
        }
    } else if (track == 5) {
        // Squirt stream projectile
        for (int i = 0; i < kMaxProjectiles; ++i) {
            if (!projectiles_[i].active) {
                projectiles_[i].active = true;
                projectiles_[i].track = track;
                projectiles_[i].level = level;
                projectiles_[i].targetId = targetId;
                projectiles_[i].attackerId = attackerId;
                projectiles_[i].arc.startMs = startMs + timing.releaseMs;
                projectiles_[i].arc.hitMs = hitMs;
                projectiles_[i].arc.maxHeight = 0.5f; // direct spray stream
                projectiles_[i].soundPlayed = false;
                break;
            }
        }
    }
}

void BattleFx::onHit(uint32_t targetId, uint32_t attackerId, int track, int level, int hp, bool miss, const Battle* battle) {
    if (track < 0) {
        // [cog attacks]
        cogAttacks_.onHit(targetId, attackerId, level, hp, miss, battle);
        if (!miss) playCue("toon_hit");
        return;
    }
    if (battle) {
        movies_.onHit(targetId, attackerId, track, level, hp, miss, *this, *battle);
    }
    (void)attackerId;
    (void)hp;

    // Trigger audio
    if (track == 4) { // Throw
        if (miss) {
            playCue("throw_miss");
        } else if (level == 0) {
            playCue("throw_tart_hit");
        } else {
            playCue("throw_slice_hit");
        }
    } else if (track == 5) { // Squirt: the spray already played at release; a miss has its own cue
        if (miss) playCue(level <= 0 ? "squirt_flower_miss" : level == 1 ? "squirt_glass_miss" : "squirt_gun_miss");
    } else if (track < 0) { // Cog attack hit toon
        if (!miss) playCue("toon_hit");
    }

    // Spawn splat/splash on hit
    if (!miss && (track == 4 || track == 5)) {
        for (int i = 0; i < kMaxSplats; ++i) {
            if (!splats_[i].active) {
                splats_[i].active = true;
                splats_[i].targetId = targetId;
                splats_[i].track = track;
                splats_[i].level = level;
                splats_[i].startMs = nowMs_;
                splats_[i].durMs = 500;
                splats_[i].isSplash = (track == 5);
                break;
            }
        }
    }
}

void BattleFx::onDie(uint32_t cogId, uint32_t startMs) {
    // Cog lose animation plays for 5.4s, then kapow at startMs + 5400
    for (int i = 0; i < kMaxKapows; ++i) {
        if (!kapows_[i].active) {
            kapows_[i].active = true;
            kapows_[i].cogId = cogId;
            kapows_[i].startMs = startMs + 5400;
            kapows_[i].durMs = 600;
            kapows_[i].playedAudio = false;
            kapows_[i].playedSpin = false;
            break;
        }
    }
}

void BattleFx::onHeal(uint32_t targetId, uint32_t healerId, int hp) {
    (void)targetId;
    (void)healerId;
    (void)hp;
}

void BattleFx::update(float dt, uint32_t nowMs, const Battle* battle, const ToonLook* localLook) {
    (void)dt;
    (void)localLook;
    nowMs_ = nowMs;

    if (battle) {
        movies_.update(dt, nowMs, *battle, *this);
    }

    // Update active props
    for (int i = 0; i < kMaxProps; ++i) {
        if (props_[i].active) {
            if (nowMs >= props_[i].releaseMs && (props_[i].track == 4 || props_[i].track == 5)) {
                props_[i].active = false; // pie/squirt released
            } else if (nowMs >= props_[i].endMs) {
                props_[i].active = false;
            }
        }
    }

    // Update projectiles
    for (int i = 0; i < kMaxProjectiles; ++i) {
        ActiveProjectile& p = projectiles_[i];
        if (p.active) {
            // Fill in start and target positions from battle actors if needed
            if (battle && (p.arc.x0 == 0.0f && p.arc.y0 == 0.0f)) {
                float ax = 0, ay = 0, az = 0, ah = 0;
                float tx = 0, ty = 0, tz = 0, th = 0;
                if (battle->actorPos(p.attackerId, &ax, &ay, &az, &ah)) {
                    p.arc.x0 = ax; p.arc.y0 = ay; p.arc.z0 = az + 2.5f;
                }
                if (battle->actorPos(p.targetId, &tx, &ty, &tz, &th)) {
                    p.arc.x1 = tx; p.arc.y1 = ty; p.arc.z1 = tz + 3.0f;
                }
            }

            if (!p.soundPlayed && nowMs >= p.arc.startMs) {
                p.soundPlayed = true;
                if (p.track == 4) playCue("pie_throw");
                else if (p.track == 5) {
                    // MovieSquirt plays the spray as the water leaves, not only on the hit frame.
                    playCue(p.level <= 0 ? "squirt_flower_hit" : p.level == 1 ? "squirt_glass_hit" : "squirt_gun_hit");
                }
            }

            if (p.arc.done(nowMs)) {
                p.active = false;
            }
        }
    }

    // Update splats
    for (int i = 0; i < kMaxSplats; ++i) {
        if (splats_[i].active) {
            if (battle && splats_[i].targetId) {
                float tx = 0, ty = 0, tz = 0, th = 0;
                if (battle->actorPos(splats_[i].targetId, &tx, &ty, &tz, &th)) {
                    splats_[i].x = tx; splats_[i].y = ty; splats_[i].z = tz; splats_[i].h = th;
                }
            }
            if (nowMs >= splats_[i].startMs + splats_[i].durMs) {
                splats_[i].active = false;
            }
        }
    }

    // Update kapows
    for (int i = 0; i < kMaxKapows; ++i) {
        if (kapows_[i].active) {
            if (battle && kapows_[i].cogId) {
                float cx = 0, cy = 0, cz = 0, ch = 0;
                if (battle->actorPos(kapows_[i].cogId, &cx, &cy, &cz, &ch)) {
                    kapows_[i].x = cx; kapows_[i].y = cy; kapows_[i].z = cz; kapows_[i].h = ch;
                    kapows_[i].height = battle->actorHeight(kapows_[i].cogId);
                }
            }
            // MovieUtil.createSuitDeathTrack: Cog_Death spins from 0.8 s into the lose clip,
            // ENC_cogfall_apart follows at 5.0 s, and the burst shows at 5.4 s.
            if (nowMs + 4600 >= kapows_[i].startMs && !kapows_[i].playedSpin) {
                kapows_[i].playedSpin = true;
                playCue("Cog_Death", 0.8f);
            }
            if (nowMs + 400 >= kapows_[i].startMs && !kapows_[i].playedAudio) {
                kapows_[i].playedAudio = true;
                playCue("ENC_cogfall_apart", 0.32f);
            }
            if (nowMs >= kapows_[i].startMs + kapows_[i].durMs) {
                kapows_[i].active = false;
            }
        }
    }

    // [cog attacks]
    cogAttacks_.update(dt, nowMs, battle);
}

void BattleFx::drawExplosion(render::Renderer& renderer, float x, float y, float z) const {
    if (!explosionModel_.loaded()) return;
    // setBillboardPointEye: the card's +Y points from the eye through the burst.
    const C3D_FVec& eye = renderer.eye();
    const float dx = x - eye.x, dy = y - eye.y, dz = z - eye.z;
    const float flat = sqrtf(dx * dx + dy * dy);
    C3D_Mtx m;
    Mtx_Identity(&m);
    Mtx_Translate(&m, x, y, z, true);
    Mtx_RotateZ(&m, atan2f(-dx, dy), true);
    Mtx_RotateX(&m, atan2f(dz, flat), true);
    Mtx_Scale(&m, 0.4f, 0.4f, 0.4f);
    renderer.drawModel(const_cast<render::Model&>(explosionModel_), m);
}

void BattleFx::draw(render::Renderer& renderer, const Battle* battle, const ToonLook* localLook) {
    if (!modelsLoaded_) return;

    if (battle) {
        movies_.draw(renderer, *battle, *this);
        // [cog attacks]
        cogAttacks_.draw(renderer, battle);
    }

    // 1. Draw active projectiles in flight
    for (int i = 0; i < kMaxProjectiles; ++i) {
        const ActiveProjectile& p = projectiles_[i];
        if (p.active && nowMs_ >= p.arc.startMs && !p.arc.done(nowMs_)) {
            float px = 0, py = 0, pz = 0, heading = 0, pitch = 0;
            p.arc.sample(nowMs_, &px, &py, &pz, &heading, &pitch);

            C3D_Mtx m;
            Mtx_Identity(&m);
            Mtx_Translate(&m, px, py, pz, true);
            Mtx_RotateZ(&m, heading * kDegToRad, true);
            Mtx_RotateX(&m, pitch * kDegToRad, true);
            const float roll = p.arc.alpha(nowMs_) * 360.0f * 3.0f;
            Mtx_RotateY(&m, roll * kDegToRad, true);

            render::Model* mod = getPropModel(p.track, p.level);
            if (mod && mod->loaded()) renderer.drawModel(*mod, m);
        }
    }

    // 2. Draw active splats and splashes
    for (int i = 0; i < kMaxSplats; ++i) {
        const ActiveSplat& s = splats_[i];
        if (s.active && nowMs_ >= s.startMs) {
            const float age = float(nowMs_ - s.startMs) / 1000.0f;
            const SplatConfig cfg = getSplatConfig(s.track, s.level);

            C3D_Mtx m;
            Mtx_Identity(&m);
            Mtx_Translate(&m, s.x, s.y, s.z + 2.5f, true);
            Mtx_RotateZ(&m, s.h * kDegToRad, true);
            Mtx_Scale(&m, cfg.scale, cfg.scale, cfg.scale);

            if (s.isSplash) {
                renderer.setBaseTint(cfg.r, cfg.g, cfg.b, cfg.a);
                if (splashModel_.loaded()) renderer.drawModel(splashModel_, m);
                renderer.setBaseTint(1.0f, 1.0f, 1.0f, 1.0f);
            } else if (splatModel_.loaded() && splatAnim_.loaded()) {
                u32 a = 0, b = 0;
                float blend = 0.0f;
                render::sampleClip(splatAnim_, age, &a, &b, &blend);
                renderer.setBaseTint(cfg.r, cfg.g, cfg.b, cfg.a);
                renderer.drawMorph(splatModel_, splatAnim_, a, b, blend, m);
                renderer.setBaseTint(1.0f, 1.0f, 1.0f, 1.0f);
            }
        }
    }

    // 3. Cog-death bursts: MovieUtil.createKapowExplosionTrack, explosion.bam at scale 0.4,
    // billboarded to the eye, at the Cog's height - 0.2, for 0.6 s.
    for (int i = 0; i < kMaxKapows; ++i) {
        const ActiveKapow& k = kapows_[i];
        if (k.active && nowMs_ >= k.startMs) drawExplosion(renderer, k.x, k.y, k.z + (k.height > 0 ? k.height - 0.2f : 3.0f));
    }

    // 4. Draw held props
    for (int i = 0; i < kMaxProps; ++i) {
        const ActiveProp& pr = props_[i];
        if (!pr.active || nowMs_ < pr.startMs || nowMs_ >= pr.releaseMs) continue;

        float ax = 0, ay = 0, az = 0, ah = 0;
        bool isToon = true;
        if (battle && battle->actorPos(pr.actorId, &ax, &ay, &az, &ah, &isToon)) {
            C3D_Mtx world;
            Mtx_Identity(&world);
            Mtx_Translate(&world, ax, ay, az, true);
            Mtx_RotateZ(&world, ah * kDegToRad, true);

            // Toons: ToonAvatar's torso frame x the clip's JNT right-hand joint (Toon
            // def_joint_right_hold, as upstream parents gag props to toon.rightHands).
            const ToonAvatar* av = isToon && toonLookup_ ? toonLookup_(toonLookupUser_, pr.actorId) : nullptr;
            ToonAvatar::TorsoFrame torso;
            if (av && av->torsoFrame(&torso) && torso.torsoKey) {
                char path[96];
                snprintf(path, sizeof(path), "sdmc:/3ds/toontown3ds/data/toon/%s_%s.jnt", torso.torsoKey,
                         kToonAnimNames[torso.anim]);
                const JntTrack* jnt = getJnt(path);
                C3D_Mtx hand;
                const int right = jnt ? jnt->joint("def_joint_right_hold") : -1;
                if (jnt && right >= 0 && jnt->sample(right, torso.time, &hand)) {
                    C3D_Mtx propWorld;
                    Mtx_Multiply(&propWorld, &torso.world, &hand);
                    render::Model* mod = getPropModel(pr.track, pr.level);
                    if (mod && mod->loaded()) renderer.drawModel(*mod, propWorld);
                }
            }
            (void)world;
        }
    }
    (void)localLook;
}


#endif // __3DS__

} // namespace tt::world
