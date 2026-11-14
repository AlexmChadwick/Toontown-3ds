#include "world/cashbot_trains.h"

#include <cmath>
#include <cstdlib>

#include "core/log.h"
#include "net/client.h"
#include "render/renderer.h"
#include "world/py_random.h"

namespace tt::world {

namespace {

constexpr float kMarkDelta = 15.0f; // Train.MarkDelta
constexpr float kCarLength = 88.0f; // Train.CarLength
constexpr float kTrackZ = -67.0f;   // CashbotHQExterior.TrackZ
constexpr float kStopWait = 3.0f;   // waitTime of a run that stops
constexpr float kStopX = 150.0f;    // halfway.setX(150)
constexpr uint8_t kSquishDamage = 10;
constexpr float kStunSeconds = 4.0f; // Toon.stunToon's blinks

// CashbotHQExterior.TrainTracks
struct TrackDef {
    float startX, endX, y;
};
constexpr TrackDef kTrack[4] = {{-1000.0f, 2200.0f, -54.45f},
                                {1800.0f, -1200.0f, -133.45f},
                                {-1000.0f, 2200.0f, -212.45f},
                                {1800.0f, -1200.0f, -291.45f}};

// Each model's wall box (its CollisionNode's four polygons), model space.
struct Box {
    float x0, x1, y0, y1, z0, z1;
};
constexpr Box kLocoBox = {-0.448456f, 90.1416f, -7.93934f, 7.85312f, -0.131093f, 59.6317f};
constexpr Box kCarBox[3] = {{-0.697162f, 76.4368f, -7.7253f, 7.7253f, 0.101299f, 55.5363f},  // CashBotBoxCar
                            {-1.74657f, 90.266f, -7.86517f, 7.79166f, -0.131093f, 56.9073f}, // CashBotTankCar
                            {-0.855255f, 88.6467f, -8.89205f, 7.8339f, -0.20094f, 16.4404f}}; // CashBotFlatCar

// CLerpInterval blend types.
float easeIn(float t) { return (3.0f * t * t - t * t * t) * 0.5f; }
float easeInOut(float t) { return 3.0f * t * t - 2.0f * t * t * t; }

bool flipped(int track) { return kTrack[track].startX < kTrack[track].endX; } // locomotive.setHpr(180, 0, 0)

} // namespace

void CashbotTrains::setZone(uint32_t zoneId) {
    present_ = zoneId == 12000;
    for (Train& t : trains_) t = Train{};
    if (!present_ || loaded_) return;
    static const char* const kCars[3] = {"car_0", "car_1", "car_2"};
    bool ok = loco_.load("sdmc:/3ds/toontown3ds/data/zones/12000_train_locomotive.ttm");
    for (int i = 0; i < 3; ++i) {
        char path[96];
        snprintf(path, sizeof(path), "sdmc:/3ds/toontown3ds/data/zones/12000_train_%s.ttm", kCars[i]);
        ok = cars_[i].load(path) && ok;
    }
    if (!ok) TT_LOG("cashbot trains: missing 12000_train_*.ttm");
    loaded_ = true;
    solids_ = DynamicSolids{};
    solids_.push = &CashbotTrains::push;
    solids_.user = this;
}

void CashbotTrains::shutdown() {
    loco_.unload();
    for (render::Model& m : cars_) m.unload();
    loaded_ = present_ = false;
}

// Train.__getNextRun with random.seed(trainId + runNumber).
void CashbotTrains::place(int track, double t) {
    Train& tr = trains_[track];
    const double firstMark = kMarkDelta / 4.0 * track;
    // currentRun = int((currentTime - firstMark) / MarkDelta): truncation, as Python's int().
    const int number = static_cast<int>((t - firstMark) / kMarkDelta);
    if (number != tr.run.number) {
        Run run;
        run.number = number;
        PyRandom rng(static_cast<uint64_t>(std::llabs(static_cast<long long>(track) + number)));
        run.cars = rng.randrange(1, 10);
        for (int c = 0; c < run.cars; ++c) run.type[c] = static_cast<uint8_t>(rng.randrange(0, 3));
        run.stop = rng.randrange(0, 4) == 0;
        // randrange(4, (MarkDelta - waitTime) / 2) or randrange(6, MarkDelta - 1)
        run.totalTime = run.stop ? rng.randrange(4, 6) : rng.randrange(6, 14);
        tr.run = run;
    }
    const float e = static_cast<float>(t - (firstMark + kMarkDelta * number));
    const TrackDef& d = kTrack[track];
    tr.y = d.y;
    tr.shown = e >= 0.0f; // before its first mark the locomotive has not left
    const float T = static_cast<float>(tr.run.totalTime);
    if (tr.run.stop) {
        if (e < T) tr.x = d.startX + (kStopX - d.startX) * easeInOut(e / T);
        else if (e < T + kStopWait) tr.x = kStopX;
        else if (e < 2.0f * T + kStopWait) tr.x = kStopX + (d.endX - kStopX) * easeIn((e - T - kStopWait) / T);
        else tr.x = d.endX;
    } else {
        tr.x = e < T ? d.startX + (d.endX - d.startX) * (e / T) : d.endX;
    }
}

bool CashbotTrains::update(net::Client& client, float x, float y, float z, uint32_t nowMs) {
    if (!present_ || !client.timeSynced()) {
        for (Train& t : trains_) t.shown = false;
        return false;
    }
    const double now = client.networkSeconds(nowMs);
    for (int i = 0; i < kTracks; ++i) place(i, now);
    // Train.__handleCollisionSphereEnter -> b_squish(10), refused while stunned.
    if (now < stunnedUntil_) return false;
    constexpr float kR = kToonRadius;
    for (int i = 0; i < kTracks; ++i) {
        const Train& tr = trains_[i];
        if (!tr.shown) continue;
        const float s = flipped(i) ? -1.0f : 1.0f;
        const float lx = (x - tr.x) * s, ly = (y - tr.y) * s, lz = z - kTrackZ;
        for (int m = 0; m <= tr.run.cars; ++m) {
            const Box& b = m == 0 ? kLocoBox : kCarBox[tr.run.type[m - 1]];
            const float ox = m == 0 ? 0.0f : kCarLength * m;
            if (lx < ox + b.x0 - kR || lx > ox + b.x1 + kR || ly < b.y0 - kR || ly > b.y1 + kR ||
                lz + 2.0f * kR < b.z0 || lz > b.z1)
                continue;
            if (!client.sendSquish(kSquishDamage)) return false;
            stunnedUntil_ = now + kStunSeconds;
            TT_LOG("cashbot trains: squished by the train on track %d", i);
            return true;
        }
    }
    return false;
}

// The train's sides are walls: a Toon inside one goes out the nearer side, off the track.
void CashbotTrains::push(void* user, float* x, float* y, float z, float radius) {
    const CashbotTrains& self = *static_cast<const CashbotTrains*>(user);
    for (int i = 0; i < kTracks; ++i) {
        const Train& tr = self.trains_[i];
        if (!tr.shown) continue;
        const float s = flipped(i) ? -1.0f : 1.0f;
        const float lx = (*x - tr.x) * s, ly = (*y - tr.y) * s, lz = z - kTrackZ;
        for (int m = 0; m <= tr.run.cars; ++m) {
            const Box& b = m == 0 ? kLocoBox : kCarBox[tr.run.type[m - 1]];
            const float ox = m == 0 ? 0.0f : kCarLength * m;
            if (lx < ox + b.x0 - radius || lx > ox + b.x1 + radius || ly < b.y0 - radius || ly > b.y1 + radius ||
                lz + 2.0f * radius < b.z0 || lz > b.z1)
                continue;
            const float out = (ly - b.y0 < b.y1 - ly) ? b.y0 - radius : b.y1 + radius;
            *y = tr.y + out * s;
        }
    }
}

void CashbotTrains::draw(render::Renderer& renderer) const {
    if (!present_) return;
    for (int i = 0; i < kTracks; ++i) {
        const Train& tr = trains_[i];
        if (!tr.shown) continue;
        C3D_Mtx m;
        Mtx_Identity(&m);
        Mtx_Translate(&m, tr.x, tr.y, kTrackZ, true);
        if (flipped(i)) Mtx_RotateZ(&m, 3.14159265f, true);
        if (loco_.loaded()) renderer.drawModel(const_cast<render::Model&>(loco_), m);
        for (int c = 0; c < tr.run.cars; ++c) {
            const render::Model& car = cars_[tr.run.type[c]];
            if (!car.loaded()) continue;
            C3D_Mtx cm = m;
            Mtx_Translate(&cm, kCarLength * (c + 1), 0.0f, 0.0f, true);
            renderer.drawModel(const_cast<render::Model&>(car), cm);
        }
    }
}

} // namespace tt::world
