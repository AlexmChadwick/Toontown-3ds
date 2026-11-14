#include "world/race_town.h"
#include "core/gpu_sync.h"
#include "core/log.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace tt::world {
namespace {
constexpr const char *kDir = "sdmc:/3ds/toontown3ds/data/racing";
constexpr int kActive = 4;            // never fill the shared loader's slots
constexpr int kMaxSideStreets = 8;    // innersidest + outersidest groups resident
constexpr float kSideStreetReach = 950; // precomputeSideStreets: camera far 650 + 300
LoadTicket skylineTicket = kNoTicket;
} // namespace

void RaceTown::load(uint16_t track, const char *trackName) {
    unload();
    // RaceGlobals: RT_Urban_1 = 40, RT_Urban_2 = 64 (+1 reversed).
    if (track != 40 && track != 41 && track != 64 && track != 65)
        return;
    key_ = track >= 64 ? 'B' : 'A';
    reversed_ = track & 1;
    nearestWalk_ = track >= 64; // showBuildings' nearest-of-three walk is RT_Urban_2 only
    char path[96];
    std::snprintf(path, sizeof(path), "%s/town_%c.trt", kDir, key_);
    FILE *f = std::fopen(path, "rb");
    if (!f) {
        TT_LOG("racetown: missing %s", path);
        return;
    }
    struct {
        char magic[4];
        uint16_t version, flags, counts[4], reserved;
    } header;
    static_assert(sizeof(header) == 18, "TRT1 header");
    bool ok = std::fread(&header, sizeof(header), 1, f) == 1 && !std::memcmp(header.magic, "TRT1", 4) &&
              header.version == 1;
    int total = 0;
    for (int s = 0; ok && s < 4; ++s) {
        first_[s] = total;
        count_[s] = header.counts[s];
        total += header.counts[s];
    }
    ok &= total > 0 && total <= kMaxGroups;
    for (int i = 0; ok && i < total; ++i) {
        float rec[4];
        uint32_t extra[2];
        ok = std::fread(rec, sizeof(rec), 1, f) == 1 && std::fread(extra, sizeof(extra), 1, f) == 1 &&
             std::isfinite(rec[0]) && std::isfinite(rec[1]) && std::isfinite(rec[2]) && std::isfinite(rec[3]);
        if (!ok)
            break;
        group_[i] = {{rec[0], rec[1], rec[2]}, rec[3], 0};
        for (int s = 0; s < 4; ++s)
            if (i >= first_[s] && i < first_[s] + count_[s])
                group_[i].side = uint8_t(s);
    }
    std::fclose(f);
    if (!ok) {
        TT_LOG("racetown: bad index %s", path);
        return;
    }
    groups_ = total;
    std::snprintf(path, sizeof(path), "%s/%s_bg.ttm", kDir, trackName);
    skylineTicket = AssetLoader::requestModel(path);
    TT_LOG("racetown: track %u town %c groups %d/%d/%d/%d", track, key_, count_[0], count_[1], count_[2],
           count_[3]);
}

void RaceTown::unload() {
    bool resident = skyline_.loaded();
    for (auto &slot : slots_)
        resident |= slot.model.loaded();
    if (resident)
        ::tt::gpu::frameSync();
    for (auto &slot : slots_) {
        if (slot.ticket)
            AssetLoader::cancel(slot.ticket);
        slot.model.unload();
        slot.group = slot.want = -1;
        slot.ticket = kNoTicket;
        slot.drop = false;
    }
    if (skylineTicket)
        AssetLoader::cancel(skylineTicket);
    skylineTicket = kNoTicket;
    skyline_.unload();
    groups_ = numWant_ = numShown_ = 0;
    cur_[0] = cur_[1] = -1;
    std::memset(failed_, 0, sizeof(failed_));
}

bool RaceTown::wanted(int g) const {
    for (int i = 0; i < numWant_; ++i)
        if (want_[i] == g)
            return true;
    return false;
}

bool RaceTown::shown(int g) const {
    for (int i = 0; i < numShown_; ++i)
        if (want_[i] == g)
            return true;
    return false;
}

void RaceTown::update(float x, float y, float t) {
    if (!groups_)
        return;
    if (reversed_)
        t = 1.0f - t;
    int prefetch[4], numPrefetch = 0;
    numWant_ = 0;
    const auto add = [this](int g) {
        if (numWant_ < kSlots && !wanted(g))
            want_[numWant_++] = g;
    };
    const auto dist2 = [this, x, y](int g) {
        const float dx = group_[g].c[0] - x, dy = group_[g].c[1] - y;
        return dx * dx + dy * dy;
    };
    for (int s = 0; s < 2; ++s) {
        const int n = count_[s];
        if (!n)
            continue;
        int ind = int(t * n) % n;
        if (ind < 0)
            ind += n;
        if (nearestWalk_) {
            // Stay on the current group unless the previous or next one is nearer the kart.
            const int cur = cur_[s] < 0 ? 0 : cur_[s];
            const int prev = (cur + n - 1) % n, next = (cur + 1) % n;
            const float dc = dist2(first_[s] + cur), dp = dist2(first_[s] + prev), dn = dist2(first_[s] + next);
            ind = dc <= dp && dc <= dn ? cur : dp <= dc && dp <= dn ? prev : next;
        }
        cur_[s] = ind;
        for (int d = -2; d <= 2; ++d)
            add(first_[s] + (ind + d + 2 * n) % n);
        prefetch[numPrefetch++] = first_[s] + (ind + 3 * n - 3) % n;
        prefetch[numPrefetch++] = first_[s] + (ind + 3) % n;
    }
    // Side streets (RT_Urban_2): the groups near the kart, nearest first.
    int near[kMaxSideStreets], numNear = 0;
    for (int g = first_[2]; g < first_[2] + count_[2] + count_[3]; ++g) {
        const float d = dist2(g);
        if (d > kSideStreetReach * kSideStreetReach)
            continue;
        int at = numNear < kMaxSideStreets ? numNear++ : kMaxSideStreets;
        if (at == kMaxSideStreets && d >= dist2(near[kMaxSideStreets - 1]))
            continue;
        at = at == kMaxSideStreets ? kMaxSideStreets - 1 : at;
        while (at > 0 && dist2(near[at - 1]) > d) {
            near[at] = near[at - 1];
            --at;
        }
        near[at] = g;
    }
    for (int i = 0; i < numNear; ++i)
        add(near[i]);
    numShown_ = numWant_;
    for (int i = 0; i < numPrefetch; ++i)
        add(prefetch[i]);

    for (auto &slot : slots_) {
        const int target = slot.want >= 0 ? slot.want : slot.group;
        if (target < 0 || slot.drop || wanted(target))
            continue;
        if (slot.ticket)
            AssetLoader::cancel(slot.ticket);
        slot.ticket = kNoTicket;
        slot.want = -1;
        slot.drop = slot.group >= 0;
    }
    for (int i = 0; i < numWant_; ++i) {
        const int g = want_[i];
        if (failed_[g])
            continue;
        Slot *free = nullptr;
        bool held = false;
        for (auto &slot : slots_) {
            held |= slot.want == g || (slot.group == g && slot.want < 0 && !slot.drop);
            if (!free && slot.want < 0 && (slot.group < 0 || slot.drop))
                free = &slot;
        }
        if (held || !free)
            continue;
        free->want = g;
    }
}

void RaceTown::pump() {
    if (skylineTicket) {
        const auto status = AssetLoader::poll(skylineTicket);
        if (status == LoadStatus::Ready) {
            skyline_.unload();
            AssetLoader::takeModel(skylineTicket, skyline_);
            skylineTicket = kNoTicket;
        } else if (status != LoadStatus::Pending) {
            AssetLoader::cancel(skylineTicket);
            skylineTicket = kNoTicket;
            TT_LOG("racetown: skyline failed");
        }
    }
    if (!groups_)
        return;
    int active = skylineTicket ? 1 : 0;
    for (auto &slot : slots_) {
        if (!slot.ticket)
            continue;
        const auto status = AssetLoader::poll(slot.ticket);
        if (status == LoadStatus::Pending) {
            ++active;
            continue;
        }
        if (status == LoadStatus::Ready && AssetLoader::takeModel(slot.ticket, slot.model)) {
            slot.group = slot.want;
        } else {
            AssetLoader::cancel(slot.ticket);
            failed_[slot.want] = true;
            TT_LOG("racetown: group %c %d failed", key_, slot.want);
        }
        slot.ticket = kNoTicket;
        slot.want = -1;
        slot.drop = false;
    }
    for (auto &slot : slots_) {
        if (slot.drop && slot.want < 0) {
            slot.model.unload();
            slot.group = -1;
            slot.drop = false;
        }
    }
    // Shown groups first, then the preloads (want_ order).
    for (int i = 0; i < numWant_ && active < kActive; ++i) {
        for (auto &slot : slots_) {
            if (slot.want != want_[i] || slot.ticket)
                continue;
            slot.model.unload(); // at the fence: the replaced group is not drawn this frame
            slot.group = -1;
            slot.drop = false;
            char path[96];
            std::snprintf(path, sizeof(path), "%s/town_%c_%d.ttm", kDir, key_, slot.want);
            slot.ticket = AssetLoader::requestModel(path);
            if (!slot.ticket)
                return; // every shared slot busy: next frame
            ++active;
        }
    }
}

void RaceTown::drawSkyline(render::Renderer &renderer, float x, float y) {
    if (!skyline_.loaded())
        return;
    // loadUrbanTrack: the dummy node at (kart x, y, 0), CompassEffect PRot, bgGeom z -1.
    C3D_Mtx m;
    Mtx_Identity(&m);
    Mtx_Translate(&m, x, y, -1, true);
    renderer.drawSky(skyline_, m, true); // not setFogOff like the sky: the track fog greys it
}

void RaceTown::draw(render::Renderer &renderer) {
    C3D_Mtx m;
    Mtx_Identity(&m);
    for (auto &slot : slots_)
        if (slot.group >= 0 && !slot.drop && slot.model.loaded() && shown(slot.group))
            renderer.drawModel(slot.model, m);
}
} // namespace tt::world
