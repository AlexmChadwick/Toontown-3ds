#pragma once
#include "net/client.h"
#include "render/model.h"
#include "render/renderer.h"
#include "world/asset_batch.h"
namespace tt::world {
class Estate {
public:
    void handleEvent(const net::Event&, net::Client&);
    void update(net::Client&, float, float, float, uint32_t);
    void draw(render::Renderer&, const net::Client&) const;
    void shutdown();
    void pumpAssets() {
        assetBatch_.pump();
    }
    bool loadingAssets() const {
        return assetBatch_.loading();
    }
    bool active() const { return active_; }
    bool interior() const { return inInterior_; }
private:
    BasicAssetBatch<16> assetBatch_;
    void loadExterior();
    void loadInterior();
    void unload();
    render::Model terrain_, interior_, houses_[6], furniture_[4];
    bool active_ = false, inInterior_ = false;
    uint32_t lastInteract_ = 0;
};
}
