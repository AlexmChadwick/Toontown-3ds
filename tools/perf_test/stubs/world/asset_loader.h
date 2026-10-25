#pragma once
#include <cstdint>
namespace tt::render {
struct Model {
    bool ready = false;
    bool loaded() const {
        return ready;
    }
    void unload() {
        ready = false;
    }
};
struct Anim {
    bool ready = false;
    bool loaded() const {
        return ready;
    }
    void unload() {
        ready = false;
    }
};
} // namespace tt::render
namespace tt::world {
using LoadTicket = uint32_t;
enum class LoadStatus { Invalid, Pending, Ready, Failed };
class AssetLoader {
public:
    static LoadTicket requestModel(const char *);
    static LoadTicket requestAnim(const char *);
    static LoadStatus poll(LoadTicket);
    static bool takeModel(LoadTicket, render::Model &);
    static bool takeAnim(LoadTicket, render::Anim &);
    static void cancel(LoadTicket);
};
} // namespace tt::world
