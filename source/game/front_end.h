#include "render/text_cache.h"
// Front end: Title -> connect/login -> Pick-a-Toon -> Make-a-Toon -> enter the playground.
// Bottom screen: touch buttons (D-pad + A/B also work). Top screen: a 3D preview of the
// selected/edited toon with a title overlay.
//
// The main loop owns the network objects; FrontEnd asks it to start them (Action) and then
// drives avatar selection through net::Client's manual mode (ClientConfig::manualAvatar).
#pragma once

#include <3ds.h>
#include <citro2d.h>
#include <citro3d.h>

#include "net/client.h"
#include "render/renderer.h"
#include "world/toon_avatar.h"
#include "world/toon_cache.h"
#include "world/toon_dna.h"

namespace tt::game {

enum class Action : u8 {
    None,
    StartOnline,  // main: start net::Client in manual mode
    StartOffline, // main: enter the world without a server
    EnterWorld,   // we are in the zone online; switch to the world
    Quit,
};

class FrontEnd {
public:
    bool init(world::ToonCache* cache);
    void shutdown();
    // net may be null until StartOnline has been handled.
    void setNet(net::Client* net) { net_ = net; }
    // Back to the title (e.g. after a fatal disconnect in the world).
    void reset();

    // Test hook for autotest: sets the typed name to use instead of opening swkbd.
    static void setTestTypedName(const char* name);
    static const char* testTypedName();

    Action update(u32 down, u32 held, const touchPosition& touch, float dt);
    // Top screen: 3D toon preview, then a citro2d overlay. Call between C3D_FrameBegin/End.
    void drawTop(render::Renderer& renderer);
    void drawBottom(C3D_RenderTarget* bottom);

private:
    enum class Screen : u8 { Title, Connecting, Pick, Make, Entering, Error };
    enum class MakeStep : u8 { Species, Body, Color, Clothes, Name, Count };

    struct Button {
        float x, y, w, h;
    };
    bool tapped(const Button& b) const;
    void text(float x, float y, float scale, u32 color, const char* fmt, ...) __attribute__((format(printf, 6, 7)));
    void textCentered(const Button& b, float scale, u32 color, const char* s);
    void button(const Button& b, const char* label, bool selected = false, bool enabled = true);

    void go(Screen s);
    void setPreviewDna(const world::ToonDna& dna);
    void updatePickPreview();
    void rebuildMakeDna();
    void updateConstructedName();
    void randomizePatternName();
    void makeStepInput(u32 down);
    void drawMakeBottom();
    void drawPickBottom();

    world::ToonCache* cache_ = nullptr;
    net::Client* net_ = nullptr;
    Screen screen_ = Screen::Title;
    MakeStep step_ = MakeStep::Species;
    int cursor_ = 0;      // highlighted button for D-pad navigation
    int pickSlot_ = 0;    // 0..5
    int colorTarget_ = 0; // 0 all, 1 head, 2 arms, 3 legs
    world::MakeAToonChoice choice_{};
    world::ToonDna makeDna_{};
    char error_[160] = {};

    // Pick-a-Toon state
    bool confirmDelete_ = false;

    // Name Shop state
    bool nameIsTyped_ = false;
    char typedName_[64] = {};
    char currentName_[64] = {};
    char nameError_[128] = {};
    int titleIndex_ = 0;
    int firstIndex_ = 0;
    int prefixIndex_ = 0;
    int suffixIndex_ = 0;
    bool titleActive_ = false;
    bool firstActive_ = true;
    bool lastActive_ = true;

    // Input snapshot for this frame (touch release = tap).
    touchPosition touch_{};
    touchPosition lastTouch_{};
    bool touchReleased_ = false;
    bool touching_ = false;

    world::ToonLook previewLook_;
    world::ToonAvatar preview_;
    float previewSpin_ = 0.0f;
    float time_ = 0.0f;
    render::TextCache textCache_;
    C2D_TextBuf textBuf_ = nullptr;
};

} // namespace tt::game
