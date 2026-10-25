#include "core/autotest.h"

#include <cstring>
#include <cstdio>

#include "core/log.h"
#include "game/front_end.h"

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
    {1.0f, 0, 0, 0, 0, -1, -1, 0, "book: gags", "book_gags"},
    {1.0f, 0, 0, 0, 0, -1, -1, 0, "book: tasks", "book_tasks"},
    {1.0f, 0, 0, 0, 0, -1, -1, 0, "book: cogs", "book_cogs"},
    {1.0f, 0, 0, 0, 0, -1, -1, 0, "book: disguise", "book_disguise"},
    {1.0f, 0, 0, 0, 0, -1, -1, 0, "book: fish", "book_fish"},
    {1.0f, 0, 0, 0, 0, -1, -1, 0, "book: trophies", "book_trophies"},
    {1.0f, 0, 0, 0, 0, -1, -1, 0, "book: sos", "book_sos"},
    {1.0f, 0, 0, 0, 0, -1, -1, 0, "book: options", "book_options"},
    {3.0f, 0, 0, 0, 0, -1, -1, 8, "stress 8 toons"},
    {3.0f, 0, 0, 0, 0, -1, -1, 16, "stress 16 toons"},
    {3.0f, 0, 0, 0, 0, -1, -1, 32, "stress 32 toons"},
    {3.0f, 0, 0, 0, 0, -1, -1, 48, "stress 48 toons"},
    {3.0f, 0, 0, 0, 0, -1, -1, 64, "stress 64 toons", "stress64"},
    {3.0f, 0, 0, 0, 0, -1, -1, 0, "cog showcase", "showcase"},
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
    {1.0f, 0, 0, 0, 0, -1, -1, 0, "trolley: game", "trolley_game*"},
    {1.0f, 0, 0, 0, 0, -1, -1, 0, "trolley: purchase", "trolley_purchase*"},
    {5.0f, 0, 0, 0, 0, -1, -1, 0, "move: door", "move_door**"},
    {6.0f, 0, 0, 0, 0, -1, -1, 0, "visit clerk", "clerk"},
    // ---- clerks: real NPC contact, panel actions and ordered reply logs ----
    {30.0f, 0, 0, 0, 0, -1, -1, 0, "clerk: pet shop", "clerk_pet*"},
    {30.0f, 0, 0, 0, 0, -1, -1, 0, "clerk: adopt", "clerk_adopt*"},
    {30.0f, 0, 0, 0, 0, -1, -1, 0, "clerk: kart shop", "clerk_kart*"},
    {30.0f, 0, 0, 0, 0, -1, -1, 0, "clerk: fisherman", "clerk_fisherman*"},
    {30.0f, 0, 0, 0, 0, -1, -1, 0, "clerk: party planner", "clerk_party*"},
    // ---- end clerks ----
    {4.0f, 0, 0, 0, 0, -1, -1, 0, "exit: interior door", "exit_interior"},
    {6.0f, 0, 0, 0, 0, -1, -1, 0, "move: tunnel", "move_tunnel**"},
    {4.0f, 0, 0, 0, 0, -1, -1, 0, "tunnel: arrived", "tunnel_arrived*"},
    {5.0f, 0, 0, 0, 0, -1, -1, 0, "exit: street door", "street_interior"},
    {5.0f, 0, 0, 0, 0, -1, -1, 0, "exit: interior door", "street_back"},
    {12.0f, 0, 0, 0, 0, -1, -1, 0, "see cog", "cog"},
    {12.0f, 0, 0, 0, 0, -1, -1, 0, "fight cog", "battle_start"},
    {20.0f, 0, 0, 0, 0, -1, -1, 0, "battle: trap", "movie_trap**"},
    {20.0f, 0, 0, 0, 0, -1, -1, 0, "battle: lure", "movie_lure**"},
    {20.0f, 0, 0, 0, 0, -1, -1, 0, "battle: sound", "movie_sound**"},
    {20.0f, 0, 0, 0, 0, -1, -1, 0, "battle: drop", "movie_drop**"},
    {20.0f, 0, 0, 0, 0, -1, -1, 0, "battle: heal", "movie_heal**"},
    {15.0f, 0, 0, 0, 0, -1, -1, 0, "fight: rounds", "battle_end**"},
    {1.0f, 0, 0, 0, KEY_TOUCH, tabX(0), kTabY, 0, "tab: map (teleport)"},
    {8.0f, 0, 0, 0, KEY_TOUCH, 60, 191, 0, "move: teleport", "move_teleport**"},
    {5.0f, 0, 0, 0, 0, -1, -1, 0, "see boat", "boat"},
    {8.0f, 0, 60, 0, 0, -1, -1, 0, "swim: harbor", "swim*"},
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
    {5.0f, 0, 0, 0, 0, -1, -1, 0, "factory: button", "factory_button*"},
    {10.0f, 0, 0, 0, 0, -1, -1, 0, "factory: stomper", "factory_stomper**"},
    {22.0f, 0, 0, 0, 0, -1, -1, 0, "factory: lift", "factory_lift*"},
    {6.0f, 0, 25, 0, 0, -1, -1, 0, "factory: barrel", "factory_barrel*"},
    {6.0f, 0, 0, 0, 0, -1, -1, 0, "factory: gear", "factory_gear*"},
    {8.0f, 0, 0, 0, 0, -1, -1, 0, "factory: mixer", "factory_mixer*"},
    {6.0f, 0, 0, 0, 0, -1, -1, 0, "factory: conveyor", "factory_conveyor*"},
    {9.0f, 0, 150, 0, 0, -1, -1, 0, "factory: door", "factory_door*"},
    {12.0f, 0, 0, 0, 0, -1, -1, 0, "factory: crates", "factory_crates**"},
    {6.0f, 0, 0, 0, 0, -1, -1, 0, "factory: goon zap", "factory_goon_zap*"},
    {8.0f, 0, 0, 0, 0, -1, -1, 0, "factory: goon stun", "factory_goon_stun*"},
    {20.0f, 0, 0, 0, 0, -1, -1, 0, "factory: entities", "factory_entities*"},
    {45.0f, 0, 0, 0, 0, -1, -1, 0, "factory: battle", "factory_battle*"},
    {300.0f, 0, 60, 0, 0, -1, -1, 0, "factory: foreman", "factory_foreman*"},
    {15.0f, 0, 0, 0, 0, -1, -1, 0, "factory: exit", "factory_exit"},
    {20.0f, 0, 0, 0, 0, -1, -1, 0, "cog building: floor", "cog_floor"},
    {45.0f, 0, 0, 0, 0, -1, -1, 0, "cog building: battle", "cog_battle"},
    {20.0f, 0, 0, 0, 0, -1, -1, 0, "cog building: exit", "cog_exit"},
    {10.0f, 0, 0, 0, 0, -1, -1, 0, "go mint", "mint_ext"},
    {40.0f, 0, 0, 0, 0, -1, -1, 0, "mint: board", "mint_in"},
    {150.0f, 0, 0, 0, 0, -1, -1, 0, "mint: walk", "mint_walk*"}, // battles on the way pause the walk
    {45.0f, 0, 0, 0, 0, -1, -1, 0, "mint: battle", "mint_battle*"},
    {300.0f, 0, 60, 0, 0, -1, -1, 0, "mint: boss", "mint_boss*"},
    {15.0f, 0, 0, 0, 0, -1, -1, 0, "mint: exit", "mint_exit"},
    {10.0f, 0, 0, 0, 0, -1, -1, 0, "go da office", "da_ext"},
    {40.0f, 0, 0, 0, 0, -1, -1, 0, "da office: board", "da_in"},
    {150.0f, 0, 0, 0, 0, -1, -1, 0, "da office: walk", "da_walk*"},
    {14.0f, 0, 0, 0, 0, -1, -1, 0, "da office: lasers", "stage_lasers**"},
    {14.0f, 0, 0, 0, 0, -1, -1, 0, "da office: security cam", "stage_camera**"},
    {45.0f, 0, 0, 0, 0, -1, -1, 0, "da office: battle", "da_battle*"},
    {90.0f, 0, 0, 0, 0, -1, -1, 0, "da office: next floor", "da_next"}, // countdown 42 s, then the ride
    {300.0f, 0, 60, 0, 0, -1, -1, 0, "da office: boss", "da_boss*"},
    {15.0f, 0, 0, 0, 0, -1, -1, 0, "da office: exit", "da_exit"},
    {10.0f, 0, 0, 0, 0, -1, -1, 0, "go country club", "club_ext"},
    {40.0f, 0, 0, 0, 0, -1, -1, 0, "club: board", "club_in"},
    {150.0f, 0, 0, 0, 0, -1, -1, 0, "club: walk", "club_walk*"},
    {15.0f, 0, 0, 0, 0, -1, -1, 0, "club: maze", "club_maze**"},
    {15.0f, 0, 0, 0, 0, -1, -1, 0, "club: moles", "club_moles**"},
    {15.0f, 0, 0, 0, 0, -1, -1, 0, "club: golf green", "club_golf**"},
    {45.0f, 0, 0, 0, 0, -1, -1, 0, "club: battle", "club_battle*"},
    {300.0f, 0, 60, 0, 0, -1, -1, 0, "club: boss", "club_boss*"},
    {15.0f, 0, 0, 0, 0, -1, -1, 0, "club: exit", "club_exit"},
    {10.0f, 0, 0, 0, 0, -1, -1, 0, "go speedway", "speedway"},
    {12.0f, 0, 0, 0, 0, -1, -1, 0, "speedway: pads", "speedway_pads"},
    {30.0f, 0, 150, KEY_A, 0, -1, -1, 0, "speedway: race", "speedway_race"},
    {12.0f, 0, 0, 0, 0, -1, -1, 0, "speedway: shop", "speedway_shop"},
    // ---- cogdo: real local-server flow; series capture asynchronous phases ----
    {60.0f, 0, 0, 0, 0, -1, -1, 0, "cogdo: board", "cogdo_board*"},
    {35.0f, 0, 0, 0, 0, -1, -1, 0, "cogdo: floor", "cogdo_floor*"},
    {240.0f, 0, 0, 0, 0, -1, -1, 0, "cogdo: game", "cogdo_game*"},
    {600.0f, 0, 0, 0, 0, -1, -1, 0, "cogdo: suite", "cogdo_suite*"},
    {30.0f, 0, 0, 0, 0, -1, -1, 0, "cogdo: exit", "cogdo_exit*"},
    // ---- end cogdo ----
    {45.0f, 0, 0, 0, 0, -1, -1, 0, "boss: board", "boss_board*"},
    {600.0f, 0, 0, 0, 0, -1, -1, 0, "boss: rounds", "boss_rounds*"},
    {600.0f, 0, 0, 0, 0, -1, -1, 0, "boss: pies", "boss_pies*"},
    {30.0f, 0, 0, 0, KEY_A, -1, -1, 0, "boss: end", "boss_end*"},
    {45.0f, 0, 0, 0, 0, -1, -1, 0, "cfo: board", "cfo_board*"},
    {600.0f, 0, 0, 0, 0, -1, -1, 0, "cfo: rounds", "cfo_rounds*"},
    {600.0f, 0, 0, 0, 0, -1, -1, 0, "cfo: cranes", "cfo_cranes*"},
    {30.0f, 0, 0, 0, KEY_A, -1, -1, 0, "cfo: end", "cfo_end*"},
    {45.0f, 0, 0, 0, 0, -1, -1, 0, "cj: board", "cj_board*"},
    {600.0f, 0, 0, 0, 0, -1, -1, 0, "cj: rounds", "cj_rounds*"},
    {1800.0f, 0, 0, 0, 0, -1, -1, 0, "cj: court", "cj_court*"},
    {30.0f, 0, 0, 0, KEY_A, -1, -1, 0, "cj: end", "cj_end*"},
    {45.0f, 0, 0, 0, 0, -1, -1, 0, "ceo: board", "ceo_board*"},
    {600.0f, 0, 0, 0, 0, -1, -1, 0, "ceo: banquet", "ceo_banquet*"},
    {600.0f, 0, 0, 0, 0, -1, -1, 0, "ceo: rounds", "ceo_rounds*"},
    {1200.0f, 0, 0, 0, 0, -1, -1, 0, "ceo: golf", "ceo_golf*"},
    {30.0f, 0, 0, 0, KEY_A, -1, -1, 0, "ceo: end", "ceo_end*"},
    // [interior life]
    {16.0f, 0, 0, 0, 0, -1, -1, 0, "interior: hq board", "hq_board*"},
    {16.0f, 0, 0, 0, 0, -1, -1, 0, "interior: pet fish", "pet_fish*"},
    {6.0f, 0, 0, 0, 0, -1, -1, 0, "interior: door leaves", "door_leaves*"},
    {22.0f, 0, 0, 0, 0, -1, -1, 0, "street: knock knock", "knock_knock*"},
    // [end interior life]
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
    {2.0f, 0, 0, 0, KEY_TOUCH, 132, 217, -1, "fe: delete toon", "fe_delete_prompt"},
    {2.0f, 0, 0, 0, KEY_TOUCH, 230, 157, -1, "fe: confirm delete", "fe_deleted"},
    {1.0f, 0, 0, 0, KEY_TOUCH, 82, 52, -1, "fe: select empty slot 1"},
    {2.0f, 0, 0, 0, KEY_TOUCH, kFeNextX, kFeNextY, -1, "fe: make a toon"},
    {2.0f, 0, 0, 0, KEY_TOUCH, 160, 51, -1, "fe: species cat", "fe_species"},
    {1.0f, 0, 0, 0, KEY_TOUCH, kFeNextX, kFeNextY, -1, "fe: next -> body"},
    {2.0f, 0, 0, 0, KEY_TOUCH, 255, 171, -1, "fe: girl", "fe_body"},
    {1.0f, 0, 0, 0, KEY_TOUCH, kFeNextX, kFeNextY, -1, "fe: next -> colour"},
    {2.0f, 0, 0, 0, KEY_TOUCH, 191, 76, -1, "fe: colour 6", "fe_color"},
    {1.0f, 0, 0, 0, KEY_TOUCH, kFeNextX, kFeNextY, -1, "fe: next -> clothes"},
    {2.0f, 0, 0, 0, KEY_TOUCH, 280, 70, -1, "fe: next shirt", "fe_clothes"},
    {2.0f, 0, 0, 0, KEY_TOUCH, kFeNextX, kFeNextY, -1, "fe: next -> name", "fe_name"},
    {2.0f, 0, 0, 0, KEY_TOUCH, 61, 164, -1, "fe: pick a name", "fe_pick_name"},
    {2.0f, 0, 0, 0, KEY_TOUCH, 166, 164, -1, "fe: typed name", "fe_typed_name"},
    {10.0f, 0, 0, 0, KEY_TOUCH, kFeNextX, kFeNextY, -1, "fe: create", "fe_world"},
    {3.0f, 0, 150, 0, 0, -1, -1, -1, "fe: run in world", "fe_run"},
};
constexpr int kFrontEndSteps = sizeof(kFrontEndScript) / sizeof(kFrontEndScript[0]);

} // namespace

bool Runner::begin() {
    shotsEnabled_ = true;
    FILE* f = fopen("sdmc:/3ds/toontown3ds/autotest_fe", "r");
    frontEnd_ = f != nullptr;
    if (!f) f = fopen("sdmc:/3ds/toontown3ds/autotest", "r");
    if (!f) return false;
    // Optional lines "from <label prefix>" (skip ahead to that step) and "until <label
    // prefix>" (finish before that step) for quicker iteration.
    char from[64] = {}, until[64] = {};
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (char* nl = strchr(line, '\n')) *nl = 0;
        if (!strcmp(line, "shots off"))
            shotsEnabled_ = false;
        else if (!strncmp(line, "from ", 5))
            snprintf(from, sizeof(from), "%.63s", line + 5);
        else if (!strncmp(line, "until ", 6)) snprintf(until, sizeof(until), "%.63s", line + 6);
        else if (!strncmp(line, "only ", 5)) snprintf(only_, sizeof(only_), "%.191s", line + 5);
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

// "only a,b,c": run just the steps whose labels start with one of the prefixes.
bool Runner::selected(const char* label) const {
    const char* p = only_;
    while (*p) {
        const char* end = strchr(p, ',');
        const size_t n = end ? size_t(end - p) : strlen(p);
        if (n && !strncmp(label, p, n)) return true;
        if (!end) break;
        p = end + 1;
    }
    return false;
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
    while (only_[0] && step_ < numSteps_ && !selected(script_[step_].label)) step_++; // "only" filter
    if (step_ >= numSteps_) {
        active_ = false;
        TT_LOG("autotest: finished");
        return false;
    }
    const Step& s = script_[step_];
    stepFrames_ = stepStarted_ ? stepFrames_ + 1 : 1;
    if (!stepStarted_) {
        TT_LOG("autotest: step %d '%s'", step_, s.label);
        if (!strcmp(s.label, "fe: typed name")) {
            game::FrontEnd::setTestTypedName("Little Pepper");
        }
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
    size_t shotLen = s.shot ? strlen(s.shot) : 0;
    int stars = 0;
    while (shotLen > 1 && s.shot[shotLen - 1] == '*') { shotLen--; stars++; }
    const bool series = stars > 0;
    const float interval = stars > 1 ? 0.25f : 1.0f;
    // ---- qafix: wall-clock steps cannot escape a live trolley session ----
    const bool trolleyBlocked = !trolleyProgress_.mayAdvance(s.label);
    if (t_ >= s.seconds && trolleyBlocked && !trolleyWaiting_) {
        TT_LOG("trolley-step: waiting '%s'", s.label);
        trolleyWaiting_ = true;
    }
    if (t_ >= s.seconds && !trolleyBlocked) {
        if (trolleyWaiting_) TT_LOG("trolley-step: complete '%s'", s.label);
        trolleyWaiting_ = false;
        // ---- end qafix ----
        pendingShot_ = s.shot;
        if (series) {
            snprintf(seriesName_, sizeof(seriesName_), "%.*s_end", int(shotLen), s.shot);
            pendingShot_ = seriesName_;
        }
        t_ = 0.0f;
        nextSeriesShot_ = 0.0f;
        seriesIndex_ = 0;
        stepStarted_ = false;
        step_++;
    } else if (series && t_ >= nextSeriesShot_ + interval) {
        nextSeriesShot_ += interval;
        snprintf(seriesName_, sizeof(seriesName_), "%.*s_%03d", int(shotLen), s.shot, ++seriesIndex_);
        pendingShot_ = seriesName_;
    }
    return true;
}

} // namespace tt::autotest
