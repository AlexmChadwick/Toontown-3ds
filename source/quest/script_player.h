// QuestParser.NPCMoviePlayer for the generated Toontorial scripts (quest/quest_scripts.*).
// Pure timing and state, no 3DS headers: tools/tutorial_test drives it on the host, and
// world/tutorial.* is the game's ScriptStage (docs/design/tutorial.md section 3).
//
// How upstream plays a script, and how this mirrors it:
//   - Commands run in file order. WAIT holds the line for its seconds.
//   - LERP_* start at once and run in the background while the next commands run
//     (QuestParser wraps each lerp in Sequence(..., duration=0.0)); easeInOut blending.
//   - *_CHAT_CONFIRM ends a "chapter": the next command runs when the speaker's last page is
//     dismissed (the avatar's 'doneChatPage' event): chatDone().
//   - WAIT_EVENT ends a chapter too: the next command runs when the game posts that event
//     (postEvent: "enterStickerBook", "stickerBookPageChange-3", "SCChatEvent", ...).
//   - SHOW_THROW_SQUIRT_PREVIEW waits for its own 'doneThrowSquirtPreview', which upstream sends
//     after its 0.3 + 3.5 + 0.3 s intervals.
//   - UPON_TIMEOUT commands are kept aside and only run by timeout() (the movie was cut short;
//     DistributedNPCToon.cleanupMovie also runs them when the movie finishes).
//   - FINISH_QUEST_MOVIE is handed to the stage like any other command (the NPC's finishMovie).
#pragma once

#include <cstdint>

#include "quest/quest_scripts.h"

namespace tt::quest {

// A node's transform in its parent's frame, Panda units / degrees.
struct ScriptXform {
    float pos[3] = {0, 0, 0};
    float hpr[3] = {0, 0, 0};
    float scale[3] = {1, 1, 1};
};

class ScriptStage {
public:
    // Current transform of a script variable (false: the stage has no such node; lerps on it
    // then only report their end value).
    virtual bool getXform(ScriptVar var, ScriptXform* out) = 0;
    virtual void setXform(ScriptVar var, const ScriptXform& x) = 0;
    virtual void setColorScale(ScriptVar var, const float rgba[4]) = 0;
    // Every other command, at the moment it runs. `text` is the chat text for this toon (CC_*
    // commands pick the Mickey or Minnie line), else cmd.text.
    virtual void run(const ScriptCmd& cmd, const char* text) = 0;

protected:
    ~ScriptStage() = default;
};

class ScriptPlayer {
public:
    static constexpr int kMaxLerps = 12;
    // parseThrowSquirtPreview: LerpFunctionInterval 0.3 s, Wait 3.5 s, LerpFunctionInterval 0.3 s.
    static constexpr float kThrowSquirtPreviewSec = 0.3f + 3.5f + 0.3f;

    enum class Block : uint8_t { None, Wait, Chat, Event, Preview, Done };

    // femaleToon: LOAD_CLASSIC_CHAR / CC_* use Minnie (parseLoadClassicChar: gender != 'm').
    void start(ScriptId id, bool femaleToon, ScriptStage* stage);
    // Runs commands until one blocks, and moves the running lerps (seconds).
    void update(float dt);
    void chatDone();                  // the confirm chat's last page was dismissed
    bool postEvent(const char* name); // true when it was the event WAIT_EVENT waits for
    // The movie is cut short (or finished as an NPC toon finishes it): lerps jump to their end,
    // then the UPON_TIMEOUT commands run.
    void timeout();
    void stop(); // drop the movie without running anything

    bool active() const { return stage_ != nullptr && block_ != Block::Done; }
    bool running() const { return stage_ != nullptr; } // also true after the last command
    ScriptId id() const { return id_; }
    int cursor() const { return cursor_; } // next command index
    Block block() const { return block_; }
    const char* waitingEvent() const { return block_ == Block::Event ? event_ : nullptr; }
    bool femaleToon() const { return female_; }
    // True once when FINISH_QUEST_MOVIE ran.
    bool popFinished();
    // The text of a chat command for this toon.
    const char* chatText(const ScriptCmd& cmd) const;

    // Panda LerpBlendHelpers easeInOut.
    static float easeInOut(float t);

private:
    enum class LerpKind : uint8_t { Pos, Hpr, Scale, PosHprScale, ColorScale, Throb };
    struct Lerp {
        bool used = false;
        LerpKind kind = LerpKind::Pos;
        ScriptVar var = ScriptVar::None;
        float from[9] = {};
        float to[9] = {};
        float duration = 0;
        float t = 0;
    };

    void execute(const ScriptCmd& cmd, bool instant);
    void startLerp(const ScriptCmd& cmd, bool instant);
    void advanceLerps(float dt);
    void applyLerp(Lerp& l, float u);
    void finishLerps();

    ScriptStage* stage_ = nullptr;
    ScriptId id_ = ScriptId::Count;
    const ScriptDef* def_ = nullptr;
    bool female_ = false;
    int cursor_ = 0;
    Block block_ = Block::Done;
    float waitLeft_ = 0;
    const char* event_ = nullptr;
    bool finished_ = false;
    bool timeoutRun_ = false;
    Lerp lerps_[kMaxLerps];
};

} // namespace tt::quest
