#include "world/battle_movies.h"

#include <cmath>
#include <cstring>
#include "core/log.h"
#include "world/battle.h"
#include "world/battle_fx.h"
#include "render/renderer.h"

namespace tt::world {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegToRad = kPi / 180.0f;
constexpr float kSuitLureDistance = 2.6f;
constexpr float kSuitTrapDistance = 2.6f;

inline float clamp(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

inline float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

static const char* trackName(int track) {
    static const char* const kNames[] = {"heal", "trap", "lure", "sound", "throw", "squirt", "drop"};
    return (track >= 0 && track < 7) ? kNames[track] : "unknown";
}

} // namespace

bool BattleMovies::init() {
    // Load trap props
    bananaModel_.load("sdmc:/3ds/toontown3ds/data/props/banana_full.ttm");
    rakeModel_.load("sdmc:/3ds/toontown3ds/data/props/rake_full.ttm");
    marblesModel_.load("sdmc:/3ds/toontown3ds/data/props/marbles_full.ttm");
    quicksandModel_.load("sdmc:/3ds/toontown3ds/data/props/quicksand.ttm");
    trapdoorModel_.load("sdmc:/3ds/toontown3ds/data/props/trapdoor.ttm");
    tntModel_.load("sdmc:/3ds/toontown3ds/data/props/tnt_full.ttm");
    traintrackModel_.load("sdmc:/3ds/toontown3ds/data/props/traintrack.ttm");

    // Load lure props
    dollarModel_.load("sdmc:/3ds/toontown3ds/data/props/1dollar_full.ttm");
    bigMagnetModel_.load("sdmc:/3ds/toontown3ds/data/props/big-magnet.ttm");
    hypnoGogglesModel_.load("sdmc:/3ds/toontown3ds/data/props/hypno-goggles_full.ttm");
    slideshowModel_.load("sdmc:/3ds/toontown3ds/data/props/slideshow.ttm");

    // Load sound props
    bikehornModel_.load("sdmc:/3ds/toontown3ds/data/props/bikehorn.ttm");
    whistleModel_.load("sdmc:/3ds/toontown3ds/data/props/whistle.ttm");
    bugleModel_.load("sdmc:/3ds/toontown3ds/data/props/bugle.ttm");
    aoogahModel_.load("sdmc:/3ds/toontown3ds/data/props/aoogah.ttm");
    elephantModel_.load("sdmc:/3ds/toontown3ds/data/props/elephant.ttm");
    foghornModel_.load("sdmc:/3ds/toontown3ds/data/props/fog_horn.ttm");
    singingModel_.load("sdmc:/3ds/toontown3ds/data/props/singing.ttm");

    // Load drop props
    flowerpotModel_.load("sdmc:/3ds/toontown3ds/data/props/flowerpot_full.ttm");
    sandbagModel_.load("sdmc:/3ds/toontown3ds/data/props/sandbag_full.ttm");
    anvilModel_.load("sdmc:/3ds/toontown3ds/data/props/anvil_full.ttm");
    weightModel_.load("sdmc:/3ds/toontown3ds/data/props/weight_full.ttm");
    safeModel_.load("sdmc:/3ds/toontown3ds/data/props/safe_full.ttm");
    pianoModel_.load("sdmc:/3ds/toontown3ds/data/props/piano_full.ttm");
    shipModel_.load("sdmc:/3ds/toontown3ds/data/props/ship.ttm");
    dropShadowModel_.load("romfs:/baked/drop_shadow.ttm");

    // Load heal props
    featherModel_.load("sdmc:/3ds/toontown3ds/data/props/feather_full.ttm");
    megaphoneModel_.load("sdmc:/3ds/toontown3ds/data/props/megaphone.ttm");
    lipstickModel_.load("sdmc:/3ds/toontown3ds/data/props/lipstick.ttm");
    caneModel_.load("sdmc:/3ds/toontown3ds/data/props/cane.ttm");
    hatModel_.load("sdmc:/3ds/toontown3ds/data/props/hat.ttm");
    cubesModel_.load("sdmc:/3ds/toontown3ds/data/props/cubes_full.ttm");
    ladderModel_.load("sdmc:/3ds/toontown3ds/data/props/ladder.ttm");

    // Load SOS props
    phoneModel_.load("sdmc:/3ds/toontown3ds/data/props/phone.ttm");
    receiverModel_.load("sdmc:/3ds/toontown3ds/data/props/receiver.ttm");

    modelsLoaded_ = true;
    reset();
    return true;
}

void BattleMovies::shutdown() {
    reset();
    bananaModel_.unload();
    rakeModel_.unload();
    marblesModel_.unload();
    quicksandModel_.unload();
    trapdoorModel_.unload();
    tntModel_.unload();
    traintrackModel_.unload();

    dollarModel_.unload();
    bigMagnetModel_.unload();
    hypnoGogglesModel_.unload();
    slideshowModel_.unload();

    bikehornModel_.unload();
    whistleModel_.unload();
    bugleModel_.unload();
    aoogahModel_.unload();
    elephantModel_.unload();
    foghornModel_.unload();
    singingModel_.unload();

    flowerpotModel_.unload();
    sandbagModel_.unload();
    anvilModel_.unload();
    weightModel_.unload();
    safeModel_.unload();
    pianoModel_.unload();
    shipModel_.unload();
    dropShadowModel_.unload();

    featherModel_.unload();
    megaphoneModel_.unload();
    lipstickModel_.unload();
    caneModel_.unload();
    hatModel_.unload();
    cubesModel_.unload();
    ladderModel_.unload();

    phoneModel_.unload();
    receiverModel_.unload();

    modelsLoaded_ = false;
}

void BattleMovies::reset() {
    for (int i = 0; i < kMaxTraps; ++i) traps_[i] = BattleTrapState{};
    for (int i = 0; i < kMaxDrops; ++i) drops_[i] = BattleDropState{};
    for (int i = 0; i < kMaxLures; ++i) lures_[i] = BattleLureState{};
}

void BattleMovies::placeTrap(uint32_t suitId, int level, uint32_t nowMs, const Battle& battle) {
    int slot = -1;
    for (int i = 0; i < kMaxTraps; ++i) {
        if (traps_[i].active && traps_[i].suitId == suitId) {
            slot = i;
            break;
        }
        if (slot == -1 && !traps_[i].active) {
            slot = i;
        }
    }
    if (slot < 0) slot = 0;

    float sx = 0, sy = 0, sz = 0, sh = 0;
    battle.actorPos(suitId, &sx, &sy, &sz, &sh);

    // Place trap directly in front of the Cog at kSuitTrapDistance
    const float rad = sh * kDegToRad;
    const float dist = (level == 1) ? 3.5f : kSuitTrapDistance; // rake slightly further out

    BattleTrapState& t = traps_[slot];
    t.active = true;
    t.level = level;
    t.suitId = suitId;
    t.x = sx - std::sin(rad) * dist;
    t.y = sy + std::cos(rad) * dist;
    t.z = sz + 0.05f;
    t.h = sh;
    t.placedMs = nowMs;
    t.triggered = false;
    t.triggerStartMs = 0;
    t.triggerDurMs = 0;

    TT_LOG("battle_movies: place trap lvl %d on suit %u", level, unsigned(suitId));
}

bool BattleMovies::triggerTrap(uint32_t suitId, uint32_t nowMs, BattleFx& fx) {
    for (int i = 0; i < kMaxTraps; ++i) {
        if (traps_[i].active && traps_[i].suitId == suitId && !traps_[i].triggered) {
            traps_[i].triggered = true;
            traps_[i].triggerStartMs = nowMs;
            traps_[i].triggerDurMs = 2000;

            // Play appropriate trap trigger audio cue
            switch (traps_[i].level) {
            case 0: // banana
                fx.playCue("toon_bodyfall", 1.0f);
                break;
            case 1: // rake
                fx.playCue("trap_rake_hit", 1.0f);
                break;
            case 2: // marbles
                fx.playCue("toon_bodyfall", 1.0f);
                break;
            case 3: // quicksand
                fx.playCue("trap_quicksand", 1.0f);
                break;
            case 4: // trap door
                fx.playCue("trap_door", 1.0f);
                break;
            case 5: // tnt
                fx.playCue("ENC_cogfall_apart", 1.0f);
                break;
            case 6: // train
                fx.playCue("trap_train_hit", 1.0f);
                break;
            default:
                break;
            }
            return true;
        }
    }
    return false;
}

void BattleMovies::clearTrap(uint32_t suitId) {
    for (int i = 0; i < kMaxTraps; ++i) {
        if (traps_[i].active && traps_[i].suitId == suitId) {
            traps_[i].active = false;
        }
    }
}

bool BattleMovies::hasTrap(uint32_t suitId) const {
    for (int i = 0; i < kMaxTraps; ++i) {
        if (traps_[i].active && traps_[i].suitId == suitId && !traps_[i].triggered) {
            return true;
        }
    }
    return false;
}

int BattleMovies::trapLevel(uint32_t suitId) const {
    for (int i = 0; i < kMaxTraps; ++i) {
        if (traps_[i].active && traps_[i].suitId == suitId && !traps_[i].triggered) {
            return traps_[i].level;
        }
    }
    return -1;
}

const BattleTrapState* BattleMovies::getTrap(uint32_t suitId) const {
    for (int i = 0; i < kMaxTraps; ++i) {
        if (traps_[i].active && traps_[i].suitId == suitId) {
            return &traps_[i];
        }
    }
    return nullptr;
}

void BattleMovies::setLured(uint32_t suitId, bool lured, uint32_t nowMs) {
    int slot = -1;
    for (int i = 0; i < kMaxLures; ++i) {
        if (lures_[i].suitId == suitId) {
            slot = i;
            break;
        }
        if (slot == -1 && !lures_[i].active) {
            slot = i;
        }
    }
    if (slot < 0) slot = 0;

    BattleLureState& l = lures_[slot];
    l.active = true;
    l.suitId = suitId;
    l.targetOffset = lured ? kSuitLureDistance : 0.0f;
    l.walkStartMs = nowMs;
    l.walkDurMs = 600;

    TT_LOG("battle_movies: suit %u %s", unsigned(suitId), lured ? "lured" : "unlured");
}

float BattleMovies::getLureOffset(uint32_t suitId) const {
    for (int i = 0; i < kMaxLures; ++i) {
        if (lures_[i].active && lures_[i].suitId == suitId) {
            return lures_[i].currOffset;
        }
    }
    return 0.0f;
}

void BattleMovies::spawnDrop(uint32_t suitId, int level, uint32_t startMs, uint32_t impactMs,
                             bool hit, int hp, const Battle& battle) {
    int slot = -1;
    for (int i = 0; i < kMaxDrops; ++i) {
        if (!drops_[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) slot = 0;

    float sx = 0, sy = 0, sz = 0, sh = 0;
    battle.actorPos(suitId, &sx, &sy, &sz, &sh);

    BattleDropState& d = drops_[slot];
    d.active = true;
    d.level = level;
    d.suitId = suitId;
    d.startMs = startMs;
    d.shadowMs = startMs + 1300;
    d.fallMs = (impactMs > 700) ? (impactMs - 700) : startMs;
    d.impactMs = impactMs;
    d.hit = hit;
    d.hp = hp;
    d.whistlePlayed = false;
    d.impactPlayed = false;
    d.targetX = sx;
    d.targetY = sy;
    d.targetZ = sz;

    TT_LOG("battle_movies: spawn drop lvl %d on suit %u", level, unsigned(suitId));
}

void BattleMovies::onAttack(uint32_t attackerId, uint32_t targetId, int track, int level,
                            uint32_t startMs, uint32_t durMs, uint32_t hitMs,
                            BattleFx& fx, const Battle& battle) {
    (void)attackerId;
    (void)durMs;

    TT_LOG("battle_movies: attack track %s (lvl %d) attacker %u target %u",
           trackName(track), level, unsigned(attackerId), unsigned(targetId));

    switch (track) {
    case 0: { // Heal
        static const char* const kHealSfx[] = {
            "heal_tickle", "heal_telljoke", "heal_smooch",
            "heal_happydance", "heal_pixiedust", "heal_juggle", "heal_dive"
        };
        const int idx = clamp(level, 0, 6);
        fx.playCue(kHealSfx[idx], 1.0f);
        break;
    }
    case 1: { // Trap
        static const char* const kTrapSfx[] = {
            "trap_banana", "trap_rake", "trap_marbles",
            "trap_quicksand", "trap_door", "trap_tnt", "trap_train"
        };
        const int idx = clamp(level, 0, 6);
        fx.playCue(kTrapSfx[idx], 1.0f);
        // Trap is placed at target Cog's feet
        placeTrap(targetId, level, hitMs, battle);
        break;
    }
    case 2: { // Lure
        static const char* const kLureSfx[] = {
            "lure_bill", "lure_small_magnet", "lure_bill",
            "lure_large_magnet", "lure_bill", "lure_hypno", "lure_hypno"
        };
        const int idx = clamp(level, 0, 6);
        fx.playCue(kLureSfx[idx], 1.0f);
        break;
    }
    case 3: { // Sound
        static const char* const kSoundSfx[] = {
            "sound_bikehorn", "sound_whistle", "sound_bugle",
            "sound_aoogah", "sound_elephant", "sound_foghorn", "sound_opera"
        };
        const int idx = clamp(level, 0, 6);
        fx.playCue(kSoundSfx[idx], 1.0f);
        break;
    }
    case 6: { // Drop
        fx.playCue("drop_button", 0.9f);
        spawnDrop(targetId, level, startMs, hitMs, true, 0, battle);
        break;
    }
    default:
        break;
    }
}

void BattleMovies::onHit(uint32_t targetId, uint32_t attackerId, int track, int level,
                         int hp, bool miss, BattleFx& fx, const Battle& battle) {
    (void)attackerId;
    (void)battle;

    TT_LOG("battle_movies: hit track %s (lvl %d) target %u hp %d miss %d",
           trackName(track), level, unsigned(targetId), hp, int(miss));

    if (track == 2) { // Lure
        if (!miss) {
            setLured(targetId, true, nowMs_);
            if (hasTrap(targetId)) {
                triggerTrap(targetId, nowMs_, fx);
            }
        }
    } else if (track == 6) { // Drop
        // Play hit or miss sound
        static const char* const kHitSfx[] = {
            "drop_flowerpot", "drop_sandbag", "drop_anvil",
            "drop_bigweight", "drop_safe", "drop_piano", "drop_boat"
        };
        static const char* const kMissSfx[] = {
            "drop_flowerpot_miss", "drop_sandbag_miss", "drop_anvil_miss",
            "drop_bigweight_miss", "drop_safe_miss", "drop_piano_miss", "drop_boat_miss"
        };
        const int idx = clamp(level, 0, 6);
        fx.playCue(miss ? kMissSfx[idx] : kHitSfx[idx], 1.0f);

        for (int i = 0; i < kMaxDrops; ++i) {
            if (drops_[i].active && drops_[i].suitId == targetId) {
                drops_[i].hit = !miss;
                drops_[i].hp = hp;
                drops_[i].impactPlayed = true;
            }
        }
        if (!miss) setLured(targetId, false, nowMs_); // Drop damage unlures Cog
    } else if (track == 3 || track == 4 || track == 5) {
        // Sound, Throw, Squirt hits unlure the Cog
        if (!miss) setLured(targetId, false, nowMs_);
    }
}

void BattleMovies::update(float dt, uint32_t nowMs, const Battle& battle, BattleFx& fx) {
    (void)dt;
    (void)battle;
    nowMs_ = nowMs;

    // Update lure walking offsets
    for (int i = 0; i < kMaxLures; ++i) {
        if (!lures_[i].active) continue;
        const uint32_t elapsed = nowMs >= lures_[i].walkStartMs ? (nowMs - lures_[i].walkStartMs) : 0;
        const float alpha = clamp(float(elapsed) / float(lures_[i].walkDurMs), 0.0f, 1.0f);
        lures_[i].currOffset = lerp(lures_[i].currOffset, lures_[i].targetOffset, alpha);
    }

    // Update drops whistle sound
    for (int i = 0; i < kMaxDrops; ++i) {
        if (!drops_[i].active) continue;
        if (!drops_[i].whistlePlayed && nowMs >= drops_[i].startMs + 2400) {
            drops_[i].whistlePlayed = true;
            fx.playCue("drop_whistle", 0.9f);
        }
        if (nowMs >= drops_[i].impactMs + 500) {
            drops_[i].active = false;
        }
    }

    // Update trap triggers
    for (int i = 0; i < kMaxTraps; ++i) {
        if (!traps_[i].active) continue;
        if (traps_[i].triggered && nowMs >= traps_[i].triggerStartMs + traps_[i].triggerDurMs) {
            traps_[i].active = false;
        }
    }
}

render::Model* BattleMovies::getPropModel(int track, int level) {
    const int l = clamp(level, 0, 6);
    switch (track) {
    case 0: { // Heal
        render::Model* const mods[] = {
            &featherModel_, &megaphoneModel_, &lipstickModel_,
            &caneModel_, &featherModel_, &cubesModel_, &ladderModel_
        };
        return mods[l];
    }
    case 1: { // Trap
        render::Model* const mods[] = {
            &bananaModel_, &rakeModel_, &marblesModel_,
            &quicksandModel_, &trapdoorModel_, &tntModel_, &traintrackModel_
        };
        return mods[l];
    }
    case 2: { // Lure
        render::Model* const mods[] = {
            &dollarModel_, &bigMagnetModel_, &dollarModel_,
            &bigMagnetModel_, &dollarModel_, &hypnoGogglesModel_, &slideshowModel_
        };
        return mods[l];
    }
    case 3: { // Sound
        render::Model* const mods[] = {
            &bikehornModel_, &whistleModel_, &bugleModel_,
            &aoogahModel_, &elephantModel_, &foghornModel_, &singingModel_
        };
        return mods[l];
    }
    case 6: { // Drop
        render::Model* const mods[] = {
            &flowerpotModel_, &sandbagModel_, &anvilModel_,
            &weightModel_, &safeModel_, &pianoModel_, &shipModel_
        };
        return mods[l];
    }
    default:
        break;
    }
    return nullptr;
}

void BattleMovies::draw(render::Renderer& renderer, const Battle& battle, BattleFx& fx) {
    (void)battle;
    if (!modelsLoaded_) return;

    // 1. Draw active persistent traps on the ground
    for (int i = 0; i < kMaxTraps; ++i) {
        const BattleTrapState& t = traps_[i];
        if (!t.active) continue;

        render::Model* mod = getPropModel(1, t.level);
        if (!mod || !mod->loaded()) continue;

        C3D_Mtx m;
        Mtx_Identity(&m);
        Mtx_Translate(&m, t.x, t.y, t.z, true);
        Mtx_RotateZ(&m, t.h * kDegToRad, true);

        if (t.triggered) {
            const float age = float(nowMs_ - t.triggerStartMs) * 0.001f;
            if (t.level == 1) { // Rake snaps up
                const float flip = clamp(age * 5.0f, 0.0f, 1.0f);
                Mtx_RotateX(&m, flip * 160.0f * kDegToRad, true);
            } else if (t.level == 5) { // TNT explosion
                if (age > 0.5f && age < 1.1f) {
                    fx.drawExplosion(renderer, t.x, t.y, t.z + 1.0f);
                }
            } else if (t.level == 0 || t.level == 2) { // Banana / marbles slide
                const float slide = clamp(age * 2.0f, 0.0f, 1.0f);
                Mtx_Translate(&m, 0.0f, -slide * 1.5f, 0.0f, true);
            }
        }

        // Custom prop scale adjustments matching upstream
        float scale = 1.0f;
        if (t.level == 1) scale = 0.7f;      // rake
        else if (t.level == 3) scale = 1.7f; // quicksand
        else if (t.level == 4) scale = 1.7f; // trapdoor
        Mtx_Scale(&m, scale, scale, scale);

        renderer.drawModel(*mod, m);
    }

    // 2. Draw falling drop objects and shadows
    for (int i = 0; i < kMaxDrops; ++i) {
        const BattleDropState& d = drops_[i];
        if (!d.active) continue;

        // Expanding shadow on ground beneath Cog
        if (nowMs_ >= d.shadowMs && nowMs_ <= d.impactMs + 200) {
            const float dur = float(d.impactMs - d.shadowMs);
            const float alpha = (dur > 0) ? clamp(float(nowMs_ - d.shadowMs) / dur, 0.0f, 1.0f) : 1.0f;
            const float shadowScale = lerp(0.2f, 1.4f, alpha);

            C3D_Mtx sm;
            Mtx_Identity(&sm);
            Mtx_Translate(&sm, d.targetX, d.targetY, d.targetZ + 0.05f, true);
            Mtx_Scale(&sm, shadowScale, shadowScale, shadowScale);

            if (dropShadowModel_.loaded()) {
                renderer.setBaseTint(0.0f, 0.0f, 0.0f, 0.6f);
                renderer.drawModel(dropShadowModel_, sm);
                renderer.setBaseTint(1.0f, 1.0f, 1.0f, 1.0f);
            }
        }

        // Falling prop from sky down to impact position
        if (nowMs_ >= d.fallMs && nowMs_ <= d.impactMs + 300) {
            render::Model* mod = getPropModel(6, d.level);
            if (mod && mod->loaded()) {
                const float fallDur = float(d.impactMs - d.fallMs);
                const float t = (fallDur > 0) ? clamp(float(nowMs_ - d.fallMs) / fallDur, 0.0f, 1.0f) : 1.0f;

                const float startZ = d.targetZ + 18.0f;
                const float landZ = d.hit ? (d.targetZ + 3.0f) : (d.targetZ + 0.4f);
                const float currZ = lerp(startZ, landZ, t);

                C3D_Mtx pm;
                Mtx_Identity(&pm);
                Mtx_Translate(&pm, d.targetX, d.targetY, currZ, true);
                if (d.level == 6) { // ship
                    Mtx_Scale(&pm, 0.35f, 0.35f, 0.35f);
                } else if (d.level == 5) { // piano
                    Mtx_Scale(&pm, 0.7f, 0.7f, 0.7f);
                }
                renderer.drawModel(*mod, pm);
            }
        }
    }
}

} // namespace tt::world
