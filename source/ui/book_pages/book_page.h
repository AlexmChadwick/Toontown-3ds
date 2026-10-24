// Common base interface for Shtiker Book pages (docs/design/shtiker_book.md).
#pragma once

#include <3ds.h>
#include <citro2d.h>
#include "net/book_net.h"

namespace tt::net {
    struct OwnStats;
    struct OwnQuests;
    struct OwnFish;
    struct District;
}

namespace tt::ui {

struct State;
struct Context;

struct BookDrawContext {
    const net::OwnStats* stats = nullptr;
    const net::OwnQuests* quests = nullptr;
    const net::OwnBookStats* book = nullptr;
    const net::OwnFish* fish = nullptr;
    const net::District* districts = nullptr;
    int numDistricts = 0;
    State* state = nullptr;
    const Context* ctx = nullptr;
    const C2D_Image* mapImage = nullptr;
    bool hasMap = false;
    float mapMinX = 0, mapMinY = 0, mapMaxX = 1, mapMaxY = 1;
    unsigned mapW = 0, mapH = 0;
};

class BookPage {
public:
    virtual ~BookPage() = default;
    virtual const char* title() const = 0;
    virtual const char* tabName() const = 0;
    virtual void init() {}
    virtual void shutdown() {}
    virtual void enter() {}
    virtual void exit() {}
    virtual u32 handleInput(u32 keysDown, const touchPosition& touch, BookDrawContext& bdc) {
        (void)keysDown; (void)touch; (void)bdc; return 0;
    }
    virtual void draw(float x, float y, float w, float h, const BookDrawContext& bdc) = 0;
};

} // namespace tt::ui
