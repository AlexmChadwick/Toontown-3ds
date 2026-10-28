#include "world/cog_attacks.h"
#include "world/battle.h"
#include "core/log.h"

#ifdef __3DS__

namespace tt::world {

namespace {

constexpr float kDegToRad = 0.0174532925f;

void stripOggExtension(const char* in, char* out, size_t outSize) {
    if (!in || !out || outSize == 0) return;
    snprintf(out, outSize, "%s", in);
    char* dot = strrchr(out, '.');
    if (dot && (!strcmp(dot, ".ogg") || !strcmp(dot, ".OGG"))) {
        *dot = '\0';
    }
}

} // namespace

bool CogAttacks::init() {
    reset();
    return true;
}

void CogAttacks::shutdown() {
    reset();
    assetBatch_.clear();
    for (int i = 0; i < kMaxPropSlots; ++i) {
        if (propSlots_[i].loaded) {
            propSlots_[i].model.unload();
            if (propSlots_[i].hasAnim) {
                propSlots_[i].anim.unload();
            }
            propSlots_[i].loaded = false;
        }
    }
    for (int i = 0; i < kMaxSfx; ++i) {
        if (sfx_[i].id != audio::kInvalidSfx) {
            audio::freeSfx(sfx_[i].id);
            sfx_[i].id = audio::kInvalidSfx;
        }
        sfx_[i].name[0] = '\0';
    }
}

void CogAttacks::reset() {
    for (int i = 0; i < kMaxAttacks; ++i) {
        attacks_[i].active = false;
    }
}

CogAttacks::PropSlot* CogAttacks::findOrLoadProp(const char* propName, const char* propAnim) {
    if (!propName || propName[0] == '\0') return nullptr;

    // 1. Search existing slot
    for (int i = 0; i < kMaxPropSlots; ++i) {
        if (propSlots_[i].loaded && !strcmp(propSlots_[i].name, propName)) {
            return &propSlots_[i];
        }
    }

    // 2. Find empty slot
    int freeSlot = -1;
    for (int i = 0; i < kMaxPropSlots; ++i) {
        if (!propSlots_[i].loaded) {
            freeSlot = i;
            break;
        }
    }
    if (freeSlot < 0) {
        TT_LOG("cog_attacks: prop slots full for %s", propName);
        return nullptr;
    }

    PropSlot& slot = propSlots_[freeSlot];
    snprintf(slot.name, sizeof(slot.name), "%s", propName);

    char modelPath[96];
    const bool isActor = (propAnim != nullptr && propAnim[0] != '\0');
    if (isActor) {
        snprintf(modelPath, sizeof(modelPath), "sdmc:/3ds/toontown3ds/data/props/%s_full.ttm", propName);
    } else {
        snprintf(modelPath, sizeof(modelPath), "sdmc:/3ds/toontown3ds/data/props/%s.ttm", propName);
    }

    // Background loader: a synchronous load here froze the battle face-off ~140 ms. The prop is
    // drawn once published (a missing file logs 'assets: batch failed').
    if (!assetBatch_.load(slot.model, modelPath)) {
        TT_LOG("cog_attacks: cannot queue %s", modelPath);
        return nullptr;
    }
    slot.hasAnim = false;
    if (isActor) {
        char animPath[96];
        snprintf(animPath, sizeof(animPath), "sdmc:/3ds/toontown3ds/data/props/%s_full_action.tta", propAnim);
        slot.hasAnim = assetBatch_.load(slot.anim, animPath);
    }

    slot.loaded = true;
    TT_LOG("cog_attacks: preloading prop %s (actor=%d)", propName, isActor ? 1 : 0);
    return &slot;
}

void CogAttacks::preloadForBattle(const char* const cogCodes[], int numCogs) {
    if (!cogCodes || numCogs <= 0) return;

    for (int c = 0; c < numCogs; ++c) {
        const char* code = cogCodes[c];
        if (!code || code[0] == '\0') continue;
        const int headIdx = findCogHeadTypeIndex(code);
        if (headIdx < 0) continue;

        for (int a = 0; a < 6; ++a) {
            const int atkId = getCogAttackId(headIdx, a);
            if (atkId < 0) continue;
            const CogAttackInfo* info = getCogAttackInfo(atkId);
            if (info && info->propName && info->propName[0] != '\0') {
                findOrLoadProp(info->propName, info->propAnim);
            }
        }
    }
}

void CogAttacks::onAttack(uint32_t attackerId, uint32_t targetId, int attackIdx,
                         uint32_t startMs, uint32_t durMs, uint32_t hitMs, const Battle* battle) {
    if (!battle) return;

    int freeIdx = -1;
    for (int i = 0; i < kMaxAttacks; ++i) {
        if (!attacks_[i].active) {
            freeIdx = i;
            break;
        }
    }
    if (freeIdx < 0) return;

    const char* cogCode = battle->actorCogCode(attackerId);
    const int atkId = getCogAttackId(cogCode, attackIdx);
    const CogAttackInfo* info = getCogAttackInfo(atkId);

    // [cog attacks] Required autotest log line: "cog_attacks: <name> by <cog>"
    TT_LOG("cog_attacks: %s by %s", info ? info->name : "Unknown", cogCode[0] ? cogCode : "suit");

    ActiveAttack& atk = attacks_[freeIdx];
    atk.active = true;
    atk.attackerId = attackerId;
    atk.targetId = targetId;
    atk.attackId = atkId;
    atk.startMs = startMs;
    atk.durMs = durMs;
    atk.hitMs = hitMs;
    atk.isThrow = info ? info->isThrow : false;
    atk.holdLeft = info ? info->holdLeftHand : false;
    atk.prop = (info && info->propName) ? findOrLoadProp(info->propName, info->propAnim) : nullptr;

    // Release frame
    if (atk.isThrow) {
        const uint32_t delay = (info && info->hitDelayMs > 1000) ? (info->hitDelayMs - 1000) : 1000;
        atk.releaseMs = startMs + delay;
    } else {
        atk.releaseMs = startMs + durMs;
    }

    // Sound
    atk.soundName[0] = '\0';
    atk.soundPlayed = false;
    if (info && info->soundName && info->soundName[0] != '\0') {
        snprintf(atk.soundName, sizeof(atk.soundName), "%s", info->soundName);
        atk.soundAtMs = startMs + info->soundDelayMs;
    }

    // Attacker hand location (x0, y0, z0)
    float ax = 0.0f, ay = 0.0f, az = 0.0f, ah = 0.0f;
    battle->actorPos(attackerId, &ax, &ay, &az, &ah);
    const float rad = ah * kDegToRad;
    const float forward = 1.2f;
    const float side = atk.holdLeft ? -0.8f : 0.8f;
    atk.x0 = ax + forward * sinf(rad) + side * cosf(rad);
    atk.y0 = ay + forward * cosf(rad) - side * sinf(rad);
    atk.z0 = az + 2.5f;

    // Target location (x1, y1, z1)
    if (targetId != 0 && battle->actorPos(targetId, &atk.x1, &atk.y1, &atk.z1, nullptr)) {
        atk.z1 += 1.8f; // chest height
    } else {
        battle->center(&atk.x1, &atk.y1, &atk.z1, nullptr);
        atk.z1 += 1.5f;
    }
}

void CogAttacks::onHit(uint32_t targetId, uint32_t attackerId, int attackIdx, int hp, bool miss,
                      const Battle* battle) {
    (void)targetId;
    (void)attackerId;
    (void)attackIdx;
    (void)hp;
    (void)miss;
    (void)battle;
}

void CogAttacks::update(float dt, uint32_t nowMs, const Battle* battle) {
    (void)dt;
    (void)battle;
    nowMs_ = nowMs;

    for (int i = 0; i < kMaxAttacks; ++i) {
        ActiveAttack& atk = attacks_[i];
        if (!atk.active) continue;

        if (nowMs >= atk.startMs + atk.durMs) {
            atk.active = false;
            continue;
        }

        if (atk.soundName[0] != '\0' && !atk.soundPlayed && nowMs >= atk.soundAtMs) {
            playSound(atk.soundName);
            atk.soundPlayed = true;
        }
    }
}

void CogAttacks::draw(render::Renderer& renderer, const Battle* battle) {
    if (!battle) return;

    for (int i = 0; i < kMaxAttacks; ++i) {
        const ActiveAttack& atk = attacks_[i];
        if (!atk.active || !atk.prop || !atk.prop->loaded || !atk.prop->model.loaded()) continue;

        const float age = float(nowMs_ - atk.startMs) / 1000.0f;
        const bool isActor = atk.prop->hasAnim && atk.prop->anim.loaded();

        if (atk.isThrow) {
            if (nowMs_ < atk.releaseMs) {
                // In Cog hand
                C3D_Mtx m;
                Mtx_Identity(&m);
                Mtx_Translate(&m, atk.x0, atk.y0, atk.z0, true);
                if (isActor) {
                    u32 fa = 0, fb = 0;
                    float blend = 0.0f;
                    render::sampleClip(atk.prop->anim, age, &fa, &fb, &blend);
                    renderer.drawMorph(atk.prop->model, atk.prop->anim, fa, fb, blend, m);
                } else {
                    renderer.drawModel(atk.prop->model, m);
                }
            } else if (nowMs_ >= atk.releaseMs && nowMs_ <= atk.hitMs) {
                // Parabolic trajectory flight
                const float dur = float(atk.hitMs - atk.releaseMs);
                const float alpha = dur > 0.001f ? float(nowMs_ - atk.releaseMs) / dur : 1.0f;
                const float clampedA = alpha > 1.0f ? 1.0f : (alpha < 0.0f ? 0.0f : alpha);

                const float px = atk.x0 + (atk.x1 - atk.x0) * clampedA;
                const float py = atk.y0 + (atk.y1 - atk.y0) * clampedA;
                const float pz = atk.z0 + (atk.z1 - atk.z0) * clampedA + 4.0f * 2.0f * clampedA * (1.0f - clampedA);

                const float dx = atk.x1 - atk.x0;
                const float dy = atk.y1 - atk.y0;
                const float heading = atan2f(dy, dx) * 180.0f / 3.14159265f - 90.0f;

                C3D_Mtx m;
                Mtx_Identity(&m);
                Mtx_Translate(&m, px, py, pz, true);
                Mtx_RotateZ(&m, heading * kDegToRad, true);
                const float roll = clampedA * 360.0f * 2.0f;
                Mtx_RotateY(&m, roll * kDegToRad, true);

                if (isActor) {
                    u32 fa = 0, fb = 0;
                    float blend = 0.0f;
                    render::sampleClip(atk.prop->anim, age, &fa, &fb, &blend);
                    renderer.drawMorph(atk.prop->model, atk.prop->anim, fa, fb, blend, m);
                } else {
                    renderer.drawModel(atk.prop->model, m);
                }
            }
        } else {
            // Not a throw: held or target prop
            if (!strcmp(atk.prop->name, "stormcloud")) {
                // Overhead above target Toon
                C3D_Mtx m;
                Mtx_Identity(&m);
                Mtx_Translate(&m, atk.x1, atk.y1, atk.z1 + 2.5f, true);
                Mtx_Scale(&m, 1.2f, 1.2f, 1.2f);
                if (isActor) {
                    u32 fa = 0, fb = 0;
                    float blend = 0.0f;
                    render::sampleClip(atk.prop->anim, age, &fa, &fb, &blend);
                    renderer.drawMorph(atk.prop->model, atk.prop->anim, fa, fb, blend, m);
                } else {
                    renderer.drawModel(atk.prop->model, m);
                }
            } else if (!strcmp(atk.prop->name, "teeth")) {
                // In front of target Toon chomp
                C3D_Mtx m;
                Mtx_Identity(&m);
                Mtx_Translate(&m, atk.x1, atk.y1, atk.z1, true);
                Mtx_Scale(&m, 1.5f, 1.5f, 1.5f);
                if (isActor) {
                    u32 fa = 0, fb = 0;
                    float blend = 0.0f;
                    render::sampleClip(atk.prop->anim, age, &fa, &fb, &blend);
                    renderer.drawMorph(atk.prop->model, atk.prop->anim, fa, fb, blend, m);
                } else {
                    renderer.drawModel(atk.prop->model, m);
                }
            } else {
                // Held at Cog hand
                C3D_Mtx m;
                Mtx_Identity(&m);
                Mtx_Translate(&m, atk.x0, atk.y0, atk.z0, true);
                if (isActor) {
                    u32 fa = 0, fb = 0;
                    float blend = 0.0f;
                    render::sampleClip(atk.prop->anim, age, &fa, &fb, &blend);
                    renderer.drawMorph(atk.prop->model, atk.prop->anim, fa, fb, blend, m);
                } else {
                    renderer.drawModel(atk.prop->model, m);
                }
            }
        }
    }
}

void CogAttacks::playSound(const char* soundFile) {
    if (!soundFile || soundFile[0] == '\0') return;

    char baseName[32];
    stripOggExtension(soundFile, baseName, sizeof(baseName));

    // Check if loaded in sfx cache
    for (int i = 0; i < kMaxSfx; ++i) {
        if (sfx_[i].id != audio::kInvalidSfx && !strcmp(sfx_[i].name, baseName)) {
            sfx_[i].lastUsed = nowMs_;
            audio::playSfx(sfx_[i].id);
            return;
        }
    }

    char tsndPath[96];
    snprintf(tsndPath, sizeof(tsndPath), "sdmc:/3ds/toontown3ds/data/audio/%s.tsnd", baseName);
    const uint32_t need = audio::sfxFileBytes(tsndPath);
    if (need == 0 || need > kSfxCacheBytes) return;

    // Bounded by bytes as well as slots (battle_validate.py budgets kSfxCacheBytes): evict the
    // least recently used cues until the new one fits.
    int freeIdx = -1;
    for (;;) {
        uint32_t used = 0;
        freeIdx = -1;
        for (int i = 0; i < kMaxSfx; ++i) {
            if (sfx_[i].id == audio::kInvalidSfx) {
                if (freeIdx < 0) freeIdx = i;
            } else {
                used += audio::sfxBytes(sfx_[i].id);
            }
        }
        if (freeIdx >= 0 && used + need <= kSfxCacheBytes) break;
        int victim = -1;
        for (int i = 0; i < kMaxSfx; ++i)
            if (sfx_[i].id != audio::kInvalidSfx && (victim < 0 || sfx_[i].lastUsed < sfx_[victim].lastUsed))
                victim = i;
        if (victim < 0) return;
        audio::freeSfx(sfx_[victim].id);
        sfx_[victim].id = audio::kInvalidSfx;
    }

    const audio::SfxId id = audio::loadSfx(tsndPath);
    if (id != audio::kInvalidSfx) {
        snprintf(sfx_[freeIdx].name, sizeof(sfx_[freeIdx].name), "%s", baseName);
        sfx_[freeIdx].id = id;
        sfx_[freeIdx].lastUsed = nowMs_;
        audio::playSfx(id);
    }
}

} // namespace tt::world

#endif // __3DS__
