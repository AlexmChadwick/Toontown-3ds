#include "world/asset_batch.h"
#include <cassert>
#include <cstdio>
using namespace tt::world;
namespace {
LoadStatus status[2000];
int nextTicket = 0, active = 0, peak = 0, published = 0, cancelled = 0;
LoadTicket request() {
    ++active;
    if (active > peak)
        peak = active;
    status[++nextTicket] = LoadStatus::Pending;
    return nextTicket;
}
} // namespace
LoadTicket AssetLoader::requestModel(const char *) {
    return request();
}
LoadTicket AssetLoader::requestAnim(const char *) {
    return request();
}
LoadStatus AssetLoader::poll(LoadTicket ticket) {
    return status[ticket];
}
void AssetLoader::cancel(LoadTicket ticket) {
    status[ticket] = LoadStatus::Invalid;
    --active;
    ++cancelled;
}
bool AssetLoader::takeModel(LoadTicket ticket, tt::render::Model &model) {
    assert(status[ticket] == LoadStatus::Ready);
    status[ticket] = LoadStatus::Invalid;
    --active;
    ++published;
    model.ready = true;
    return true;
}
bool AssetLoader::takeAnim(LoadTicket ticket, tt::render::Anim &anim) {
    assert(status[ticket] == LoadStatus::Ready);
    status[ticket] = LoadStatus::Invalid;
    --active;
    ++published;
    anim.ready = true;
    return true;
}
int main() {
    AssetBatch batch;
    tt::render::Model models[160];
    tt::render::Anim anim;
    for (auto &model : models)
        assert(batch.load(model, "model.ttm"));
    assert(batch.load(models[0], "model.ttm"));
    assert(!batch.load(anim, "full.tta"));
    int frames = 0;
    while (batch.loading()) {
        batch.pump();
        assert(active <= 4);
        for (int i = 1; i <= nextTicket; ++i)
            if (status[i] == LoadStatus::Pending)
                status[i] = LoadStatus::Ready;
        assert(++frames < 100);
    }
    for (const auto &model : models)
        assert(model.ready);
    assert(published == 160 && peak == 4 && active == 0);
    batch.clear();
    assert(batch.load(anim, "clip.tta"));
    batch.pump();
    assert(active == 1);
    batch.clear();
    assert(active == 0 && cancelled == 1 && !anim.ready);
    assert(batch.load(anim, "bad.tta"));
    batch.pump();
    status[nextTicket] = LoadStatus::Failed;
    batch.pump();
    assert(!batch.loading() && active == 0 && !anim.ready);
    batch.clear();
    assert(batch.load(anim, "clip.tta"));
    batch.pump();
    status[nextTicket] = LoadStatus::Ready;
    batch.pump();
    assert(anim.ready);
    assert(batch.load(anim, "replacement.tta"));
    batch.pump();
    assert(!anim.ready);
    status[nextTicket] = LoadStatus::Ready;
    batch.pump();
    assert(anim.ready);
    std::printf("AssetBatch 160 destinations: direct main-thread loads 160 -> 0, peak staging 4, "
                "admissions/publications spread over %d frames; cancellation/failure/full/reuse PASS; "
                "descriptor bytes %zu\n",
                frames, sizeof(batch));
}
