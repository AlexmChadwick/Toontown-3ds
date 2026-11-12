#include "game/race_game.h"

#include "render/ui_font.h"

#include <cmath>

#include <cstdio>
#include <cstring>

namespace tt::game {

namespace {
constexpr float kCameraMoveSeconds = 0.75f; // moveCamera: posQuatInterval(0.75, ...)
// DistributedRaceGame.cameraTopView (onstage): x, y, z, h, p.
constexpr float kTopView[5] = {-22.78f, -41.65f, 31.53f, -51.55f, -42.68f};
constexpr float kDeg = 0.0174532925f;
// showChanceRewards per reward: showChanceCard, 1 s, showChanceCardText, 3 s, resetChanceCard, 0.25 s.
constexpr float kCardFly = 1.0f, kCardText = 3.0f, kCardEnd = 4.0f, kCardStep = 4.25f;
// RaceGameGlobals.ChanceRewards' TTLocalizer strings (-1: RaceGameCardTextHi1), and which use
// RaceGameCardTextBeans (a bean bonus).
const char* const kRewardText[racegame::kNumRewards] = {
    " forward 1 space", " forward 1 space", " forward 1 space", " forward 2 spaces", " forward 2 spaces",
    " forward 2 spaces", " forward 3 spaces", " forward 3 spaces", " forward 3 spaces", "all others back \n3 spaces",
    "all others back \n3 spaces", " back 1 space", " back 1 space", " back 2 spaces", " back 2 spaces",
    " back 3 spaces", " back 3 spaces", " all others forward \n3 spaces", " all others forward \n3 spaces",
    "2 jellybeans", "2 jellybeans", "2 jellybeans", "2 jellybeans", "4 jellybeans", "4 jellybeans",
    "4 jellybeans", "4 jellybeans", "10 jellybeans!", nullptr, "Instant Winner!"};

// getWalkDuration: squares / 1.2, times 0.3 for a run (more than 4 squares).
float walkDuration(int squares) {
    const float d = std::fabs(squares / 1.2f);
    return squares > 4 || squares < -4 ? d * 0.3f : d;
}

struct Quat4 {
    float x, y, z, w;
};

// Rotation (row-major 3x3) to quaternion and back; slerp between them (posQuatInterval).
Quat4 quatFromRows(const float m[3][3]) {
    Quat4 q;
    const float t = m[0][0] + m[1][1] + m[2][2];
    if (t > 0.0f) {
        const float s = std::sqrt(t + 1.0f) * 2.0f;
        q = {(m[2][1] - m[1][2]) / s, (m[0][2] - m[2][0]) / s, (m[1][0] - m[0][1]) / s, 0.25f * s};
    } else if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) {
        const float s = std::sqrt(1.0f + m[0][0] - m[1][1] - m[2][2]) * 2.0f;
        q = {0.25f * s, (m[0][1] + m[1][0]) / s, (m[0][2] + m[2][0]) / s, (m[2][1] - m[1][2]) / s};
    } else if (m[1][1] > m[2][2]) {
        const float s = std::sqrt(1.0f + m[1][1] - m[0][0] - m[2][2]) * 2.0f;
        q = {(m[0][1] + m[1][0]) / s, 0.25f * s, (m[1][2] + m[2][1]) / s, (m[0][2] - m[2][0]) / s};
    } else {
        const float s = std::sqrt(1.0f + m[2][2] - m[0][0] - m[1][1]) * 2.0f;
        q = {(m[0][2] + m[2][0]) / s, (m[1][2] + m[2][1]) / s, 0.25f * s, (m[1][0] - m[0][1]) / s};
    }
    return q;
}

void rowsFromQuat(Quat4 q, float m[3][3]) {
    const float x = q.x, y = q.y, z = q.z, w = q.w;
    m[0][0] = 1 - 2 * (y * y + z * z), m[0][1] = 2 * (x * y - z * w), m[0][2] = 2 * (x * z + y * w);
    m[1][0] = 2 * (x * y + z * w), m[1][1] = 1 - 2 * (x * x + z * z), m[1][2] = 2 * (y * z - x * w);
    m[2][0] = 2 * (x * z - y * w), m[2][1] = 2 * (y * z + x * w), m[2][2] = 1 - 2 * (x * x + y * y);
}

Quat4 slerp(Quat4 a, Quat4 b, float t) {
    float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (d < 0.0f) b = {-b.x, -b.y, -b.z, -b.w}, d = -d;
    float ka = 1.0f - t, kb = t;
    if (d < 0.9995f) {
        const float th = std::acos(d), sn = std::sin(th);
        ka = std::sin((1.0f - t) * th) / sn;
        kb = std::sin(t * th) / sn;
    }
    Quat4 q = {a.x * ka + b.x * kb, a.y * ka + b.y * kb, a.z * ka + b.z * kb, a.w * ka + b.w * kb};
    const float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return {q.x / n, q.y / n, q.z / n, q.w / n};
}

// Panda's HPR as a column-vector rotation: Rz(h) Rx(p) Ry(r).
void hprRows(float h, float p, float r, float m[3][3]) {
    const float ch = std::cos(h * kDeg), sh = std::sin(h * kDeg), cp = std::cos(p * kDeg), sp = std::sin(p * kDeg);
    const float cr = std::cos(r * kDeg), sr = std::sin(r * kDeg);
    const float a[3][3] = {{ch, -sh, 0}, {sh, ch, 0}, {0, 0, 1}};
    const float b[3][3] = {{1, 0, 0}, {0, cp, -sp}, {0, sp, cp}};
    const float c[3][3] = {{cr, 0, sr}, {0, 1, 0}, {-sr, 0, cr}};
    float ab[3][3];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) ab[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) m[i][j] = ab[i][0] * c[0][j] + ab[i][1] * c[1][j] + ab[i][2] * c[2][j];
}
} // namespace

bool RaceGame::init(const char* root) {
    sim_.init(1);
    selectedChoice_ = 1;
    choiceSubmitted_ = false;
    timer_ = 0.0f;
    numTurns_ = 0;
    turn_ = 0;
    step_ = Step::Idle;
    stepTime_ = stepLength_ = walkDelay_ = walkDuration_ = 0.0f;
    for (int i = 0; i < 4; ++i) walkFrom_[i] = walkTo_[i] = shownPos_[i] = 0;
    cardPlayer_ = -1;
    cardTextFor_ = -1;
    lane_ = 0;
    numRacers_ = 1;
    {
        // onstage: the top view until the first move; look 10 units along its heading/pitch.
        const float h = kTopView[3] * kDeg;
        const float p = kTopView[4] * kDeg;
        const float look[3] = {-std::sin(h) * std::cos(p), std::cos(h) * std::cos(p), std::sin(p)};
        for (int i = 0; i < 3; ++i) {
            camTo_[i] = kTopView[i];
            camTo_[3 + i] = kTopView[i] + look[i] * 10.0f;
        }
        for (int i = 0; i < 6; ++i) camFrom_[i] = camTo_[i];
        camTime_ = kCameraMoveSeconds;
    }

    char path[256];
    std::snprintf(path, sizeof(path), "%s/race_track.ttm", root);
    assetBatch_.load(trackModel_, path);
    std::snprintf(path, sizeof(path), "%s/race_chance_marker.ttm", root);
    assetBatch_.load(markerModel_, path);
    std::snprintf(path, sizeof(path), "%s/race_chance_card.ttm", root);
    assetBatch_.load(cardModel_, path);
    for (int i = 0; i < 4; ++i) markerPos_[i] = -1, markerHideIn_[i] = -1.0f;

    return true;
}

void RaceGame::shutdown() {
    assetBatch_.clear();
    trackModel_.unload();
    markerModel_.unload();
    cardModel_.unload();
    shutdownText();
}

void RaceGame::shutdownText() {
    if (cardBuf_) C2D_TextBufDelete(cardBuf_);
    cardBuf_ = nullptr;
    cardTextFor_ = -1;
}

void RaceGame::onSetChancePositions(const uint8_t* positions, int count) {
    for (int i = 0; i < count && i < 4; ++i) {
        sim_.setChance(i, positions[i], 19); // default reward
        markerPos_[i] = positions[i] < racegame::kNumPlaces ? positions[i] : -1;
        markerHideIn_[i] = -1.0f;
    }
}

void RaceGame::onSetServerChoices(const int8_t* choices, const uint8_t* positions, const int8_t* rewards, int count,
                                  int numPlayers) {
    numRacers_ = numPlayers < 1 ? 1 : numPlayers > 4 ? 4 : numPlayers;
    // A walk or card still playing from the last turn ends where it was going.
    if (step_ != Step::Idle)
        for (int i = 0; i < 4; ++i) shownPos_[i] = numTurns_ > 0 ? turnPos_[numTurns_ - 1][i] : shownPos_[i];
    numTurns_ = count / numRacers_;
    if (numTurns_ > kMaxTurns) numTurns_ = kMaxTurns;
    for (int t = 0; t < numTurns_; ++t)
        for (int i = 0; i < 4; ++i) {
            const int k = t * numRacers_ + i;
            const bool in = i < numRacers_;
            turnChoice_[t][i] = in ? choices[k] : 0;
            // setServerChoices clamps every position to 0..NumberToWin.
            turnPos_[t][i] = in ? (positions[k] > racegame::kNumberToWin ? racegame::kNumberToWin : positions[k])
                                : uint8_t(shownPos_[i]);
            turnReward_[t][i] = in && rewards ? rewards[k] : -1;
        }
    choiceSubmitted_ = false;
    if (numTurns_ == 0) {
        step_ = Step::Idle; // no one moved
        return;
    }
    for (int i = 0; i < numRacers_; ++i)
        if (turnPos_[numTurns_ - 1][i] >= racegame::kNumberToWin) sim_.isDone = true;
    startTurn(0);
}

void RaceGame::startTurn(int turn) {
    turn_ = turn;
    walkDuration_ = 0.0f;
    for (int i = 0; i < 4; ++i) {
        walkFrom_[i] = shownPos_[i];
        walkTo_[i] = turnPos_[turn][i];
        sim_.positions[i] = walkTo_[i]; // moveAvatars: avatarPositions, then moveCamera
        const float d = walkDuration(walkTo_[i] - walkFrom_[i]);
        if (i < numRacers_ && d > walkDuration_) walkDuration_ = d; // getLongestLerpTime
    }
    moveCamera();
    // Turn 0: showNumbers, 0.5 s, showMatches, 0.75 s, moveAvatars, 0.75 s, hideNumbers,
    // longest - 0.5 s. Later turns: moveAvatars, longest.
    walkDelay_ = turn == 0 ? 1.25f : 0.0f;
    const float after = turn == 0 ? 0.75f + (walkDuration_ > 0.5f ? walkDuration_ - 0.5f : 0.0f) : walkDuration_;
    stepLength_ = walkDelay_ + (after > walkDuration_ ? after : walkDuration_);
    stepTime_ = 0.0f;
    step_ = Step::Walk;
}

void RaceGame::startCard(int fromPlayer) {
    for (int i = fromPlayer; i < numRacers_; ++i) {
        if (turnReward_[turn_][i] < 0 || turnReward_[turn_][i] >= racegame::kNumRewards) continue;
        cardPlayer_ = i;
        markerPos_[i] = -1; // hideChanceMarker
        markerHideIn_[i] = -1.0f;
        step_ = Step::Card;
        stepTime_ = 0.0f;
        stepLength_ = kCardStep;
        return;
    }
    cardPlayer_ = -1;
    if (turn_ + 1 < numTurns_) {
        startTurn(turn_ + 1);
    } else {
        step_ = Step::Idle; // walkDone
    }
}

void RaceGame::nextStep() {
    if (step_ == Step::Walk) {
        for (int i = 0; i < 4; ++i) shownPos_[i] = walkTo_[i];
        startCard(0);
    } else if (step_ == Step::Card) {
        startCard(cardPlayer_ + 1);
    }
}

float RaceGame::lanePlace(int lane) const {
    if (lane < 0 || lane >= 4) return 0.0f;
    if (step_ != Step::Walk || walkDuration_ <= 0.0f || stepTime_ < walkDelay_) return float(shownPos_[lane]);
    const float d = walkDuration(walkTo_[lane] - walkFrom_[lane]);
    if (d <= 0.0f) return float(walkTo_[lane]);
    const float t = (stepTime_ - walkDelay_) / d;
    return walkFrom_[lane] + (walkTo_[lane] - walkFrom_[lane]) * (t > 1.0f ? 1.0f : t);
}

void RaceGame::update(net::Client& client, const circlePosition& pad, u32 keysDown, float dt, u32 nowMs) {
    (void)pad;
    (void)nowMs;

    if (step_ != Step::Idle) {
        stepTime_ += dt;
        if (stepTime_ >= stepLength_) nextStep();
    }
    if (camTime_ < kCameraMoveSeconds) camTime_ += dt;

    if (sim_.isDone) return;

    // inputChoice comes after walkDone: no move is chosen while the last turn still plays.
    if (!choiceSubmitted_ && step_ == Step::Idle) {
        if (keysDown & (KEY_DLEFT | KEY_Y)) {
            selectedChoice_ = (selectedChoice_ > 0) ? selectedChoice_ - 1 : 4;
        } else if (keysDown & (KEY_DRIGHT | KEY_A)) {
            selectedChoice_ = (selectedChoice_ < 4) ? selectedChoice_ + 1 : 0;
        }

        if (keysDown & (KEY_A | KEY_START)) {
            const int toChance = sim_.chancePositions[lane_] - shownPos_[lane_];
            if (autoAim_ && sim_.chancePositions[lane_] >= 0 && toChance >= 1)
                selectedChoice_ = toChance <= 4 ? toChance : toChance - 4 >= 1 && toChance - 4 <= 4 ? toChance - 4 : 4;
            choiceSubmitted_ = true;
            client.sendRaceChoice(selectedChoice_);

            // DistributedRaceGame.setServerChoices owns movement and the next turn.

        }
    }
}

void RaceGame::cardMatrix(C3D_Mtx* out) const {
    // showChanceCard: from render's origin, posQuatInterval(1.0, (19.62, 13.41, 13.14),
    // hpr (270, 0, -85.24), other=camera). The camera has no roll (lookAt).
    C3D_FVec eye, target;
    camera(&eye, &target);
    float f[3] = {target.x - eye.x, target.y - eye.y, target.z - eye.z};
    float n = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    if (n < 1e-4f) n = 1.0f;
    for (float& v : f) v /= n;
    float r[3] = {f[1], -f[0], 0.0f}; // forward x up(0, 0, 1)
    n = std::sqrt(r[0] * r[0] + r[1] * r[1]);
    if (n < 1e-4f) r[0] = 1.0f, r[1] = 0.0f, n = 1.0f;
    r[0] /= n, r[1] /= n;
    const float u[3] = {r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0]};
    const float cam[3][3] = {{r[0], f[0], u[0]}, {r[1], f[1], u[1]}, {r[2], f[2], u[2]}};
    float local[3][3], world[3][3];
    hprRows(270.0f, 0.0f, -85.24f, local);
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            world[i][j] = cam[i][0] * local[0][j] + cam[i][1] * local[1][j] + cam[i][2] * local[2][j];
    const float off[3] = {19.62f, 13.41f, 13.14f};
    float pos[3];
    for (int i = 0; i < 3; ++i) pos[i] = cam[i][0] * off[0] + cam[i][1] * off[1] + cam[i][2] * off[2];
    pos[0] += eye.x, pos[1] += eye.y, pos[2] += eye.z;
    const float t = stepTime_ >= kCardFly ? 1.0f : stepTime_ / kCardFly;
    float rot[3][3];
    const float identity[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    rowsFromQuat(slerp(quatFromRows(identity), quatFromRows(world), t), rot);
    Mtx_Identity(out);
    out->r[0].x = rot[0][0], out->r[0].y = rot[0][1], out->r[0].z = rot[0][2], out->r[0].w = pos[0] * t;
    out->r[1].x = rot[1][0], out->r[1].y = rot[1][1], out->r[1].z = rot[1][2], out->r[1].w = pos[1] * t;
    out->r[2].x = rot[2][0], out->r[2].y = rot[2][1], out->r[2].z = rot[2][2], out->r[2].w = pos[2] * t;
}

void RaceGame::draw(render::Renderer& renderer) {
    if (trackModel_.loaded()) {
        C3D_Mtx world;
        Mtx_Identity(&world);
        // DistributedRaceGame.load scales the board to 0.8.  Omitting this
        // made the board sit outside the upstream camera framing.
        Mtx_Scale(&world, 0.8f, 0.8f, 0.8f);
        renderer.drawModel(trackModel_, world);
    }
    if (markerModel_.loaded()) {
        // setPosHpr(raceBoard, x, y, z, h + 180, 0, 0.025), setScale(0.7), depth offset 1.
        for (int i = 0; i < 4; ++i) {
            if (markerPos_[i] < 0) continue;
            const float* p = racegame::kLanePosHpr[0][markerPos_[i]];
            C3D_Mtx m;
            Mtx_Identity(&m);
            Mtx_Translate(&m, p[0] * 0.8f, p[1] * 0.8f, p[2] * 0.8f + 0.02f, true);
            Mtx_RotateZ(&m, (p[3] + 180.0f) * kDeg, true);
            Mtx_RotateY(&m, 0.025f * kDeg, true);
            Mtx_Scale(&m, 0.7f, 0.7f, 0.7f);
            renderer.drawModel(markerModel_, m);
        }
    }
    // The card is parented to render (not the board) and hidden until showChanceCard.
    if (cardModel_.loaded() && step_ == Step::Card && stepTime_ < kCardEnd) {
        C3D_Mtx m;
        cardMatrix(&m);
        renderer.drawModel(cardModel_, m);
    }
}

bool RaceGame::cardUp() const {
    return step_ == Step::Card && stepTime_ >= kCardFly * 0.5f && stepTime_ < kCardEnd;
}

void RaceGame::draw2D(C3D_RenderTarget* target, const char* (*nameOf)(int lane, void* ctx), void* ctx) {
    (void)target;
    if (step_ != Step::Card || stepTime_ < kCardText || stepTime_ >= kCardEnd || cardPlayer_ < 0) return;
    const int reward = turnReward_[turn_][cardPlayer_];
    const int key = turn_ * 4 + cardPlayer_;
    if (!cardBuf_) cardBuf_ = C2D_TextBufNew(160);
    if (!cardBuf_) return;
    if (key != cardTextFor_) {
        const char* name = nameOf ? nameOf(cardPlayer_, ctx) : nullptr;
        if (!name || !name[0]) name = "Toon";
        char text[128];
        const char* what = kRewardText[reward];
        if (!what) {
            std::snprintf(text, sizeof(text), "%s is one Fabulous Toon!", name); // RaceGameCardTextHi1
        } else if (racegame::kChanceRewards[reward].beanBonus > 0) {
            std::snprintf(text, sizeof(text), "%s receives: %s", name, what); // RaceGameCardTextBeans
        } else {
            std::snprintf(text, sizeof(text), "%s draws: %s", name, what); // RaceGameCardText
        }
        C2D_TextBufClear(cardBuf_);
        render::textParseSign(&cardText_, cardBuf_, text);
        C2D_TextOptimize(&cardText_);
        cardTextFor_ = key;
    }
    // aspect2d on the 400 x 240 top screen: one unit is 120 px. Scale 0.14 is a 16.8 px line
    // (render/ui_font.h: 30 px at scale 1), wordwrap 14 lines is 235 px, centred on x 0;
    // pos y 0.2 is the first line's baseline.
    constexpr float kUnitPx = 120.0f, kFontLinePx = 30.0f, kBaseline = 0.8f;
    const float scale = 0.14f * kUnitPx / kFontLinePx;
    const float wrap = 14.0f * 0.14f * kUnitPx;
    const float baseline = 120.0f - 0.2f * kUnitPx;
    C2D_DrawText(&cardText_, C2D_WithColor | C2D_AlignCenter | C2D_WordWrap, 200.0f, baseline - kBaseline * kFontLinePx * scale,
                 0.5f, scale, scale, C2D_Color32f(1.0f, 0.0f, 0.0f, 1.0f), wrap);
}

void RaceGame::camera(C3D_FVec* eye, C3D_FVec* target) const {
    const float t = camTime_ / kCameraMoveSeconds > 1.0f ? 1.0f : camTime_ / kCameraMoveSeconds;
    float v[6];
    for (int i = 0; i < 6; ++i) v[i] = camFrom_[i] + (camTo_[i] - camFrom_[i]) * t;
    if (eye) *eye = FVec4_New(v[0], v[1], v[2], 1.0f);
    if (target) *target = FVec4_New(v[3], v[4], v[5], 1.0f);
}

void RaceGame::moveCamera() {
    // DistributedRaceGame.moveCamera, positions in posHprArray units (render space, as upstream).
    using racegame::kLanePosHpr;
    using racegame::kNumberToWin;
    int best = sim_.positions[0];
    int bestLane = 0;
    for (int lane = 0; lane < numRacers_ && lane < racegame::kNumLanes; ++lane) {
        if (sim_.positions[lane] > best) {
            best = sim_.positions[lane];
            bestLane = lane;
        }
    }
    best = best < kNumberToWin ? best : kNumberToWin;
    const int own = sim_.positions[lane_];
    const float* pos1 = kLanePosHpr[lane_][own < kNumberToWin - 4 ? own : kNumberToWin - 4];
    const int bestLookAt = best + 1;
    const int ownLookAt = own + 4;
    const float* pos2;
    if (ownLookAt >= bestLookAt) {
        pos2 = kLanePosHpr[lane_][ownLookAt < kNumberToWin ? ownLookAt : kNumberToWin];
    } else {
        pos2 = kLanePosHpr[bestLane][bestLookAt < kNumberToWin ? bestLookAt : kNumberToWin];
    }
    constexpr float kDistanceMultiplier = 0.8f;
    const float raceFraction = float(best) / float(kNumberToWin);
    const float height = 10.0f * raceFraction + (1.0f - raceFraction) * 22.0f;
    const float* lookAt = kLanePosHpr[lane_][own < kNumberToWin - 6 ? own : kNumberToWin - 6];
    for (int i = 0; i < 6; ++i) camFrom_[i] = camFrom_[i] + (camTo_[i] - camFrom_[i]) *
                                     (camTime_ / kCameraMoveSeconds > 1.0f ? 1.0f : camTime_ / kCameraMoveSeconds);
    camTo_[0] = pos2[0] + kDistanceMultiplier * (pos2[0] - pos1[0]);
    camTo_[1] = pos2[1] + kDistanceMultiplier * (pos2[1] - pos1[1]);
    camTo_[2] = pos2[2] + height;
    camTo_[3] = lookAt[0];
    camTo_[4] = lookAt[1];
    camTo_[5] = lookAt[2];
    camTime_ = 0.0f;
}

void RaceGame::toonPose(float* x, float* y, float* z, float* h) const {
    float px, py, pz, ph;
    lanePose(lane_, &px, &py, &pz, &ph);
    if (x) *x = px;
    if (y) *y = py;
    if (z) *z = pz;
    if (h) *h = ph;
}

void RaceGame::lanePose(int lane, float* x, float* y, float* z, float* h) const {
    if (lane < 0 || lane >= racegame::kNumLanes) lane = 0;
    float position = lanePlace(lane);
    if (position < 0.0f) position = 0.0f;
    if (position > float(racegame::kNumPlaces - 1)) position = float(racegame::kNumPlaces - 1);
    // DistributedRaceGame.positionInPlace / walkInPlace: the lane's posHprArray entries, relative
    // to the race board (scaled 0.8 in load); between places the toon walks the straight segment.
    const int from = int(position);
    const int to = from + 1 < racegame::kNumPlaces ? from + 1 : from;
    const float f = position - float(from);
    const float* a = racegame::kLanePosHpr[lane][from];
    const float* b = racegame::kLanePosHpr[lane][to];
    constexpr float kBoardScale = 0.8f; // DistributedRaceGame.load: raceBoard.setScale(0.8)
    *x = (a[0] + (b[0] - a[0]) * f) * kBoardScale;
    *y = (a[1] + (b[1] - a[1]) * f) * kBoardScale;
    *z = (a[2] + (b[2] - a[2]) * f) * kBoardScale;
    *h = a[3] + (b[3] - a[3]) * f;
}

const char* RaceGame::line(int i) const {
    if (i == 0) {
        std::snprintf(lines_[0], sizeof(lines_[0]), "Position: %d / %d", shownPos_[lane_], racegame::kNumberToWin);
        return lines_[0];
    }
    if (i == 1) {
        std::snprintf(lines_[1], sizeof(lines_[1]), "Select Move: %d spaces (Left/Right, A: Move)", selectedChoice_);
        return lines_[1];
    }
    if (i == 2) {
        std::snprintf(lines_[2], sizeof(lines_[2]), "Score: %d beans", sim_.scores[0]);
        return lines_[2];
    }
    return "";
}

} // namespace tt::game
