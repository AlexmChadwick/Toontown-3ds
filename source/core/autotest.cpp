#include "core/autotest.h"

#include <cstring>

#include <cstdio>

#include "core/log.h"

namespace tt::autotest {

namespace {

// Bottom-screen tab centres (render/ui.cpp: 5 tabs across 320 px, bar at y >= 208).
constexpr int kTabY = 224;
constexpr int tabX(int i) { return 26 + i * 53; } // 6 tabs of 320/6 px

const Step kScript[] = {
    {2.0f, 0, 0, 0, 0, -1, -1, -1, "idle"},
    {3.0f, 0, 150, 0, 0, -1, -1, -1, "run forward", "run"},
    {2.0f, 120, 150, 0, 0, -1, -1, -1, "run + turn right"},
    {1.0f, 0, 150, 0, KEY_A, -1, -1, -1, "running jump"},
    {2.0f, 0, 150, KEY_B, 0, -1, -1, -1, "walk (B)"},
    {2.0f, 0, 0, KEY_L, 0, -1, -1, -1, "camera orbit (L)", "orbit"},
    {2.0f, 0, -150, 0, 0, -1, -1, -1, "reverse"},
    {1.5f, 0, 0, 0, KEY_TOUCH, tabX(0), kTabY, -1, "tab: map", "map"},
    {1.5f, 0, 0, 0, KEY_TOUCH, tabX(2), kTabY, -1, "tab: toon"},
    {1.0f, 0, 0, 0, KEY_TOUCH, 8 + 4 * 34 + 15, 44 + 15, -1, "toon: pick color 4", "toon"},
    {1.5f, 0, 0, 0, KEY_TOUCH, tabX(3), kTabY, -1, "tab: gags", "gags"},
    {1.0f, 0, 0, 0, KEY_TOUCH, tabX(5), kTabY, -1, "tab: debug"},
    {3.0f, 0, 0, 0, 0, -1, -1, 8, "stress 8 toons"},
    {3.0f, 0, 0, 0, 0, -1, -1, 16, "stress 16 toons"},
    {3.0f, 0, 0, 0, 0, -1, -1, 32, "stress 32 toons"},
    {3.0f, 0, 0, 0, 0, -1, -1, 48, "stress 48 toons"},
    {3.0f, 0, 0, 0, 0, -1, -1, 64, "stress 64 toons", "stress64"},
    {3.0f, 0, 0, 0, 0, -1, -1, 0, "cog showcase", "showcase"},
    {4.0f, 0, 0, 0, 0, -1, -1, 0, "battle fx showcase", "battle_fx"},
    {1.3f, 0, 0, 0, 0, -1, -1, 0, "clip: throw", "clip_throw"},
    {1.6f, 0, 0, 0, 0, -1, -1, 0, "clip: pushbutton", "clip_push"},
    {1.0f, 0, 0, 0, 0, -1, -1, 0, "clip: cringe", "clip_cringe"},
    {2.0f, 0, 0, 0, 0, -1, -1, 0, "clip: victory", "clip_victory"},
    // "emote: <TTEmote index> <name>": sent and played like the SpeedChat Emotes menu.
    {1.2f, 0, 0, 0, 0, -1, -1, 0, "emote: 0 wave", "emote_wave"},
    {2.0f, 0, 0, 0, 0, -1, -1, 0, "emote: 6 dance", "emote_dance"},
    {1.5f, 0, 0, 0, 0, -1, -1, 0, "emote: 13 bow", "emote_bow"},
    {1.5f, 0, 0, 0, 0, -1, -1, 0, "emote: 12 belly flop", "emote_flop"},
    {1.5f, 0, 0, 0, 0, -1, -1, 0, "emote: 9 applause", "emote_applause"},
    {3.0f, 0, 0, 0, 0, -1, -1, 0, "grab treasure", "grab"},
    {4.0f, 0, 0, 0, 0, -1, -1, 0, "meet mickey", "mickey"},
    {3.0f, 0, 0, 0, 0, -1, -1, 0, "go fishing", "fish_sit"},
    {1.0f, 0, 0, KEY_A, KEY_A, -1, -1, 0, "fish: charge"},
    {8.0f, 0, 0, 0, 0, -1, -1, 0, "fish: wait", "fish_cast"},
    {3.0f, 0, 0, 0, KEY_B, -1, -1, 0, "fish: leave"},
    {7.0f, 0, 0, 0, 0, -1, -1, 0, "board trolley", "trolley"},
    {9.0f, 0, 0, 0, 0, -1, -1, 0, "trolley: ride", "trolley_ride"},
    {35.0f, 0, 0, 0, 0, -1, -1, 0, "catch game", "catch_play"},
    {45.0f, 0, 0, 0, 0, -1, -1, 0, "catch: finish", "catch"},
    {4.0f, 0, 0, 0, 0, -1, -1, 0, "exit: door sz19", "exit_door"},
    {6.0f, 0, 0, 0, 0, -1, -1, 0, "visit clerk", "clerk"},
    {4.0f, 0, 0, 0, 0, -1, -1, 0, "exit: interior door", "exit_interior"},
    {4.0f, 0, 0, 0, 0, -1, -1, 0, "exit: tunnel 2132", "exit_tunnel"},
    {5.0f, 0, 0, 0, 0, -1, -1, 0, "exit: street door", "street_interior"},
    {5.0f, 0, 0, 0, 0, -1, -1, 0, "exit: interior door", "street_back"},
    {8.0f, 0, 0, 0, 0, -1, -1, 0, "see cog", "cog"},
    {12.0f, 0, 0, 0, 0, -1, -1, 0, "fight cog", "battle_start"},
    {45.0f, 0, 0, 0, 0, -1, -1, 0, "fight: rounds", "battle_end"},
    {1.0f, 0, 0, 0, KEY_TOUCH, tabX(0), kTabY, 0, "tab: map (teleport)"},
    {8.0f, 0, 0, 0, KEY_TOUCH, 60, 191, 0, "teleport DD", "hood_dd"},
    {5.0f, 0, 0, 0, 0, -1, -1, 0, "see boat", "boat"},
    {6.0f, 0, 0, 0, 0, -1, -1, 0, "ride boat", "ride"},
    {8.0f, 0, 0, 0, KEY_TOUCH, 100, 191, 0, "teleport DG", "hood_dg"},
    {8.0f, 0, 0, 0, KEY_TOUCH, 180, 191, 0, "teleport BR", "hood_br"},
    {8.0f, 0, 0, 0, 0, -1, -1, 0, "go sellbot hq", "hq_sell"},
    {8.0f, 0, 0, 0, 0, -1, -1, 0, "go cashbot hq", "hq_cash"},
    {8.0f, 0, 0, 0, 0, -1, -1, 0, "go lawbot hq", "hq_law"},
    {8.0f, 0, 0, 0, 0, -1, -1, 0, "go bossbot hq", "hq_boss"},
    {10.0f, 0, 0, 0, 0, -1, -1, 0, "toon hq: talk", "hq_talk"},
    {14.0f, 0, 0, 0, 0, -1, -1, 0, "toon hq: choose", "hq_choice"},
    {2.0f, 0, 0, 0, KEY_TOUCH, tabX(2), kTabY, 0, "tab: tasks", "tasks"},
    {12.0f, 0, 0, 0, 0, -1, -1, 0, "tailor visit", "tailor"},
    {15.0f, 0, 0, 0, 0, -1, -1, 0, "go home", "estate"},
    {12.0f, 0, 0, 0, 0, -1, -1, 0, "golf: board", "golf_board"},
    {8.0f, 0, 0, 0, KEY_A, -1, -1, 0, "golf: swing", "golf_swing"},
    {12.0f, 0, 0, 0, KEY_A, -1, -1, 0, "golf: finish hole", "golf_finish"},
    {8.0f, 0, 0, 0, KEY_B, -1, -1, 0, "golf: exit", "golf_exit"},
    {10.0f, 0, 0, 0, 0, -1, -1, 0, "go factory", "factory_ext"},
    {30.0f, 0, 0, 0, 0, -1, -1, 0, "factory: board", "factory_in"},
    {20.0f, 0, 0, 0, 0, -1, -1, 0, "factory: entities", "factory_entities"},
    {45.0f, 0, 0, 0, 0, -1, -1, 0, "factory: battle", "factory_battle"},
    {15.0f, 0, 0, 0, 0, -1, -1, 0, "factory: exit", "factory_exit"},
    {20.0f, 0, 0, 0, 0, -1, -1, 0, "cog building: floor", "cog_floor"},
    {45.0f, 0, 0, 0, 0, -1, -1, 0, "cog building: battle", "cog_battle"},
    {20.0f, 0, 0, 0, 0, -1, -1, 0, "cog building: exit", "cog_exit"},
    {20.0f, 0, 0, 0, 0, -1, -1, 0, "mint: room 1", "mint_room1"},
    {45.0f, 0, 0, 0, 0, -1, -1, 0, "mint: battle", "mint_battle"},
    {20.0f, 0, 0, 0, 0, -1, -1, 0, "mint: next room", "mint_next"},
    {20.0f, 0, 0, 0, 0, -1, -1, 0, "da office: room 1", "da_room1"},
    {45.0f, 0, 0, 0, 0, -1, -1, 0, "da office: battle", "da_battle"},
    {20.0f, 0, 0, 0, 0, -1, -1, 0, "da office: exit", "da_exit"},
    {10.0f, 0, 0, 0, 0, -1, -1, 0, "go speedway", "speedway"},
    {12.0f, 0, 0, 0, 0, -1, -1, 0, "speedway: pads", "speedway_pads"},
    {30.0f, 0, 150, KEY_A, 0, -1, -1, 0, "speedway: race", "speedway_race"},
    {12.0f, 0, 0, 0, 0, -1, -1, 0, "speedway: shop", "speedway_shop"},
    {1.0f, 0, 0, 0, 0, -1, -1, 0, "done"},
};
constexpr int kWorldSteps = sizeof(kScript) / sizeof(kScript[0]);

// Front end (source/game/front_end.cpp layout): title -> Play online -> slot 1 ->
// Make a Toon (cat girl, colour, shirt) -> Create -> playground. Taps fire on release,
// so each tap step holds the touch for its first frame only.
constexpr int kFeNextX = 255, kFeNextY = 217;
const Step kFrontEndScript[] = {
    {2.0f, 0, 0, 0, 0, -1, -1, -1, "fe: title", "fe_title"},
    {6.0f, 0, 0, 0, KEY_TOUCH, 160, 94, -1, "fe: play online", "fe_pick"},
    {1.0f, 0, 0, 0, KEY_TOUCH, 82, 52, -1, "fe: pick slot 1"},
    {2.0f, 0, 0, 0, KEY_TOUCH, kFeNextX, kFeNextY, -1, "fe: make a toon"},
    {2.0f, 0, 0, 0, KEY_TOUCH, 160, 51, -1, "fe: species cat", "fe_species"},
    {1.0f, 0, 0, 0, KEY_TOUCH, kFeNextX, kFeNextY, -1, "fe: next -> body"},
    {2.0f, 0, 0, 0, KEY_TOUCH, 255, 171, -1, "fe: girl", "fe_body"},
    {1.0f, 0, 0, 0, KEY_TOUCH, kFeNextX, kFeNextY, -1, "fe: next -> colour"},
    {2.0f, 0, 0, 0, KEY_TOUCH, 191, 76, -1, "fe: colour 6", "fe_color"},
    {1.0f, 0, 0, 0, KEY_TOUCH, kFeNextX, kFeNextY, -1, "fe: next -> clothes"},
    {2.0f, 0, 0, 0, KEY_TOUCH, 280, 70, -1, "fe: next shirt", "fe_clothes"},
    {2.0f, 0, 0, 0, KEY_TOUCH, kFeNextX, kFeNextY, -1, "fe: next -> confirm", "fe_confirm"},
    {10.0f, 0, 0, 0, KEY_TOUCH, kFeNextX, kFeNextY, -1, "fe: create", "fe_world"},
    {3.0f, 0, 150, 0, 0, -1, -1, -1, "fe: run in world", "fe_run"},
};
constexpr int kFrontEndSteps = sizeof(kFrontEndScript) / sizeof(kFrontEndScript[0]);

} // namespace

bool Runner::begin() {
    FILE* f = fopen("sdmc:/3ds/toontown3ds/autotest_fe", "r");
    frontEnd_ = f != nullptr;
    if (!f) f = fopen("sdmc:/3ds/toontown3ds/autotest", "r");
    if (!f) return false;
    // Optional lines "from <label prefix>" (skip ahead to that step) and "until <label
    // prefix>" (finish before that step) for quicker iteration.
    char from[64] = {}, until[64] = {};
    char line[64];
    while (fgets(line, sizeof(line), f)) {
        if (char* nl = strchr(line, '\n')) *nl = 0;
        if (!strncmp(line, "from ", 5)) snprintf(from, sizeof(from), "%s", line + 5);
        else if (!strncmp(line, "until ", 6)) snprintf(until, sizeof(until), "%s", line + 6);
    }
    fclose(f);
    active_ = true;
    script_ = frontEnd_ ? kFrontEndScript : kScript;
    numSteps_ = frontEnd_ ? kFrontEndSteps : kWorldSteps;
    if (from[0]) {
        for (int i = 0; i < numSteps_; i++) {
            if (!strncmp(script_[i].label, from, strlen(from))) {
                step_ = i;
                ready_ = false;
                break;
            }
        }
    }
    if (until[0]) {
        for (int i = step_ + 1; i < numSteps_; i++) {
            if (!strncmp(script_[i].label, until, strlen(until))) {
                numSteps_ = i;
                break;
            }
        }
    }
    TT_LOG("autotest: start %s script (%d steps)", frontEnd_ ? "front-end" : "world", numSteps_);
    return true;
}

const char* Runner::label() const { return active_ && ready_ && step_ < numSteps_ ? script_[step_].label : ""; }

bool Runner::apply(float dt, circlePosition& pad, u32& held, u32& down, touchPosition& touch, int& stressToons) {
    pendingShot_ = nullptr;
    if (!active_ || !ready_) return true;
    if (step_ >= numSteps_) {
        active_ = false;
        TT_LOG("autotest: finished");
        return false;
    }
    const Step& s = script_[step_];
    if (!stepStarted_) {
        TT_LOG("autotest: step %d '%s'", step_, s.label);
        down |= s.downOnce;
        if (s.touchX >= 0) {
            touch.px = static_cast<u16>(s.touchX);
            touch.py = static_cast<u16>(s.touchY);
            held |= KEY_TOUCH;
        }
        if (s.stressToons >= 0) stressToons = s.stressToons;
        stepStarted_ = true;
    }
    pad.dx = s.padX;
    pad.dy = s.padY;
    held |= s.held;
    t_ += dt;
    if (t_ >= s.seconds) {
        pendingShot_ = s.shot;
        t_ = 0.0f;
        stepStarted_ = false;
        step_++;
    }
    return true;
}

} // namespace tt::autotest
