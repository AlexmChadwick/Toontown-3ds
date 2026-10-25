#pragma once
#include "net/client.h"
#include "render/model.h"
#include "render/renderer.h"
#include "world/asset_batch.h"
namespace tt::world {
class Pets {
public:
    void pumpAssets() {
        assetBatch_.pump();
        loaded_ = doodle_.loaded();
    }
    void handleEvent(const net::Event&);
    void update(const net::Client&);
    void draw(render::Renderer&, const net::Client&) const;
    void shutdown();
private:
    BasicAssetBatch<2> assetBatch_;
    render::Model doodle_;
    bool loaded_ = false;
};
}
