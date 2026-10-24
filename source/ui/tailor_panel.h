#pragma once

#include <3ds.h>
#include <citro2d.h>

#include "net/client.h"
#include "world/toon_cache.h"

namespace tt::ui {

class TailorPanel {
public:
    bool init();
    void shutdown();
    void setPreview(world::ToonCache* cache, world::ToonLook* look);
    bool active() const { return active_; }
    // Puts the original clothes back and closes (sent on the next update). Also used by the
    // autotest: the stock AI crashes if a client vanishes with a tailor session open.
    void cancel() {
        if (active_ && !pendingSend_) pendingSend_ = 2;
    }
    void onMovie(const net::Client& client, uint32_t nowMs);
    void update(net::Client& client, uint32_t nowMs);
    u32 handleInput(u32 keysDown, const touchPosition& touch);
    void draw();
    const char* takeNotice() {
        const char* n = notice_;
        notice_ = nullptr;
        return n;
    }

private:
    void close();
    void choose(int delta);
    void preview();
    void rebuildText();
    bool topMatches(const world::ToonDna& dna, int i) const;
    bool bottomMatches(const world::ToonDna& dna, int i) const;

    bool active_ = false;
    uint32_t tailor_ = 0;
    uint32_t deadlineMs_ = 0;
    uint32_t nowMs_ = 0;
    int section_ = 0;
    int topChoice_ = 0;
    int bottomChoice_ = 0;
    bool changedTop_ = false;
    bool changedBottom_ = false;
    int pendingSend_ = 0; // 1 Done, 2 Cancel
    world::ToonDna original_{};
    world::ToonDna working_{};
    world::ToonCache* cache_ = nullptr;
    world::ToonLook* look_ = nullptr;
    const char* notice_ = nullptr;
    bool dirty_ = true;

    C2D_TextBuf textBuf_ = nullptr;
    C2D_Text titleText_, topText_, bottomText_, hintText_, timerText_, doneText_, cancelText_;
};

} // namespace tt::ui
