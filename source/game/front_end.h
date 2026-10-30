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
#include "render/model.h"
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
    // Shown on the title screen so a wrong server.txt is obvious before connecting.
    void setServer(const char* host, int port, bool online);
    // Back to the title (e.g. after a fatal disconnect in the world).
    void reset();
    // Frees the Pick-A-Toon and Make-A-Toon art (about 0.55 MB of linear memory, plus the
    // Make-A-Toon room when one is resident) once we are in
    // the world; call after C3D_FrameBegin (the last front-end frame is then complete). It
    // reloads on demand when the front end comes back.
    void releaseArt();

    // Test hook for autotest: sets the typed name to use instead of opening swkbd.
    static void setTestTypedName(const char* name);
    static const char* testTypedName();

    Action update(u32 down, u32 held, const touchPosition& touch, float dt);
    // MakeAToon.music (create_a_toon) while making or naming a Toon; otherwise the title's tt_theme.
    bool wantsMakeAToonMusic() const { return screen_ == Screen::Make; }
    // Top screen: 3D toon preview, then a citro2d overlay. Call between C3D_FrameBegin/End.
    void drawTop(render::Renderer& renderer);
    void drawBottom(C3D_RenderTarget* bottom);

    struct Button { // a screen rectangle (bottom-screen pixels)
        float x, y, w, h;
    };

private:
    enum class Screen : u8 { Title, Connecting, Pick, Make, TutorialChoice, Entering, Error };
    enum class MakeStep : u8 { Species, Body, Color, Clothes, Name, Count };

    bool tapped(const Button& b) const;
    void text(float x, float y, float scale, u32 color, const char* fmt, ...) __attribute__((format(printf, 6, 7)));
    void textCentered(const Button& b, float scale, u32 color, const char* s);
    void checkBox(const Button& b, bool on);
    void button(const Button& b, const char* label, bool selected = false, bool enabled = true);
    bool matButton(const Button& b, const char* label, bool selected, bool enabled, bool focused);
    void dialogText(float y, float scale, u32 color, const char* s);
    // One of BodyShop's / ColorShop's shuffle frames (row 0 the big one, 1..3 the tilted small ones)
    // with its gold arrows: -1 / +1 when an arrow is pressed, 2 when the frame itself is tapped.
    int shuffleRow(int row, const char* name, bool glow, bool enabled);
    bool inside(const Button& b) const; // the stylus is on b
    void shuffleFrame(float cy, const char* name, const char* count); // draw pass only

    void go(Screen s);
    void setPreviewDna(const world::ToonDna& dna);
    void updatePickPreview();
    void rebuildMakeDna();
    void updateConstructedName();
    void randomizePatternName();
    void makeStepInput(u32 down);
    bool createToon(net::Client::TutorialChoice tutorial);
    void drawMakeBottom();
    void drawPickBottom();
    // Pick-A-Toon (AvatarChooser): the six coloured squares; returns true when it left the screen.
    bool pickInput(u32 down, bool back);
    void startMakeAToon();
    void shadowText(float cx, float y, float scale, u32 color, float maxW, const char* s);

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
    // Naming an existing toon whose name is still open (AvatarChoice's "Name Your Toon!"):
    // the Name Shop step alone, then setNamePattern / setNameTyped for this avatar.
    uint32_t renameAvId_ = 0;
    bool sendRename();

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

    // Make-A-Toon's room (bake_mat_room.py): the floor / background / spotlight, and the current
    // shop's walls + props, loaded on the first top-screen draw of a step (after C3D_FrameBegin, so
    // the previous frame no longer reads what is replaced). A new shop drops in (roomDrop) while the
    // last one squishes away (roomSquish / propSquish) with one of MakeAToon's crash sounds; the
    // squished shop is freed once the 0.875 s animation has finished.
    struct RoomShop {
        render::Model walls, props;
        int shop = -1;
    };
    bool ensureRoom();
    void drawRoom(render::Renderer& renderer);
    void releaseRoom();
    void playRoomCrash();
    render::Model roomCommon_;
    render::Model roomSmoke_[6]; // tt_m_ara_mat_smoke's frames, loaded with the first drop
    RoomShop roomShops_[2];
    int roomCur_ = 0;          // roomShops_ index of the shop on screen; the other one squishes away
    float roomAnimT_ = 10.0f;  // seconds since the current shop started dropping in
    bool roomFailed_ = false;  // a room file is missing: the plain preview instead (logged once)
    int crashSfx_[5] = {-1, -1, -1, -1, -1}; // tt::audio::SfxId, loaded with the first drop
    bool crashTried_ = false;
    float makeTurn_ = 0.0f;     // L / R: MakeAToon's rotate buttons, degrees added to the shop's heading
    MakeStep turnStep_ = MakeStep::Count; // the step makeTurn_ belongs to (reset on a new step)

    world::ToonLook previewLook_;
    world::ToonAvatar preview_;
    float previewSpin_ = 0.0f;
    float time_ = 0.0f;
    render::TextCache textCache_;
    C2D_TextBuf textBuf_ = nullptr;
    render::Model logo_; // the Toontown Online logo (title screen)
    // ToontownLoadingScreen (bake_loading_screen.py): progress-background's radial `bg`, drawn as a
    // far quad behind the Toon on every top screen but Make-A-Toon's room, and toon_council's
    // scroll for the TOON TIP on the connecting / heading-to screens (loaded on demand).
    void drawLoadingBg(render::Renderer& renderer, const C3D_FVec& eye, const C3D_FVec& at);
    void drawLoadingScroll(const char* label);
    render::Model loadingBg_;
    render::Model loadingGui_;
    bool loadingGuiTried_ = false;
    int tip_ = 0; // kLoadingTips index, picked each time a loading screen opens
    render::Model pickArt_; // AvatarChooser's background + squares and the trashcan (pick_a_toon.ttm)
    bool pickArtTried_ = false;
    char serverLine_[96] = "";
};

} // namespace tt::game
