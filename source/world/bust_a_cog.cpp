#include "world/bust_a_cog.h"

#ifdef __3DS__

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/log.h"

namespace tt::world {

namespace {

constexpr const char* kDir = "sdmc:/3ds/toontown3ds/data/level/props/";
constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegToRad = kPi / 180.0f;

// DistributedGolfGreenGame.__init__'s board geometry.
constexpr float kSizeMult = 1.4f;
constexpr float kCellX = 1.0f * kSizeMult;
constexpr float kCellZ = kCellX * 0.8f;
constexpr float kRadius = 0.5f * kCellX;
constexpr int kDimX = BustACog::kDimX, kDimZ = BustACog::kDimZ;
constexpr float kMinX = -1.0f * (kDimX + 0.3751f) * 0.5f * kCellX;
constexpr float kMinZ = -kDimZ * 0.1f * kCellZ;
constexpr float kMaxX = kMinX + (kDimX + 0.5f) * kCellX;
constexpr float kMaxZ = kMinZ + kDimZ * kCellZ;
constexpr float kNewBallZ = kMinZ + 0.1f * kSizeMult;
constexpr float kFrameZ = 2.0f;          // frame.setZ(2)
constexpr int kCountTime = 10;
constexpr int kBombIndex = 7, kWildIndex = 8;
constexpr float kBallSize = kRadius * 2.0f; // addSprite's size

// GameSprite.colorList / disolveList.
constexpr float kColors[10][4] = {
    {1, 0.2f, 0.2f, 1}, {0.7f, 0.8f, 1, 1}, {0, 1, 0, 1}, {1, 1, 1, 1}, {0, 0, 0, 1},
    {1, 0, 1, 1},       {1, 1, 0, 1},       {1, 0.5f, 0, 1}, {0, 1, 1, 1}, {0.5f, 0.5f, 0.5f, 1}};

// bac_ball_* order (bake_level_props.BUSTACOG_BALLS).
enum BallModel : int8_t { kWhite, kRed, kBlue, kGreen, kCog, kPurple, kYellow, kSteel, kFire, kUnknown };
constexpr const char* kBallNames[BustACog::kModelCount] = {"white", "red",    "blue",  "green", "cog",
                                                           "purple", "yello", "steel", "fire",  "unknown"};
constexpr const char* kSfxNames[] = {"Golf_Hit_Ball",          "MG_maze_pickup",           "Toon_bodyfall_synergy",
                                     "MG_cannon_fire_alt",     "LB_capacitor_discharge_3", "MG_pairing_match_bonus_both",
                                     "SA_shred"};

float cellX(int x, int z) {
    const float offset = z % 2 == 0 ? kCellX * 0.5f : 0.0f;
    return x * kCellX + kMinX + offset + kCellX * 0.4375f;
}
float cellZ(int z) { return z * kCellZ + kMinZ; }

// The six hex neighbours (hasNeighbor / getNeighbors order): even rows lean right.
void neighbours(int x, int z, int out[6][2]) {
    const int d = z % 2 == 0 ? 1 : -1;
    const int n[6][2] = {{x - 1, z}, {x + 1, z}, {x, z + 1}, {x + d, z + 1}, {x, z - 1}, {x + d, z - 1}};
    std::memcpy(out, n, sizeof(n));
}

} // namespace

float BustACog::random() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return (rng_ & 0xFFFFFF) / float(0x1000000);
}

void BustACog::load(BasicAssetBatch<32>& batch) {
    char path[128];
    for (int i = 0; i < kModelCount; ++i) {
        std::snprintf(path, sizeof(path), "%sbac_ball_%s.ttm", kDir, kBallNames[i]);
        batch.load(balls_[i], path);
    }
    std::snprintf(path, sizeof(path), "%sgolf_hole.ttm", kDir);
    batch.load(hole_, path);
    std::snprintf(path, sizeof(path), "%sbac_background.ttm", kDir);
    batch.load(background_, path);
    std::snprintf(path, sizeof(path), "%sbac_shooter.ttm", kDir);
    batch.load(shooter_, path);
    std::snprintf(path, sizeof(path), "%sbac_shooter_moving.ttm", kDir);
    batch.load(shooterMoving_, path);
    for (int i = 0; i < kSfxCount; ++i) {
        std::snprintf(path, sizeof(path), "%sbac_%s.tsnd", kDir, kSfxNames[i]);
        sfx_[i] = audio::loadSfx(path);
    }
}

void BustACog::unload() {
    end();
    for (render::Model& m : balls_) m.unload();
    hole_.unload();
    background_.unload();
    shooter_.unload();
    shooterMoving_.unload();
    for (audio::SfxId& id : sfx_) {
        if (id != audio::kInvalidSfx) audio::freeSfx(id);
        id = audio::kInvalidSfx;
    }
}

void BustACog::play(int sfx) const {
    if (sfx >= 0 && sfx < kSfxCount && sfx_[sfx] != audio::kInvalidSfx) audio::playSfx(sfx_[sfx]);
}

// GameSprite.setBallType: a foundation (or solidOverride) ball takes its coloured model; any
// other is the white ball tinted with the ghost colour. Types 10 / 11 carry a gift.
void BustACog::setBallType(Sprite& s, int type, bool solidOverride) {
    s.colorType = int8_t(type);
    s.multiColor = false;
    s.breakable = true;
    s.giftId = -1;
    s.model = kWhite;
    const float white[4] = {1, 1, 1, 1};
    std::memcpy(s.color, white, sizeof(white));
    const bool solid = s.foundation || solidOverride;
    auto multi = [&s](std::initializer_list<uint8_t> list, float step) {
        s.multiColor = true;
        s.mcLen = 0;
        for (uint8_t c : list) s.mcList[s.mcLen++] = c;
        s.mcIndex = 0, s.mcNext = 1, s.mcLevel = 0, s.mcStep = step;
    };
    if (type == 10) { // bust_a_cog_ball_fire: red, gives a bomb (7)
        s.model = kFire, s.giftId = 7, s.colorType = 0;
        return;
    }
    if (type == 11) { // bust_a_cog_ball_unknown: blue, gives a wild ball (8)
        s.model = kUnknown, s.giftId = 8, s.colorType = 1;
        return;
    }
    if (!solid) {
        if (type <= 2) { // the ghost colours: alpha 0.5
            std::memcpy(s.color, kColors[type], sizeof(s.color));
            s.color[3] = 0.5f;
        } else if (type < 10) {
            std::memcpy(s.color, kColors[type], sizeof(s.color));
        }
        if (type == 7) multi({7, 4}, 0.1f);
        if (type == 8) multi({0, 1, 2, 6}, 0.025f);
        if (type == 9) s.breakable = false;
        return;
    }
    switch (type) {
    case 0: s.model = kRed; break;
    case 1: s.model = kBlue; break;
    case 2: s.model = kGreen; break;
    case 3: s.model = kCog; break;
    case 4: std::memcpy(s.color, kColors[4], sizeof(s.color)); break;
    case 5: s.model = kPurple; break;
    case 6: s.model = kYellow; break;
    case 7:
        std::memcpy(s.color, kColors[7], sizeof(s.color));
        multi({7, 4}, 0.15f);
        break;
    case 8:
        std::memcpy(s.color, kColors[8], sizeof(s.color));
        multi({0, 1, 2, 6}, 0.1f);
        break;
    case 9: s.model = kSteel, s.breakable = false; break;
    default: break;
    }
}

void BustACog::runColor(Sprite& s) {
    if (!s.multiColor || s.mcLen < 2) return;
    const float* c1 = kColors[s.mcList[s.mcIndex]];
    const float* c2 = kColors[s.mcList[s.mcNext]];
    for (int k = 0; k < 4; ++k) s.color[k] = c1[k] * (1.0f - s.mcLevel) + c2[k] * s.mcLevel;
    s.mcLevel += s.mcStep;
    if (s.mcLevel > 1.0f) {
        s.mcLevel = 0;
        s.mcIndex = uint8_t((s.mcIndex + 1) % s.mcLen);
        s.mcNext = uint8_t((s.mcIndex + 1) % s.mcLen);
    }
}

int BustACog::addSprite(bool found, int color) {
    for (int i = 0; i < kMaxSprites; ++i) {
        Sprite& s = sprites_[i];
        if (s.used) continue;
        s = Sprite{};
        s.used = true;
        s.foundation = found;
        setBallType(s, color, false);
        order_[numOrder_++] = i;
        return i;
    }
    TT_LOG("bust-a-cog: sprite pool full");
    return -1;
}

void BustACog::deathEffect(int idx) {
    const Sprite& s = sprites_[idx];
    for (Effect& e : effects_) {
        if (e.used) continue;
        e = Effect{};
        e.used = true;
        // wrtReparentTo(render), then ProjectileInterval(startVel, gravityMult 2) while the colour
        // scale lerps to the dissolve colour. Kept in sprite-node space here (the board does not
        // move during the effect); x / z on the board, y up off it.
        e.pos[0] = s.x, e.pos[1] = 0, e.pos[2] = s.z;
        const float n2 = random(), n3 = random();
        e.vel[0] = -20.0f + (1.0f - n3) * 40.0f; // world x
        e.vel[1] = -20.0f + n3 * 40.0f;          // world y (board -z)
        e.vel[2] = 30.0f;                        // world z (off the board)
        e.dur = 0.5f + n2 * 1.0f;
        e.tilt = 95.0f - (s.z + kFrameZ) * 2.0f;
        e.model = s.model;
        const int ct = s.colorType >= 0 && s.colorType < 10 ? s.colorType : 3;
        std::memcpy(e.from, kColors[ct], sizeof(e.from));
        std::memcpy(e.to, kColors[ct], sizeof(e.to));
        e.to[3] = 0;
        return;
    }
}

void BustACog::wildEffect(float x, float z) {
    for (Effect& e : effects_) {
        if (e.used) continue;
        e = Effect{};
        e.used = true;
        e.wild = true;
        e.pos[0] = x, e.pos[2] = z;
        e.dur = 1.0f;
        e.tilt = 95.0f - (z + kFrameZ) * 2.0f;
        e.model = kWhite;
        std::memcpy(e.from, kColors[1], sizeof(e.from)); // questionSprite: a colour-1 ghost
        e.from[3] = 0.5f;
        return;
    }
}

void BustACog::killSprite(int idx) {
    if (idx < 0 || !sprites_[idx].used) return;
    Sprite& s = sprites_[idx];
    if (s.giftId >= 0) giftId_ = s.giftId;
    if (control_ == idx) control_ = -1;
    for (int i = 0; i < numOrder_; ++i)
        if (order_[i] == idx) {
            std::memmove(order_ + i, order_ + i + 1, (numOrder_ - i - 1) * sizeof(int));
            --numOrder_;
            break;
        }
    if (s.gridX >= 0) {
        grid_[s.gridX][s.gridZ] = -1;
        gridColor_[s.gridX][s.gridZ] = 1.0f;
        deathEffect(idx);
    }
    s.used = false;
    hasChanged_ = true;
}

void BustACog::clearGrid() {
    for (int x = 0; x < kDimX; ++x)
        for (int z = 0; z < kDimZ; ++z) {
            if (grid_[x][z] >= 0) killSprite(grid_[x][z]);
            gridColor_[x][z] = 1.0f;
        }
    killSprite(control_);
}

int BustACog::colorTypeAt(int x, int z) const {
    if (x < 0 || x >= kDimX || z < 0 || z >= kDimZ || grid_[x][z] < 0) return -1;
    return sprites_[grid_[x][z]].colorType;
}

int BustACog::breakableAt(int x, int z) const {
    if (x < 0 || x >= kDimX || z < 0 || z >= kDimZ || grid_[x][z] < 0) return -1;
    return sprites_[grid_[x][z]].breakable ? 1 : 0;
}

bool BustACog::hasNeighbor(int x, int z) const {
    int n[6][2];
    neighbours(x, z, n);
    for (auto& c : n)
        if (c[0] >= 0 && c[0] < kDimX && c[1] >= 0 && c[1] < kDimZ && grid_[c[0]][c[1]] >= 0) return true;
    return false;
}

bool BustACog::findGrid(float x, float z, bool force, int* cx, int* cz) const {
    float best = 1e7f;
    bool found = false;
    for (int gx = 0; gx < kDimX; ++gx)
        for (int gz = 0; gz < kDimZ; ++gz) {
            const float dx = x - cellX(gx, gz), dz = z - cellZ(gz);
            float d = dx * dx + dz * dz;
            if (d == 0) d = 1e-10f;
            if (grid_[gx][gz] < 0 && d < best && (force || hasNeighbor(gx, gz))) {
                best = d, *cx = gx, *cz = gz, found = true;
            }
        }
    return found;
}

void BustACog::placeIntoGrid(int idx, int x, int z) {
    // An occupied cell tries (x + 1, z - 1), as upstream's recursion does.
    while (x >= 0 && x < kDimX && z >= 0 && z < kDimZ && grid_[x][z] >= 0) ++x, --z;
    if (x < 0 || x >= kDimX || z < 0 || z >= kDimZ) {
        killSprite(idx);
        return;
    }
    Sprite& s = sprites_[idx];
    grid_[x][z] = int8_t(idx);
    s.gridX = int8_t(x), s.gridZ = int8_t(z);
    s.isActive = false, s.velX = s.velZ = 0, s.hasAcc = false;
    s.prevX = s.x, s.prevZ = s.z;
    s.x = cellX(x, z), s.z = cellZ(z);
    if (control_ == idx) control_ = -1;
    colorGridFlag_ = true;
    hasChanged_ = true;
    checkForTooLow();
}

bool BustACog::matchWild(int x, int z, int color) const {
    const int t = colorTypeAt(x, z);
    if (breakableAt(x, z) != 1) return false;
    return t != -1 && (t == kWildIndex || color == kWildIndex);
}

void BustACog::fillMatchList(int x, int z) {
    for (int i = 0; i < numMatch_; ++i)
        if (matchList_[i][0] == x && matchList_[i][1] == z) return;
    matchList_[numMatch_][0] = x, matchList_[numMatch_][1] = z;
    ++numMatch_;
    const int color = sprites_[grid_[x][z]].colorType;
    if (color == 4) return;
    int n[6][2];
    neighbours(x, z, n);
    for (auto& c : n)
        if (colorTypeAt(c[0], c[1]) == color || matchWild(c[0], c[1], color)) fillMatchList(c[0], c[1]);
}

void BustACog::addNeighbors(int x, int z, bool breakableOnly) {
    int n[6][2];
    neighbours(x, z, n);
    for (auto& c : n) {
        if (colorTypeAt(c[0], c[1]) == -1) continue;
        if (breakableOnly && breakableAt(c[0], c[1]) <= 0) continue;
        matchList_[numMatch_][0] = c[0], matchList_[numMatch_][1] = c[1];
        ++numMatch_;
    }
}

void BustACog::clearMatchList(int typeClear) {
    play(kBurst);
    for (int i = 0; i < numMatch_; ++i) {
        const int idx = grid_[matchList_[i][0]][matchList_[i][1]];
        if (idx < 0) continue;
        if (typeClear == kWildIndex) wildEffect(sprites_[idx].x, sprites_[idx].z); // questionSprite
        killSprite(idx);
    }
}

void BustACog::bump(Sprite& s, bool warning) {
    // warningBump: Wait(n1 / 10), up 0.5 over n2 / 10, down over n3 / 10, up over n2 / 10, down
    // over n1 / 10. shake: up 0.25 over n2 / 10, down over n1 / 10 (n in 0..2 / 0..1).
    s.bumpStart = now_;
    if (warning) {
        const float n1 = random() * 2.0f * 0.1f, n2 = random() * 2.0f * 0.1f, n3 = random() * 2.0f * 0.1f;
        const float t[5] = {n1, n1 + n2, n1 + n2 + n3, n1 + 2 * n2 + n3, 2 * n1 + 2 * n2 + n3};
        const float v[5] = {0, 0.5f, 0, 0.5f, 0};
        std::memcpy(s.bumpT, t, sizeof(t));
        std::memcpy(s.bumpV, v, sizeof(v));
        s.bumpKeys = 5;
    } else {
        const float n1 = random() * 0.1f, n2 = random() * 0.1f;
        const float t[3] = {0, n2, n2 + n1};
        const float v[3] = {0, 0.25f, 0};
        std::memcpy(s.bumpT, t, sizeof(t));
        std::memcpy(s.bumpV, v, sizeof(v));
        s.bumpKeys = 3;
    }
}

float BustACog::bumpAt(const Sprite& s) const {
    if (s.bumpStart < 0 || !s.bumpKeys) return 0;
    const float t = now_ - s.bumpStart;
    if (t <= s.bumpT[0]) return s.bumpV[0];
    for (int k = 1; k < s.bumpKeys; ++k)
        if (t <= s.bumpT[k]) {
            const float span = s.bumpT[k] - s.bumpT[k - 1];
            const float u = span > 0 ? (t - s.bumpT[k - 1]) / span : 1.0f;
            return s.bumpV[k - 1] + (s.bumpV[k] - s.bumpV[k - 1]) * u;
        }
    return s.bumpV[s.bumpKeys - 1];
}

void BustACog::stickInGrid(int idx, bool force) {
    Sprite& s = sprites_[idx];
    if (!s.used || !s.isActive) return;
    int cx, cz;
    if (!findGrid(s.x, s.z, force, &cx, &cz)) return;
    const int colorType = s.colorType;
    s.isActive = false, s.velX = s.velZ = 0, s.hasAcc = false;
    play(kLand);
    closestX_ = cx, closestZ_ = cz;
    placeIntoGrid(idx, cx, cz);
    numMatch_ = 0;
    if (colorType == kBombIndex) {
        play(kBomb); // bombNeighbors (the kapow explosion is not drawn)
        addNeighbors(cx, cz, true);
        for (int i = 0; i < numMatch_; ++i) {
            int n[6][2];
            neighbours(matchList_[i][0], matchList_[i][1], n);
            for (auto& c : n) {
                const int ni = colorTypeAt(c[0], c[1]) == -1 ? -1 : grid_[c[0]][c[1]];
                bool inMatch = false;
                for (int j = 0; j < numMatch_; ++j) inMatch |= matchList_[j][0] == c[0] && matchList_[j][1] == c[1];
                if (ni >= 0 && !inMatch) bump(sprites_[ni], false);
            }
        }
        clearMatchList(0);
    } else {
        fillMatchList(cx, cz);
        if (numMatch_ >= 3) {
            clearMatchList(colorType);
        } else {
            int n[6][2];
            neighbours(cx, cz, n);
            for (auto& c : n)
                if (colorTypeAt(c[0], c[1]) != -1) bump(sprites_[grid_[c[0]][c[1]]], false);
        }
    }
}

void BustACog::clearFloaters() {
    // Everything not connected to the top row (gridDimZ - 1) falls.
    bool grounded[kDimX][kDimZ] = {};
    for (int x = 0; x < kDimX; ++x) grounded[x][kDimZ - 1] = grid_[x][kDimZ - 1] >= 0;
    for (bool changed = true; changed;) {
        changed = false;
        for (int x = 0; x < kDimX; ++x)
            for (int z = 0; z < kDimZ; ++z) {
                if (grid_[x][z] < 0 || grounded[x][z]) continue;
                int n[6][2];
                neighbours(x, z, n);
                for (auto& c : n)
                    if (c[0] >= 0 && c[0] < kDimX && c[1] >= 0 && c[1] < kDimZ && grounded[c[0]][c[1]]) {
                        grounded[x][z] = changed = true;
                        break;
                    }
            }
    }
    for (int x = 0; x < kDimX; ++x)
        for (int z = 0; z < kDimZ; ++z)
            if (grid_[x][z] >= 0 && !grounded[x][z]) killSprite(grid_[x][z]);
}

void BustACog::explodeBombs() {
    bool did = false;
    for (int x = 0; x < kDimX; ++x)
        for (int z = 0; z < kDimZ; ++z)
            if (grid_[x][z] >= 0 && sprites_[grid_[x][z]].colorType == kBombIndex) {
                killSprite(grid_[x][z]);
                did = true;
            }
    if (did) play(kBomb);
}

void BustACog::checkForTooLow() {
    // findLowestSprite skips gridPosZ 0 (falsy in upstream's test).
    int lowest = 100;
    for (int i = 0; i < numOrder_; ++i) {
        const Sprite& s = sprites_[order_[i]];
        if (s.gridZ > 0 && s.gridZ < lowest) lowest = s.gridZ;
    }
    if (lowest <= notch_ && !tooLow_) doFail();
}

void BustACog::doFail() {
    tooLow_ = true;
    failAt_ = now_ + 1.0f;
    for (int i = 0; i < numOrder_; ++i) { // setColorType(4)
        Sprite& s = sprites_[order_[i]];
        s.colorType = 4;
        std::memcpy(s.color, kColors[4], sizeof(s.color));
        s.multiColor = false;
    }
    running_ = false;
}

void BustACog::finishBoard(bool success) {
    rollT_ = -1;
    countDown_ = kCountTime;
    play(success ? kWin : kLose);
    if (!success) giftId_ = -1;
    patternLen_ = 0;
    countDownRunning_ = false;
    clearGrid();
    notch_ = 0;
    notchShown_ = 0;
    running_ = false;
    pendingRequest_ = true;
    pendingSuccess_ = success ? 1 : 0;
}

void BustACog::colorGrid() {
    for (int x = 0; x < kDimX; ++x)
        for (int z = 0; z < kDimZ; ++z) {
            if (grid_[x][z] >= 0) gridColor_[x][z] = sprites_[grid_[x][z]].colorType == 3 ? 0.0f : 0.6f;
            else if (z <= notch_) gridColor_[x][z] = -1.0f; // outColor: invisible
            else if (hasNeighbor(x, z)) gridColor_[x][z] = 0.8f;
            else gridColor_[x][z] = 1.0f;
        }
}

void BustACog::collide(int a, int b) {
    Sprite& m = sprites_[a];
    Sprite& t = sprites_[b];
    m.velX = m.velZ = t.velX = t.velZ = 0;
    if (t.isActive) t.x = t.prevX, t.z = t.prevZ;
    if (m.isActive) m.x = m.prevX, m.z = m.prevZ;
    stickInGrid(a, false);
    if (sprites_[b].used) stickInGrid(b, false);
}

void BustACog::begin() {
    if (active_) return;
    for (auto& row : grid_)
        for (int8_t& c : row) c = -1;
    for (auto& row : gridColor_)
        for (float& c : row) c = 1.0f;
    for (Sprite& s : sprites_) s.used = false;
    for (Effect& e : effects_) e.used = false;
    numOrder_ = 0;
    control_ = -1;
    standby_ = Sprite{};
    standby_.used = true;
    standby_.x = 0, standby_.z = -3.0f;
    setBallType(standby_, 0, false);
    std::memcpy(standby_.color, kColors[4], sizeof(standby_.color)); // colorShadow
    standby_.color[3] = 0.5f;
    standbyHold_ = -1;
    patternLen_ = 0, attackCounter_ = 0;
    notch_ = 0, notchShown_ = 0, rollT_ = -1;
    countDown_ = kCountTime, countDownRunning_ = false;
    giftId_ = -1, ballLoaded_ = 0;
    aimRadian_ = -0.5f * kPi;
    running_ = false, finished_ = false, tooLow_ = false;
    failAt_ = -1;
    pendingRequest_ = false;
    active_ = true;
    TT_LOG("bust-a-cog: game start");
}

void BustACog::end() {
    if (!active_) return;
    active_ = false;
    running_ = false;
    countDownRunning_ = false;
    for (Sprite& s : sprites_) s.used = false;
    for (Effect& e : effects_) e.used = false;
    numOrder_ = 0;
    control_ = -1;
    TT_LOG("bust-a-cog: game left");
}

void BustACog::startBoard(const uint8_t (*balls)[3], int numBalls, const uint8_t* pattern, int patternLen) {
    if (!active_ || finished_) return;
    clearGrid();
    patternLen_ = std::min(patternLen, int(sizeof(pattern_)));
    std::memcpy(pattern_, pattern, patternLen_);
    attackCounter_ = 0;
    notch_ = 0, notchShown_ = 0, rollT_ = -1;
    countDown_ = kCountTime;
    tooLow_ = false;
    failAt_ = -1;
    for (int i = 0; i < numBalls; ++i) {
        const int idx = addSprite(true, balls[i][2]);
        if (idx < 0) break;
        const int x = balls[i][0], z = kDimZ - 1 - balls[i][1];
        placeIntoGrid(idx, x < kDimX ? x : kDimX - 1, z >= 0 ? z : 0);
    }
    colorGridFlag_ = true;
    tooLow_ = false;
    countDownRunning_ = true;
    countClock_ = 0;
    killSprite(control_);
    running_ = true;
    TT_LOG("bust-a-cog: board of %d balls, pattern of %d", numBalls, patternLen_);
}

bool BustACog::update(float dt, float aim, bool fire, uint8_t* success) {
    if (!active_) return false;
    now_ += dt;

    // Effects.
    for (Effect& e : effects_)
        if (e.used && (e.t += dt) >= e.dur) e.used = false;
    if (rollT_ >= 0) { // lerpSpritePos
        rollT_ += dt;
        const float u = std::min(rollT_ / 0.5f, 1.0f);
        notchShown_ = notchFrom_ + (notch_ - notchFrom_) * u;
        if (u >= 1.0f) rollT_ = -1;
    }
    if (failAt_ >= 0 && now_ >= failAt_) { // failBoard
        failAt_ = -1;
        finishBoard(false);
    }

    // doCountDown: once a second; a warning bump at 3, 2, 1 and the board rolls down one row at 0.
    if (countDownRunning_) {
        countClock_ += dt;
        if (countClock_ >= 1.0f) {
            countClock_ -= 1.0f;
            --countDown_;
            if (countDown_ == 3 || countDown_ == 2 || countDown_ == 1) {
                for (int i = 0; i < numOrder_; ++i) bump(sprites_[order_[i]], true);
            } else if (countDown_ <= 0) {
                countDown_ = kCountTime;
                ++notch_;
                notchFrom_ = notchShown_;
                rollT_ = 0;
                play(kMove);
                colorGridFlag_ = true;
                checkForTooLow();
            }
        }
    }

    if (running_) {
        // The mouse aim, here turned by the pad: within 0.2 of level either way.
        aimRadian_ -= aim * 1.6f * dt;
        aimRadian_ = std::clamp(aimRadian_, 0.2f - kPi, -0.2f);
        if (fire && ballLoaded_ && control_ >= 0) { // __handleMouseClick
            Sprite& c = sprites_[control_];
            c.inFrame = false;
            c.x = 0.0f;
            c.z = kNewBallZ + notchShown_ * kCellZ; // the frame spot in sprite-node space
            const float dir = -aimRadian_;
            c.accX = std::cos(dir) * 14.0f, c.accZ = std::sin(dir) * 14.0f, c.hasAcc = true;
            c.isActive = true;
            play(kFire);
            ballLoaded_ = 0;
        }

        if (giftId_ >= 0) { // a gift replaces the waiting ball
            ballLoaded_ = 2;
            standbyHold_ = giftId_;
            setBallType(standby_, giftId_, true);
            giftId_ = -1;
        }
        while (control_ < 0 && patternLen_ > 0) {
            if (attackCounter_ > patternLen_ - 1) attackCounter_ = 0;
            if (standbyHold_ >= 0) { // addControlSprite: the shot waits on the shooter
                control_ = addSprite(true, standbyHold_);
                if (control_ < 0) break;
                sprites_[control_].inFrame = true;
                sprites_[control_].x = 0.0f, sprites_[control_].z = -1.54f;
            }
            ballLoaded_ = 1;
            standbyHold_ = pattern_[attackCounter_ % patternLen_]; // predictAttackPattern(0)
            setBallType(standby_, standbyHold_, true);
            ++attackCounter_;
        }
        runColor(standby_);

        const float wallMaxX = kMaxX - kRadius, wallMinX = kMinX + kRadius, wallMaxZ = kMaxZ - kRadius;
        for (int i = 0; i < numOrder_; ++i) {
            const int idx = order_[i];
            Sprite& s = sprites_[idx];
            runColor(s);
            if (s.isActive) { // GameSprite.run
                s.prevX = s.x, s.prevZ = s.z;
                s.x += s.velX * dt, s.z += s.velZ * dt;
                s.velX *= 1.0f - dt * 4.0f, s.velZ *= 1.0f - dt * 4.0f;
                if (s.hasAcc) s.velX = s.accX, s.velZ = s.accZ;
            }
            if (s.inFrame) continue;
            if (s.x > wallMaxX) {
                s.x = wallMaxX;
                s.velX = -s.velX, s.accX = -s.accX;
            }
            if (s.x < wallMinX) {
                s.x = wallMinX;
                s.velX = -s.velX, s.accX = -s.accX;
            }
            if (s.z > wallMaxZ) stickInGrid(idx, true);
        }

        // __colTest: a moving ball against every other one.
        for (int i = 0; i < numOrder_; ++i)
            for (int j = i + 1; j < numOrder_; ++j) {
                const int a = order_[i], b = order_[j];
                Sprite& m = sprites_[a];
                Sprite& t = sprites_[b];
                if (!m.used || !t.used || !(m.isActive ^ t.isActive) || !m.canCollide || !t.canCollide) continue;
                const float dx = m.x - t.x, dz = m.z - t.z;
                if (dx * dx + dz * dz < (kRadius * 1.65f) * (kRadius * 1.65f)) collide(a, b);
            }

        if (hasChanged_ && running_) {
            clearFloaters();
            explodeBombs();
            int cogs = 0;
            for (int x = 0; x < kDimX; ++x)
                for (int z = 0; z < kDimZ; ++z)
                    if (grid_[x][z] >= 0 && sprites_[grid_[x][z]].colorType == 3) ++cogs;
            if (cogs == 0) { // the Cog balls are gone: solved
                finishBoard(true);
                killSprite(control_);
                standbyHold_ = -1;
            }
            colorGridFlag_ = true;
        }
        hasChanged_ = false;
    }
    if (colorGridFlag_) {
        colorGridFlag_ = false;
        colorGrid();
    }
    if (pendingRequest_) {
        pendingRequest_ = false;
        *success = pendingSuccess_;
        return true;
    }
    return false;
}

void BustACog::spriteNodeMtx(const C3D_Mtx& frame, C3D_Mtx* out) const {
    *out = frame;
    Mtx_Translate(out, 0.0f, kRadius, -notchShown_ * kCellZ, true);
}

void BustACog::drawBall(render::Renderer& renderer, const C3D_Mtx& parent, float x, float z, int model,
                        const float color[4], float bumpZ, float scale) const {
    const render::Model& m = balls_[model];
    if (!m.loaded()) return;
    // GameSprite.face: nodeObj P = 95 - (z + frameZ) * 2, scaled to the ball's size.
    C3D_Mtx mm = parent;
    Mtx_Translate(&mm, x, 0.0f, z + bumpZ, true);
    Mtx_RotateX(&mm, (95.0f - (z + kFrameZ) * 2.0f) * kDegToRad, true);
    Mtx_Scale(&mm, kBallSize * scale, kBallSize * scale, kBallSize * scale);
    renderer.setBaseTint(color[0], color[1], color[2], color[3]);
    renderer.drawModel(const_cast<render::Model&>(m), mm);
}

void BustACog::draw(render::Renderer& renderer, const C3D_Mtx& entity) const {
    if (!active_) return;
    C3D_Mtx frame = entity;
    Mtx_Translate(&frame, 0.0f, 10.0f, kFrameZ, true);
    Mtx_RotateX(&frame, 90.0f * kDegToRad, true);
    C3D_Mtx sprite;
    spriteNodeMtx(frame, &sprite);

    if (background_.loaded()) { // backBoard: (0, -1.5, 8), P -90, scale (0.3, 0.2, 0.25)
        C3D_Mtx m = frame;
        Mtx_Translate(&m, 0.0f, -1.5f, 8.0f, true);
        Mtx_RotateX(&m, -90.0f * kDegToRad, true);
        Mtx_Scale(&m, 0.3f, 0.2f, 0.25f);
        renderer.drawModel(const_cast<render::Model&>(background_), m);
    }
    if (shooter_.loaded()) { // aimbase: (0, 0, minZ + 0.1), hpr (90, 0, 90), scale (0.3, 0.3, 0.15)
        C3D_Mtx m = frame;
        Mtx_Translate(&m, 0.0f, 0.0f, kMinZ + 0.1f, true);
        Mtx_RotateZ(&m, 90.0f * kDegToRad, true);
        Mtx_RotateY(&m, 90.0f * kDegToRad, true);
        Mtx_Scale(&m, 0.3f, 0.3f, 0.15f);
        renderer.drawModel(const_cast<render::Model&>(shooter_), m);
        if (shooterMoving_.loaded()) { // the aimer turns by aimRadian in degrees
            Mtx_RotateZ(&m, aimRadian_, true);
            renderer.drawModel(const_cast<render::Model&>(shooterMoving_), m);
        }
    }
    if (hole_.loaded()) { // the grid's holes: (x, -radius, z), hpr (-90, 0, 90), scale 1.2
        for (int x = 0; x < kDimX; ++x)
            for (int z = 0; z < kDimZ; ++z) {
                const float c = gridColor_[x][z];
                if (c < 0) continue;
                C3D_Mtx m = sprite;
                Mtx_Translate(&m, cellX(x, z), -kRadius, cellZ(z), true);
                Mtx_RotateZ(&m, -90.0f * kDegToRad, true);
                Mtx_RotateY(&m, 90.0f * kDegToRad, true);
                Mtx_Scale(&m, 1.2f, 1.2f, 1.2f);
                renderer.setBaseTint(c, c, c, 1.0f);
                renderer.drawModel(const_cast<render::Model&>(hole_), m);
            }
    }
    C3D_Mtx raised = frame; // the shot on the shooter (0, 0.7, -1.54) and the waiting ball (0, 0.7, -3)
    Mtx_Translate(&raised, 0.0f, kRadius, 0.0f, true);
    for (int i = 0; i < numOrder_; ++i) {
        const Sprite& s = sprites_[order_[i]];
        if (!s.used) continue;
        drawBall(renderer, s.inFrame ? raised : sprite, s.x, s.z, s.model, s.color, bumpAt(s));
    }
    if (standby_.used && standbyHold_ >= 0)
        drawBall(renderer, raised, standby_.x, standby_.z, standby_.model, standby_.color, 0.0f);
    for (const Effect& e : effects_) {
        if (!e.used) continue;
        const float u = std::min(e.t / e.dur, 1.0f);
        float color[4];
        for (int k = 0; k < 4; ++k) color[k] = e.from[k] + (e.to[k] - e.from[k]) * u;
        if (e.wild) {
            drawBall(renderer, sprite, e.pos[0], e.pos[2], e.model, color, 0.0f, 1.0f + 0.5f * u);
            continue;
        }
        // The projectile in world axes: board x, world y = -board z, world z = board y.
        const float t = e.t;
        const float wx = e.vel[0] * t, wy = e.vel[1] * t, wz = e.vel[2] * t - 0.5f * 64.35f * t * t;
        C3D_Mtx m = sprite;
        Mtx_Translate(&m, e.pos[0] + wx, wz, e.pos[2] - wy, true);
        Mtx_RotateX(&m, e.tilt * kDegToRad, true);
        Mtx_Scale(&m, kBallSize, kBallSize, kBallSize);
        if (balls_[e.model].loaded()) {
            renderer.setBaseTint(color[0], color[1], color[2], color[3]);
            renderer.drawModel(const_cast<render::Model&>(balls_[e.model]), m);
        }
    }
    renderer.setBaseTint(1.0f, 1.0f, 1.0f, 1.0f);
}

void BustACog::camera(const C3D_Mtx& entity, float eye[3], float target[3]) {
    // focusPoint (0, 12, 27) H 180 with the camera at P -70 under it.
    const float local[2][3] = {{0.0f, 12.0f, 27.0f},
                               {0.0f, 12.0f - std::cos(70.0f * kDegToRad), 27.0f - std::sin(70.0f * kDegToRad)}};
    for (int p = 0; p < 2; ++p) {
        float* out = p ? target : eye;
        for (int r = 0; r < 3; ++r) {
            const C3D_FVec& row = entity.r[r];
            out[r] = row.x * local[p][0] + row.y * local[p][1] + row.z * local[p][2] + row.w;
        }
    }
}

} // namespace tt::world

#endif // __3DS__
