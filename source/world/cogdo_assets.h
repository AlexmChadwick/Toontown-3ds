#pragma once
#include "world/asset_loader.h"
#include "world/collision.h"
namespace tt::world {
enum class CogdoScene : uint8_t { None, Maze, Suite, Barrels, Crane, Boardroom, BattleFloor };
class CogdoAssets {
public:
    static constexpr int kMaxModels = 16;
    ~CogdoAssets();
    void select(CogdoScene scene); // Main thread after FrameBegin fence.
    void pump();
    void release();
    bool ready() const {
        return ready_;
    }
    bool failed() const {
        return failed_;
    }
    CogdoScene scene() const {
        return scene_;
    }
    int count() const;
    render::Model models[kMaxModels];
    CollisionWorld collision;
    Vec3 exteriorDoor{};
    Vec3 mazeEntrance{};
    Vec3 mazeExit{};
    Vec3 cage{};
    Vec3 cageDoor{};

private:
    const char *name(int index) const;
    bool budgets() const;
    bool points();
    CogdoScene scene_ = CogdoScene::None;
    LoadTicket ticket_ = 0;
    int index_ = 0;
    bool checked_ = false;
    bool ready_ = false;
    bool failed_ = false;
};
} // namespace tt::world
