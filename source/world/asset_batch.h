#pragma once
#include "core/log.h"
#include "world/asset_loader.h"
#include <cstdio>
#include <cstring>
namespace tt::world {
// Main-thread descriptors only. Shared AssetLoader owns all IO and staging.
// Publish exclusively after the GPU fence; destinations outlive this batch.
template <int Capacity> class BasicAssetBatch {
public:
    static constexpr int kCapacity = Capacity;
    static constexpr int kActive = 4;
    bool load(render::Model &model, const char *path) {
        return add(&model, nullptr, path);
    }
    bool load(render::Anim &anim, const char *path) {
        return add(nullptr, &anim, path);
    }
    void clear() {
        for (int i = 0; i < count_; ++i)
            if (jobs_[i].ticket)
                AssetLoader::cancel(jobs_[i].ticket);
        count_ = remaining_ = 0;
    }
    bool loading() const {
        return remaining_ > 0;
    }
    void pump() {
        int active = 0;
        for (int i = 0; i < count_; ++i) {
            Job &job = jobs_[i];
            if (!job.ticket)
                continue;
            const auto status = AssetLoader::poll(job.ticket);
            if (status == LoadStatus::Pending) {
                ++active;
                continue;
            }
            const bool ready =
                status == LoadStatus::Ready && (job.model ? AssetLoader::takeModel(job.ticket, *job.model)
                                                          : AssetLoader::takeAnim(job.ticket, *job.anim));
            if (!ready) {
                AssetLoader::cancel(job.ticket);
                TT_LOG("assets: batch failed %s", job.path);
            }
            job.ticket = 0;
            job.done = true;
            --remaining_;
        }
        // Bound admissions as well as publications. Never fill all shared slots.
        for (int i = 0; i < count_ && active < kActive; ++i) {
            Job &job = jobs_[i];
            if (job.done || job.ticket)
                continue;
            // Discard a replaced destination only at the fence, before its new
            // IO starts; replacement does not double its resident GPU memory.
            if (job.model && job.model->loaded())
                job.model->unload();
            if (job.anim && job.anim->loaded())
                job.anim->unload();
            job.ticket = job.model ? AssetLoader::requestModel(job.path) : AssetLoader::requestAnim(job.path);
            if (!job.ticket)
                break;
            ++active;
        }
    }

private:
    struct Job {
        render::Model *model = nullptr;
        render::Anim *anim = nullptr;
        LoadTicket ticket = 0;
        bool done = false;
        char path[112] = {};
    };
    bool add(render::Model *model, render::Anim *anim, const char *path) {
        for (int i = 0; i < count_; ++i) {
            Job &previous = jobs_[i];
            if (previous.model != model || previous.anim != anim)
                continue;
            if (!std::strcmp(previous.path, path))
                return true;
            if (std::strlen(path) >= sizeof(previous.path))
                return false;
            if (previous.ticket)
                AssetLoader::cancel(previous.ticket);
            previous.ticket = 0;
            if (previous.done)
                ++remaining_;
            previous.done = false;
            std::snprintf(previous.path, sizeof(previous.path), "%s", path);
            return true;
        }
        if (count_ == kCapacity || std::strlen(path) >= sizeof(Job::path))
            return false;
        Job &job = jobs_[count_++];
        job = Job{};
        job.model = model;
        job.anim = anim;
        std::snprintf(job.path, sizeof(job.path), "%s", path);
        ++remaining_;
        return true;
    }
    Job jobs_[kCapacity];
    int count_ = 0;
    int remaining_ = 0;
};
using AssetBatch = BasicAssetBatch<160>;
} // namespace tt::world
