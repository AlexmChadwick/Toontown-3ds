// Real generated scripts and the production stage, without libctru or an emulator.
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "quest/script_player.h"
#include "world/tutorial.h"

using namespace tt::quest;

namespace tt::log {
void write(const char*, ...) {}
}

namespace {

struct Stage : ScriptStage {
    ScriptXform x[static_cast<int>(ScriptVar::Count)];
    int chats = 0, finishes = 0, cleanup = 0;
    bool getXform(ScriptVar v, ScriptXform* out) override {
        *out = x[static_cast<int>(v)];
        return true;
    }
    void setXform(ScriptVar v, const ScriptXform& value) override { x[static_cast<int>(v)] = value; }
    void setColorScale(ScriptVar, const float[4]) override {}
    void run(const ScriptCmd& c, const char*) override {
        if (c.flags & kScriptUponTimeout) ++cleanup;
        if (c.op == ScriptOp::FinishQuestMovie) ++finishes;
        if (c.op == ScriptOp::ChatConfirm || c.op == ScriptOp::LocalChatConfirm ||
            c.op == ScriptOp::CCChatConfirm || c.op == ScriptOp::CCChatToConfirm) ++chats;
    }
};

void scripts() {
    for (int id = 0; id < static_cast<int>(ScriptId::Count); ++id) {
        for (bool female : {false, true}) {
            Stage stage;
            ScriptPlayer player;
            player.start(static_cast<ScriptId>(id), female, &stage);
            int frames = 0;
            while (player.active() && ++frames < 30000) {
                player.update(1.0f / 30.0f);
                if (player.block() == ScriptPlayer::Block::Chat) {
                    const int cursor = player.cursor();
                    player.update(100.0f);
                    assert(player.cursor() == cursor);
                    player.chatDone();
                } else if (player.block() == ScriptPlayer::Block::Event) {
                    assert(!player.postEvent("unrelated-event"));
                    assert(player.postEvent(player.waitingEvent()));
                }
            }
            assert(!player.active() && frames < 30000);
            assert(stage.chats > 0);
            const ScriptDef& def = scriptDef(static_cast<ScriptId>(id));
            int expectedFinishes = 0;
            for (int i = 0; i < def.numCmds; ++i)
                if (!(def.cmds[i].flags & kScriptUponTimeout) &&
                    def.cmds[i].op == ScriptOp::FinishQuestMovie) ++expectedFinishes;
            assert(stage.finishes == expectedFinishes);
            assert(player.popFinished() == (expectedFinishes != 0));
            assert(!player.popFinished());
            assert(stage.cleanup == 0);
            player.timeout();
            const int cleanup = stage.cleanup;
            player.timeout();
            assert(stage.cleanup == cleanup);
        }
    }

    Stage stage;
    ScriptPlayer player;
    player.start(ScriptId::TutorialMickey, false, &stage);
    player.update(2.0f);
    player.update(1.0f);
    assert(std::fabs(stage.x[static_cast<int>(ScriptVar::LocalToon)].pos[1] - 7.2f) < 0.001f);
    player.stop();
    assert(!player.running());
}

void ready(tt::world::TutorialFlow& flow) {
    flow.start(ScriptId::TutorialMickey, false);
    flow.update(10.0f);
    assert(flow.active() && flow.text()[0]);
}

void dialogue() {
    tt::world::TutorialFlow flow;
    ready(flow);
    ScriptCmd chat{};
    chat.op = ScriptOp::ChatConfirm;
    flow.run(chat, "First page\aSecond page\aLast page");
    assert(!strcmp(flow.text(), "First page"));
    flow.advance();
    assert(!strcmp(flow.text(), "Second page"));
    flow.update(100.0f);
    assert(!strcmp(flow.text(), "Second page"));
    flow.advance();
    assert(!strcmp(flow.text(), "Last page"));
    flow.advance();
    assert(!flow.text()[0]);

    ready(flow);
    std::string longText(150, 'x');
    longText += " word ";
    longText += "\xc3\xa9";
    longText += std::string(200, 'z');
    flow.run(chat, longText.c_str());
    std::string recovered;
    for (int pages = 0; flow.text()[0]; ++pages) {
        assert(pages < 10);
        const size_t n = strlen(flow.text());
        assert(n <= 159 && !strchr(flow.text(), '\a'));
        assert((static_cast<unsigned char>(flow.text()[0]) & 0xc0) != 0x80);
        recovered += flow.text();
        flow.advance();
    }
    std::string expected;
    for (char c : longText) if (c != ' ') expected += c;
    std::string actual;
    for (char c : recovered) if (c != ' ') actual += c;
    assert(actual == expected);

    ready(flow);
    flow.run(chat, "Old\aStale tail");
    ScriptCmd clear{};
    clear.op = ScriptOp::ClearChat;
    flow.run(clear, nullptr);
    assert(!flow.text()[0]);
    flow.advance();
    assert(!flow.text()[0]);
    flow.stop();
    assert(!flow.active() && !flow.text()[0]);

    ready(flow);
    const ScriptDef& def = scriptDef(ScriptId::TutorialMickey);
    const ScriptCmd* stock = nullptr;
    for (int i = 0; i < def.numCmds; ++i)
        if (def.cmds[i].text && strchr(def.cmds[i].text, '\a')) { stock = &def.cmds[i]; break; }
    assert(stock);
    flow.run(*stock, stock->text);
    assert(!strchr(flow.text(), '\a'));
    flow.advance();
    assert(flow.text()[0]);

    // The 3DS bottom screen is persistent rather than Panda's modal Shticker Book. A must
    // acknowledge the highlighted action at stock WAIT_EVENT gates so a real play-through
    // cannot stall where only the autotest's named event injection used to work.
    flow.start(ScriptId::QuestAssign110, false);
    for (int frames = 0; flow.active() && !flow.waitingEvent(); ++frames) {
        assert(frames < 30000);
        flow.update(1.0f / 30.0f);
        if (flow.text()[0]) flow.advance();
    }
    const char* event = flow.waitingEvent();
    assert(event && !strcmp(event, "enterStickerBook"));
    flow.advance();
    assert(!flow.waitingEvent());
}

} // namespace

int main() {
    scripts();
    dialogue();
    puts("tutorial_test: five scripts, both genders, timing, event gates and dialogue pages: PASS");
}
