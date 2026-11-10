#include "core/autotest.h"

#include <cstring>
#include <cstdio>
#include <cstdlib>

#include "core/log.h"
#include "game/front_end.h"
#include "ui/pad_tables.h"

namespace tt::autotest {

namespace {

// Bottom-screen tab centres (render/ui.cpp: Map Chat Toon Gags Book across 320 px, bar at y >= 208;
// the Debug tab joins them only while SELECT has it on).
constexpr int kTabY = 224;
// The middle of player tab i (5 tabs right of the laff pocket, ui/pad_tables.h).
constexpr int tabX(int i) { return int((tt::ui::pad::tabX(i, 5) + tt::ui::pad::tabX(i + 1, 5)) * 0.5f); }
// The Map tab's teleport row (pad::hoods): the middle of hood i.
constexpr int kHoodTapY = int(tt::ui::pad::hoods::kRowY + tt::ui::pad::hoods::kRowH * 0.5f);

constexpr int kBattleSosX = int(tt::ui::pad::battle::kSosX + 10), kBattleSosY = int(tt::ui::pad::battle::kRow2Y + 8);
constexpr int kBattleFireX = int(tt::ui::pad::battle::kFireX + 10), kBattleFireY = int(tt::ui::pad::battle::kBtnY + 8);

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
    // Deterministic real-art sweep: online-style Toon/Laff/jar and Gags/jar panels without a
    // server. main.cpp supplies the same view structs a setHp/setMoney/setInventory would.
    {1.5f, 0, 0, 0, KEY_TOUCH, tabX(2), kTabY, -1, "gui: toon assets", "gui_toon_assets"},
    {1.5f, 0, 0, 0, 0, -1, -1, -1, "gui: pocket three digit", "gui_pocket_three_digit"},
    {1.5f, 0, 0, 0, KEY_TOUCH, tabX(3), kTabY, -1, "gui: gags assets", "gui_gags_assets"},
    {1.5f, 0, 0, 0, KEY_TOUCH, tabX(4), kTabY, -1, "tab: book", "booktab"},
    {1.0f, 0, 0, 0, KEY_SELECT, -1, -1, -1, "debug: on", "debug"},
    {0.5f, 0, 0, 0, KEY_SELECT, -1, -1, -1, "debug: off"},
    // ---- padnav: the tabs and the Shtiker Book with buttons only (no touch) ----
    {1.0f, 0, 0, 0, KEY_Y, -1, -1, -1, "pad: tab map"},
    {1.0f, 0, 0, 0, KEY_DDOWN, -1, -1, -1, "pad: map focus"},
    {1.0f, 0, 0, 0, KEY_DRIGHT, -1, -1, -1, "pad: map hoods"},
    {1.0f, 0, 0, 0, KEY_DRIGHT, -1, -1, -1, "pad: map next hood", "pad_map"},
    {1.0f, 0, 0, 0, KEY_B, -1, -1, -1, "pad: map release"},
    {1.0f, 0, 0, 0, KEY_DRIGHT, -1, -1, -1, "pad: tab chat"},
    {1.0f, 0, 0, 0, KEY_DDOWN, -1, -1, -1, "pad: chat focus", "pad_chat"},
    {0.5f, 0, 0, 0, KEY_DDOWN, -1, -1, -1, "pad: chat down"},
    {0.5f, 0, 0, 0, KEY_A, -1, -1, -1, "pad: chat open hello", "pad_chat_hello"},
    {1.0f, 0, 0, 0, KEY_A, -1, -1, -1, "pad: chat say hello", "pad_chat_said"}, // the recent-chat strip
    {1.0f, 0, 0, 0, KEY_X, -1, -1, -1, "pad: chat whisper"},
    {1.0f, 0, 0, 0, KEY_B, -1, -1, -1, "pad: chat back"},
    // SpeedChat TOONTASKS > MY TASKS (TTSCToontaskMenu). The menu is still in HELLO: focus a row,
    // B back to the root, B lets go, then Up wraps from the top row past OK, No, Yes, GAG SHOP
    // and BATTLE to TOONTASKS; then MY TASKS and its first phrase (setSCToontask online).
    {0.5f, 0, 0, 0, KEY_DDOWN, -1, -1, -1, "pad: chat tasks hello row"},
    {0.5f, 0, 0, 0, KEY_B, -1, -1, -1, "pad: chat tasks to root"},
    {0.5f, 0, 0, 0, KEY_B, -1, -1, -1, "pad: chat tasks release"},
    {0.5f, 0, 0, 0, KEY_DDOWN, -1, -1, -1, "pad: chat tasks focus"},
    {0.3f, 0, 0, 0, KEY_DUP, -1, -1, -1, "pad: chat tasks up 1"},
    {0.3f, 0, 0, 0, KEY_DUP, -1, -1, -1, "pad: chat tasks up 2"},
    {0.3f, 0, 0, 0, KEY_DUP, -1, -1, -1, "pad: chat tasks up 3"},
    {0.3f, 0, 0, 0, KEY_DUP, -1, -1, -1, "pad: chat tasks up 4"},
    {0.3f, 0, 0, 0, KEY_DUP, -1, -1, -1, "pad: chat tasks up 5"},
    {0.5f, 0, 0, 0, KEY_DUP, -1, -1, -1, "pad: chat tasks up 6", "pad_chat_toontasks"},
    {0.5f, 0, 0, 0, KEY_A, -1, -1, -1, "pad: chat open toontasks"},
    {1.0f, 0, 0, 0, KEY_A, -1, -1, -1, "pad: chat open my tasks", "pad_chat_mytasks"},
    {1.5f, 0, 0, 0, KEY_A, -1, -1, -1, "pad: chat say task", "pad_chat_task_said"},
    // The stock FriendsListPanel art inside Chat. R is the button-only shortcut; this touch
    // exercises the visible header button, then B returns to SpeedChat.
    {1.5f, 0, 0, 0, KEY_TOUCH, 280, 14, -1, "friends: open", "friends_panel"},
    {0.8f, 0, 0, 0, KEY_DDOWN, -1, -1, -1, "friends: focus", "friends_focus"},
    {0.8f, 0, 0, 0, KEY_B, -1, -1, -1, "friends: close", "friends_closed"},
    {1.0f, 0, 0, 0, KEY_DRIGHT, -1, -1, -1, "pad: tab toon"},
    {1.0f, 0, 0, 0, KEY_DDOWN, -1, -1, -1, "pad: toon focus", "pad_toon"},
    {1.5f, 0, 0, 0, KEY_A, -1, -1, -1, "pad: book from toon tab", "pad_book"},
    {1.0f, 0, 0, 0, KEY_R, -1, -1, -1, "pad: book next page"},
    {1.0f, 0, 0, 0, KEY_DRIGHT, -1, -1, -1, "pad: book page turn", "pad_book_turn"},
    {1.0f, 0, 0, 0, KEY_B, -1, -1, -1, "pad: book close"},
    {1.0f, 0, 0, 0, KEY_DRIGHT, -1, -1, -1, "pad: tab gags"},
    {1.0f, 0, 0, 0, KEY_DRIGHT, -1, -1, -1, "pad: tab book"},
    {1.0f, 0, 0, 0, KEY_DDOWN, -1, -1, -1, "pad: book tab focus"},
    {1.0f, 0, 0, 0, KEY_DRIGHT, -1, -1, -1, "pad: book tab right"},
    {1.0f, 0, 0, 0, KEY_DDOWN, -1, -1, -1, "pad: book tab down", "pad_booktab"},
    {1.5f, 0, 0, 0, KEY_A, -1, -1, -1, "pad: book tab press", "pad_booktab_fish"},
    {1.0f, 0, 0, 0, KEY_B, -1, -1, -1, "pad: book tab close"},
    {1.0f, 0, 0, 0, KEY_B, -1, -1, -1, "pad: book tab release"},
    {1.0f, 0, 0, 0, KEY_DRIGHT, -1, -1, -1, "pad: tab wraps to map"},
    // ---- end padnav ----
    {1.0f, 0, 0, 0, 0, -1, -1, 0, "book: gags", "book_gags"},
    {1.0f, 0, 0, 0, 0, -1, -1, 0, "book: tasks", "book_tasks"},
    // Sample QuestPosters (main.cpp fills them in for these steps only).
    {1.5f, 0, 0, 0, 0, -1, -1, 0, "book: tasks demo 1", "book_tasks_demo1"},
    {1.5f, 0, 0, 0, 0, -1, -1, 0, "book: tasks demo 2", "book_tasks_demo2"},
    {1.5f, 0, 0, 0, 0, -1, -1, 0, "book: tasks demo 3", "book_tasks_demo3"},
    {1.0f, 0, 0, 0, 0, -1, -1, 0, "book: cogs", "book_cogs"},
    {1.0f, 0, 0, 0, 0, -1, -1, 0, "book: disguise", "book_disguise"},
    {1.0f, 0, 0, 0, 0, -1, -1, 0, "book: fish", "book_fish"},
    {1.0f, 0, 0, 0, 0, -1, -1, 0, "book: trophies", "book_trophies"},
    {1.0f, 0, 0, 0, 0, -1, -1, 0, "book: sos", "book_sos"},
    {1.0f, 0, 0, 0, 0, -1, -1, 0, "book: options", "book_options"},
    // ---- gui polish: SpeedChat paging and book page shots (AUTOTEST_ONLY="gui polish:") ----
    {0.3f, 0, 0, 0, KEY_TOUCH, tabX(1), kTabY, 0, "gui polish: chat tab"},
    {0.5f, 0, 0, 0, 0, -1, -1, 0, "gui polish: speedchat four"},
    {1.0f, 0, 0, 0, 0, -1, -1, 0, "gui polish: speedchat four shot", "b_speedchat_four"},
    {0.3f, 0, 0, 0, 0, -1, -1, 0, "gui polish: speedchat mid"},
    {1.0f, 0, 0, 0, 0, -1, -1, 0, "gui polish: speedchat mid shot", "b_speedchat_mid"},
    {0.3f, 0, 0, 0, 0, -1, -1, 0, "gui polish: speedchat last"},
    {1.0f, 0, 0, 0, 0, -1, -1, 0, "gui polish: speedchat last shot", "b_speedchat_last"},
    {0.3f, 0, 0, 0, KEY_START, -1, -1, 0, "gui polish: book open"},
    {0.3f, 0, 0, 0, 0, -1, -1, 0, "gui polish: book map page"},
    {1.0f, 0, 0, 0, 0, -1, -1, 0, "gui polish: book map settle"},
    {0.3f, 0, 0, 0, 0, -1, -1, 0, "gui polish: book map shot", "a_book_map_top"},
    {0.3f, 0, 0, 0, 0, -1, -1, 0, "gui polish: book gags page"},
    {1.0f, 0, 0, 0, 0, -1, -1, 0, "gui polish: book gags settle"},
    {0.3f, 0, 0, 0, 0, -1, -1, 0, "gui polish: book gags shot", "r_book_gags_top"},
    {0.3f, 0, 0, 0, KEY_B, -1, -1, 0, "gui polish: book close"},
    // ---- end gui polish ----
    // ---- padnav: the "book: options" step left the book open on Options ----
    {1.0f, 0, 0, 0, KEY_DDOWN, -1, -1, 0, "pad: book options focus"},
    {1.0f, 0, 0, 0, KEY_DDOWN, -1, -1, 0, "pad: book options down"},
    {1.0f, 0, 0, 0, KEY_A, -1, -1, 0, "pad: book options press", "pad_book_options"},
    {1.0f, 0, 0, 0, KEY_A, -1, -1, 0, "pad: book options press again"},
    {1.0f, 0, 0, 0, KEY_R, -1, -1, 0, "pad: book map page"},
    {1.0f, 0, 0, 0, KEY_DDOWN, -1, -1, 0, "pad: book map focus"},
    {1.0f, 0, 0, 0, KEY_DRIGHT, -1, -1, 0, "pad: book map hoods", "pad_book_map"},
    {1.0f, 0, 0, 0, KEY_B, -1, -1, 0, "pad: book close map"},
    // ---- end padnav ----
    // Shtiker Book by touch, held like a real stylus tap (6 frames, see apply): every tap but the
    // red X / B / START must leave it open. Run with AUTOTEST_FROM / AUTOTEST_ONLY="book touch".
    {1.0f, 0, 0, 0, KEY_START, -1, -1, 0, "book touch: open (START)", "booktouch_open"},
    {0.8f, 0, 0, 0, KEY_TOUCH, 53, 224, 0, "book touch: tab tasks", "booktouch_tab_tasks"},
    {0.8f, 0, 0, 0, KEY_TOUCH, 88, 224, 0, "book touch: tab cogs", "booktouch_tab_cogs"},
    {0.8f, 0, 0, 0, KEY_TOUCH, 159, 224, 0, "book touch: tab fish", "booktouch_tab_fish"},
    {0.8f, 0, 0, 0, KEY_TOUCH, 301, 224, 0, "book touch: tab map", "booktouch_tab_map"},
    {0.8f, 0, 0, 0, KEY_TOUCH, 20, 10, 0, "book touch: prev", "booktouch_prev"},
    {0.8f, 0, 0, 0, KEY_TOUCH, 245, 10, 0, "book touch: next", "booktouch_next"},
    {0.8f, 0, 0, 0, KEY_TOUCH, 140, 10, 0, "book touch: header gap", "booktouch_header"},
    {0.8f, 0, 0, 0, KEY_TOUCH, 265, 224, 0, "book touch: tab options", "booktouch_tab_options"},
    {0.8f, 0, 0, 0, KEY_TOUCH, 80, 40, 0, "book touch: options stereo", "booktouch_opt_stereo"},
    {0.8f, 0, 0, 0, KEY_TOUCH, 80, 40, 0, "book touch: options stereo back"},
    {0.8f, 0, 0, 0, KEY_TOUCH, 80, 64, 0, "book touch: options detail"},
    {0.8f, 0, 0, 0, KEY_TOUCH, 80, 64, 0, "book touch: options detail back"},
    {0.8f, 0, 0, 0, KEY_TOUCH, 136, 88, 0, "book touch: options music +", "booktouch_opt_music"},
    {0.8f, 0, 0, 0, KEY_TOUCH, 90, 112, 0, "book touch: options sound -"},
    {0.8f, 0, 0, 0, KEY_TOUCH, 236, 120, 0, "book touch: options districts", "booktouch_opt_districts"},
    {0.8f, 0, 0, 0, KEY_TOUCH, 17, 224, 0, "book touch: tab gags"},
    {0.8f, 0, 0, 0, KEY_TOUCH, 160, 120, 0, "book touch: gags content", "booktouch_gags_content"},
    {0.8f, 0, 0, 0, KEY_TOUCH, 300, 10, 0, "book touch: close X", "booktouch_closed_x"},
    {0.8f, 0, 0, 0, KEY_START, -1, -1, 0, "book touch: reopen 1"},
    {0.8f, 0, 0, 0, KEY_B, -1, -1, 0, "book touch: close B", "booktouch_closed_b"},
    {0.8f, 0, 0, 0, KEY_START, -1, -1, 0, "book touch: reopen 2"},
    {0.8f, 0, 0, 0, KEY_START, -1, -1, 0, "book touch: close START", "booktouch_closed_start"},
    {0.5f, 0, 0, 0, 0, -1, -1, 0, "book touch: done"},
    {3.0f, 0, 0, 0, 0, -1, -1, 8, "stress 8 toons"},
    {3.0f, 0, 0, 0, 0, -1, -1, 16, "stress 16 toons"},
    {3.0f, 0, 0, 0, 0, -1, -1, 32, "stress 32 toons"},
    {3.0f, 0, 0, 0, 0, -1, -1, 48, "stress 48 toons"},
    {3.0f, 0, 0, 0, 0, -1, -1, 64, "stress 64 toons", "stress64"},
    {3.0f, 0, 0, 0, 0, -1, -1, 0, "cog showcase", "showcase"},
    // Every Cog type, four at a time (SuitDNA.suitHeadTypes order).
    {3.0f, 0, 0, 0, 0, -1, -1, 0, "cog showcase 1", "showcase1"},
    {3.0f, 0, 0, 0, 0, -1, -1, 0, "cog showcase 2", "showcase2"},
    {3.0f, 0, 0, 0, 0, -1, -1, 0, "cog showcase 3", "showcase3"},
    {3.0f, 0, 0, 0, 0, -1, -1, 0, "cog showcase 4", "showcase4"},
    {3.0f, 0, 0, 0, 0, -1, -1, 0, "cog showcase 5", "showcase5"},
    {3.0f, 0, 0, 0, 0, -1, -1, 0, "cog showcase 6", "showcase6"},
    {3.0f, 0, 0, 0, 0, -1, -1, 0, "cog showcase 7", "showcase7"},
    {3.0f, 0, 0, 0, 0, -1, -1, 0, "cog showcase 8", "showcase8"},
    // Every Toon head (species), both genders, sizes and clothes, four at a time.
    {3.0f, 0, 0, 0, 0, -1, -1, 0, "toon showcase 1", "toonshow1"},
    {3.0f, 0, 0, 0, 0, -1, -1, 0, "toon showcase 2", "toonshow2"},
    {3.0f, 0, 0, 0, 0, -1, -1, 0, "toon showcase 3", "toonshow3"},
    {3.0f, 0, 0, 0, 0, -1, -1, 0, "toon showcase 4", "toonshow4"},
    {3.0f, 0, 0, 0, 0, -1, -1, 0, "toon showcase 5", "toonshow5"},
    {3.0f, 0, 0, 0, 0, -1, -1, 0, "toon showcase 6", "toonshow6"},
    {3.0f, 0, 0, 0, 0, -1, -1, 0, "toon showcase 7", "toonshow7"},
    {3.0f, 0, 0, 0, 0, -1, -1, 0, "toon showcase 8", "toonshow8"},
    {3.0f, 0, 0, 0, 0, -1, -1, 0, "toon showcase 9", "toonshow9"},
    {3.0f, 0, 0, 0, 0, -1, -1, 0, "toon showcase 10", "toonshow10"},
    {3.0f, 0, 0, 0, 0, -1, -1, 0, "toon showcase 11", "toonshow11"},
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
    {2.0f, 0, 0, KEY_DLEFT, 0, -1, -1, 0, "pad: fish aim", "pad_fish_aim"}, // padnav: D-Pad aims
    {1.0f, 0, 0, KEY_A, KEY_A, -1, -1, 0, "fish: charge"},
    {8.0f, 0, 0, 0, 0, -1, -1, 0, "fish: wait", "fish_cast"},
    {0.6f, 0, 0, 0, 0, -1, -1, 0, "fish: touch cast", "fish_touch_drag"}, // shot mid-drag
    {6.0f, 0, 0, 0, 0, -1, -1, 0, "fish: touch wait", "fish_touch_cast"},
    {3.0f, 0, 0, 0, KEY_B, -1, -1, 0, "fish: leave"},
    // Board, then the panel's Hop off button by touch (Trolley's exitButton); then board for real.
    {3.0f, 0, 0, 0, 0, -1, -1, 0, "trolley hop: board", "trolley_hop_wait"},
    {3.0f, 0, 0, 0, KEY_TOUCH, 160, 206, 0, "trolley hop: tap hop off", "trolley_hop"},
    {7.0f, 0, 0, 0, 0, -1, -1, 0, "board trolley", "trolley"},
    {9.0f, 0, 0, 0, 0, -1, -1, 0, "trolley: ride", "trolley_ride"},
    {1.0f, 0, 0, 0, 0, -1, -1, 0, "trolley: game", "trolley_game*"},
    {1.0f, 0, 0, 0, 0, -1, -1, 0, "trolley: purchase", "trolley_purchase*"},
    {5.0f, 0, 0, 0, 0, -1, -1, 0, "move: door", "move_door**"},
    {6.0f, 0, 0, 0, 0, -1, -1, 0, "visit clerk", "clerk"},
    // ---- padnav: the Gag Shop button row (the grid already had the D-Pad) ----
    {12.0f, 0, 0, 0, 0, -1, -1, 0, "pad: shop open"},
    {0.5f, 0, 0, 0, KEY_DDOWN, -1, -1, 0, "pad: shop down 1"},
    {0.5f, 0, 0, 0, KEY_DDOWN, -1, -1, 0, "pad: shop down 2"},
    {0.5f, 0, 0, 0, KEY_DDOWN, -1, -1, 0, "pad: shop down 3"},
    {0.5f, 0, 0, 0, KEY_DDOWN, -1, -1, 0, "pad: shop down 4"},
    {0.5f, 0, 0, 0, KEY_DDOWN, -1, -1, 0, "pad: shop down 5"},
    {0.5f, 0, 0, 0, KEY_DDOWN, -1, -1, 0, "pad: shop down 6"},
    {0.5f, 0, 0, 0, KEY_DDOWN, -1, -1, 0, "pad: shop down 7"},
    {1.0f, 0, 0, 0, KEY_DRIGHT, -1, -1, 0, "pad: shop cancel", "pad_shop"},
    {2.0f, 0, 0, 0, KEY_A, -1, -1, 0, "pad: shop press"},
    // ---- end padnav ----
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
    // ---- battle GUI edge-state views (hold the live Input panel; main.cpp supplies fixtures) ----
    {2.0f, 0, 0, 0, 0, -1, -1, 0, "battle: qa normal", "battle_qa_normal"},
    {2.0f, 0, 0, 0, 0, -1, -1, 0, "battle: qa 4-cog", "battle_qa_4cog"},
    {2.0f, 0, 0, 0, 0, -1, -1, 0, "battle: qa locked", "battle_qa_locked"},
    {2.0f, 0, 0, 0, 0, -1, -1, 0, "battle: qa empty", "battle_qa_empty"},
    {2.0f, 0, 0, 0, 0, -1, -1, 0, "battle: qa low laff", "battle_qa_low_laff"},
    {2.0f, 0, 0, 0, 0, -1, -1, 0, "battle: qa three digit laff", "battle_qa_three_digit_laff"},
    {2.0f, 0, 0, 0, 0, -1, -1, 0, "battle: qa gone toon", "battle_qa_gone_toon"},
    {20.0f, 0, 0, 0, 0, -1, -1, 0, "battle: trap", "movie_trap**"},
    {20.0f, 0, 0, 0, 0, -1, -1, 0, "battle: lure", "movie_lure**"},
    {20.0f, 0, 0, 0, 0, -1, -1, 0, "battle: sound", "movie_sound**"},
    {20.0f, 0, 0, 0, 0, -1, -1, 0, "battle: drop", "movie_drop**"},
    {20.0f, 0, 0, 0, 0, -1, -1, 0, "battle: heal", "movie_heal**"},
    // [cog attacks] Street fight observing multiple Cog attacks
    {35.0f, 0, 0, 0, 0, -1, -1, 0, "battle: cog attacks", "cog_attacks**"},
    {15.0f, 0, 0, 0, 0, -1, -1, 0, "fight: rounds", "battle_end**"},
    // ---- padnav: one battle turn chosen with buttons, then autoplay finishes the fight ----
    {20.0f, 0, 0, 0, 0, -1, -1, 0, "pad: battle start", "pad_battle_start"},
    {1.0f, 0, 0, 0, KEY_DUP, -1, -1, 0, "pad: battle cogs"},
    {1.0f, 0, 0, 0, KEY_DDOWN, -1, -1, 0, "pad: battle gags"},
    {1.0f, 0, 0, 0, KEY_R, -1, -1, 0, "pad: battle target", "pad_battle"},
    // TownBattleSOSPanel by touch, then B back to the gag that was focused (Fire stays grey
    // without pink slips; the stock server gives a fresh toon none).
    {1.5f, 0, 0, 0, KEY_TOUCH, kBattleSosX, kBattleSosY, 0, "pad: battle sos", "pad_battle_sos"},
    {1.0f, 0, 0, 0, KEY_B, -1, -1, 0, "pad: battle sos back", "pad_battle_sos_back"},
    // The same panels with sample cards and slips (main.cpp fills them in for these steps only).
    {1.5f, 0, 0, 0, KEY_TOUCH, kBattleSosX, kBattleSosY, 0, "pad: battle demo sos", "pad_battle_demo_sos"},
    {1.0f, 0, 0, 0, KEY_R, -1, -1, 0, "pad: battle demo sos page", "pad_battle_demo_sos2"},
    {1.0f, 0, 0, 0, KEY_B, -1, -1, 0, "pad: battle demo sos back"},
    {1.5f, 0, 0, 0, KEY_TOUCH, kBattleFireX, kBattleFireY, 0, "pad: battle demo fire", "pad_battle_demo_fire"},
    {1.0f, 0, 0, 0, KEY_B, -1, -1, 0, "pad: battle demo fire back", "pad_battle_demo_back"},
    {2.0f, 0, 0, 0, KEY_A, -1, -1, 0, "pad: battle fire"},
    {30.0f, 0, 0, 0, 0, -1, -1, 0, "pad: battle rounds", "pad_battle_end*"},
    // ---- end padnav ----
    {1.0f, 0, 0, 0, KEY_TOUCH, tabX(0), kTabY, 0, "tab: map (teleport)"},
    {8.0f, 0, 0, 0, KEY_TOUCH, 60, kHoodTapY, 0, "move: teleport", "move_teleport**"},
    {5.0f, 0, 0, 0, 0, -1, -1, 0, "see boat", "boat"},
    {20.0f, 0, 60, 0, 0, -1, -1, 0, "swim: harbor", "swim*"}, // includes a teleport to 1000
    {6.0f, 0, 0, 0, 0, -1, -1, 0, "ride boat", "ride"},
    {8.0f, 0, 0, 0, KEY_TOUCH, 100, kHoodTapY, 0, "teleport DG", "hood_dg"},
    {8.0f, 0, 0, 0, KEY_TOUCH, 180, kHoodTapY, 0, "teleport BR", "hood_br"},
    {8.0f, 0, 0, 0, 0, -1, -1, 0, "go sellbot hq", "hq_sell"},
    {8.0f, 0, 0, 0, 0, -1, -1, 0, "go cashbot hq", "hq_cash"},
    {8.0f, 0, 0, 0, 0, -1, -1, 0, "go lawbot hq", "hq_law"},
    {8.0f, 0, 0, 0, 0, -1, -1, 0, "go bossbot hq", "hq_boss"},
    {10.0f, 0, 0, 0, 0, -1, -1, 0, "toon hq: talk", "hq_talk"},
    // ---- padnav: the quest choice with buttons (A pages through the dialog) ----
    {16.0f, 0, 0, 0, 0, -1, -1, 0, "pad: quest pages", "pad_quest_pages*"},
    {1.0f, 0, 0, 0, KEY_DDOWN, -1, -1, 0, "pad: quest focus"},
    {1.0f, 0, 0, 0, KEY_DDOWN, -1, -1, 0, "pad: quest next"},
    {1.0f, 0, 0, 0, KEY_DUP, -1, -1, 0, "pad: quest back up", "pad_quest"},
    {2.0f, 0, 0, 0, KEY_A, -1, -1, 0, "pad: quest choose"},
    // ---- end padnav ----
    {14.0f, 0, 0, 0, 0, -1, -1, 0, "toon hq: choose", "hq_choice"},
    {2.0f, 0, 0, 0, KEY_TOUCH, tabX(2), kTabY, 0, "tab: tasks", "tasks"},
    {12.0f, 0, 0, 0, 0, -1, -1, 0, "tailor visit", "tailor"},
    // ---- padnav: the tailor rows and Done / Cancel with buttons ----
    {15.0f, 0, 0, 0, 0, -1, -1, 0, "pad: tailor open"},
    {1.0f, 0, 0, 0, KEY_DDOWN, -1, -1, 0, "pad: tailor shorts"},
    {1.0f, 0, 0, 0, KEY_DRIGHT, -1, -1, 0, "pad: tailor style", "pad_tailor"},
    {1.0f, 0, 0, 0, KEY_DDOWN, -1, -1, 0, "pad: tailor buttons"},
    {1.0f, 0, 0, 0, KEY_DRIGHT, -1, -1, 0, "pad: tailor cancel"},
    {2.0f, 0, 0, 0, KEY_A, -1, -1, 0, "pad: tailor press"},
    // ---- end padnav ----
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
    {120.0f, 0, 0, 0, 0, -1, -1, 0, "cog building: floor", "cog_floor*"}, // street search + elevator
    {600.0f, 0, 0, 0, 0, -1, -1, 0, "cog building: battle", "cog_battle*"}, // ends when back outside
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
    {90.0f, 0, 0, 0, 0, -1, -1, 0, "club: next floor", "club_next"}, // the kart room's golf kart
    {300.0f, 0, 60, 0, 0, -1, -1, 0, "club: boss", "club_boss*"},
    {15.0f, 0, 0, 0, 0, -1, -1, 0, "club: exit", "club_exit"},
    {10.0f, 0, 0, 0, 0, -1, -1, 0, "go speedway", "speedway"},
    // ---- racing2: bounded real purchase and solo Practice race ----
    {180.0f, 0, 0, 0, 0, -1, -1, 0, "speedway: pads", "speedway_pads"},
    {600.0f, 0, 0, 0, 0, -1, -1, 0, "speedway: race", "speedway_race*"},
    {12.0f, 0, 0, 0, 0, -1, -1, 0, "speedway: shop", "speedway_shop"},
    // ---- end racing2 ----
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
    // ---- render: fixed camera views for before/after render comparisons (main.cpp kViews). Run
    // with AUTOTEST_ONLY="view:". The first view of each zone waits longer for the teleport. ----
    {20.0f, 0, 0, 0, 0, -1, -1, 0, "view: ttc_near", "view_ttc_near"},
    {10.0f, 0, 0, 0, 0, -1, -1, 0, "view: ttc_mid", "view_ttc_mid"},
    {10.0f, 0, 0, 0, 0, -1, -1, 0, "view: ttc_far", "view_ttc_far"},
    {10.0f, 0, 0, 0, 0, -1, -1, 0, "view: ttc_tree", "view_ttc_tree"},
    {25.0f, 0, 0, 0, 0, -1, -1, 0, "view: street_near", "view_street_near"},
    {10.0f, 0, 0, 0, 0, -1, -1, 0, "view: street_far", "view_street_far"},
    {25.0f, 0, 0, 0, 0, -1, -1, 0, "view: dd_near", "view_dd_near"},
    {10.0f, 0, 0, 0, 0, -1, -1, 0, "view: dd_far", "view_dd_far"},
    // ---- end render views ----
    // ---- poses: "pose <zone> ..." lines in the autotest file (main.cpp). Run with
    // AUTOTEST_ONLY="pose:"; a step without its pose line is skipped. ----
    {25.0f, 0, 0, 0, 0, -1, -1, 0, "pose: 1", "pose_1"},
    {25.0f, 0, 0, 0, 0, -1, -1, 0, "pose: 2", "pose_2"},
    {25.0f, 0, 0, 0, 0, -1, -1, 0, "pose: 3", "pose_3"},
    {25.0f, 0, 0, 0, 0, -1, -1, 0, "pose: 4", "pose_4"},
    {25.0f, 0, 0, 0, 0, -1, -1, 0, "pose: 5", "pose_5"},
    {25.0f, 0, 0, 0, 0, -1, -1, 0, "pose: 6", "pose_6"},
    {25.0f, 0, 0, 0, 0, -1, -1, 0, "pose: 7", "pose_7"},
    {25.0f, 0, 0, 0, 0, -1, -1, 0, "pose: 8", "pose_8"},
    {14.0f, 0, 0, 0, 0, -1, -1, 0, "pose: 9", "pose_9"},
    {14.0f, 0, 0, 0, 0, -1, -1, 0, "pose: 10", "pose_10"},
    {14.0f, 0, 0, 0, 0, -1, -1, 0, "pose: 11", "pose_11"},
    {14.0f, 0, 0, 0, 0, -1, -1, 0, "pose: 12", "pose_12"},
    {14.0f, 0, 0, 0, 0, -1, -1, 0, "pose: 13", "pose_13"},
    {14.0f, 0, 0, 0, 0, -1, -1, 0, "pose: 14", "pose_14"},
    {14.0f, 0, 0, 0, 0, -1, -1, 0, "pose: 15", "pose_15"},
    {14.0f, 0, 0, 0, 0, -1, -1, 0, "pose: 16", "pose_16"},
    {14.0f, 0, 0, 0, 0, -1, -1, 0, "pose: 17", "pose_17"},
    {14.0f, 0, 0, 0, 0, -1, -1, 0, "pose: 18", "pose_18"},
    {14.0f, 0, 0, 0, 0, -1, -1, 0, "pose: 19", "pose_19"},
    {14.0f, 0, 0, 0, 0, -1, -1, 0, "pose: 20", "pose_20"},
    {14.0f, 0, 0, 0, 0, -1, -1, 0, "pose: 21", "pose_21"},
    {14.0f, 0, 0, 0, 0, -1, -1, 0, "pose: 22", "pose_22"},
    {14.0f, 0, 0, 0, 0, -1, -1, 0, "pose: 23", "pose_23"},
    {14.0f, 0, 0, 0, 0, -1, -1, 0, "pose: 24", "pose_24"},
    // ---- end poses ----
    {1.0f, 0, 0, 0, 0, -1, -1, 0, "done"},
};
constexpr int kWorldSteps = sizeof(kScript) / sizeof(kScript[0]);

// Front end (source/game/front_end.cpp layout): title -> Play online -> slot 1 ->
// Make a Toon (cat girl, colour, shirt) -> Create -> playground. Taps fire on release,
// so each tap step holds the touch for its first frame only.
constexpr int kFeNextX = 255, kFeNextY = 217;
const Step kFrontEndScript[] = {
    {2.0f, 0, 0, 0, 0, -1, -1, -1, "fe: title", "fe_title"},
    // The loading screen (ToontownLoadingScreen: logo, TOON TIP scroll, label) while it connects.
    {0.3f, 0, 0, 0, KEY_TOUCH, 160, 94, -1, "fe: play online", "fe_connecting"},
    {5.7f, 0, 0, 0, 0, -1, -1, -1, "fe: avatar list", "fe_pick"},
    // Pick-A-Toon's squares (ui/pick_a_toon_atlas.h): slot 1 is picked on arrival; Y is its
    // trashcan; a tap on an empty square makes a Toon there.
    {1.0f, 0, 0, 0, 0, -1, -1, -1, "fe: pick slot 1"},
    // "returning:" steps run only when an "only" line names them (a returning Toon: A plays the
    // picked Toon, which must go straight to its playground when tutorialAck is 1).
    {2.0f, 0, 0, 0, KEY_A, -1, -1, -1, "returning: play slot 1", "fe_returning_play"},
    {12.0f, 0, 0, 0, 0, -1, -1, -1, "returning: entering", "fe_returning_world"},
    {3.0f, 0, 150, 0, 0, -1, -1, -1, "returning: run in world", "fe_returning_run"},
    {2.0f, 0, 0, 0, KEY_Y, -1, -1, -1, "fe: delete toon", "fe_delete_prompt"},
    // Inside the dialog's Delete button and between the Pink and Yellow squares, so a run with no
    // Toon to delete (no dialog) taps nothing.
    {2.0f, 0, 0, 0, KEY_TOUCH, 212, 157, -1, "fe: confirm delete", "fe_deleted"},
    {1.0f, 0, 0, 0, 0, -1, -1, -1, "fe: select empty slot 1", "fe_pick_empty"},
    {2.0f, 0, 0, 0, KEY_TOUCH, 59, 77, -1, "fe: make a toon"},
    {2.0f, 0, 0, 0, KEY_TOUCH, 160, 51, -1, "fe: species cat", "fe_species"},
    {1.0f, 0, 0, 0, KEY_TOUCH, kFeNextX, kFeNextY, -1, "fe: next -> body"},
    {2.0f, 0, 0, 0, KEY_TOUCH, 278, 51, -1, "fe: girl", "fe_body"}, // GenderShop's girl button beside the Species frame
    {1.25f, 0, 0, 0, KEY_TOUCH, kFeNextX, kFeNextY, -1, "fe: next -> colour", "fe_room_drop**"}, // the colour shop drops in
    {2.0f, 0, 0, 0, KEY_TOUCH, 191, 76, -1, "fe: colour 6", "fe_color"},
    {1.0f, 0, 0, 0, KEY_TOUCH, kFeNextX, kFeNextY, -1, "fe: next -> clothes"},
    {2.0f, 0, 0, 0, KEY_TOUCH, 256, 70, -1, "fe: next shirt", "fe_clothes"}, // the Shirt frame's > arrow
    {2.0f, 0, 0, 0, KEY_TOUCH, kFeNextX, kFeNextY, -1, "fe: next -> name", "fe_name"},
    {2.0f, 0, 0, 0, KEY_TOUCH, 61, 164, -1, "fe: pick a name", "fe_pick_name"},
    {2.0f, 0, 0, 0, KEY_TOUCH, 166, 164, -1, "fe: typed name", "fe_typed_name"},
    {2.0f, 0, 0, 0, KEY_TOUCH, kFeNextX, kFeNextY, -1, "fe: create", "fe_tutorial_choice"},
    // An account's first Toon goes straight into the Toontorial (NameShop.__isFirstTime); the
    // "skip" trigger line makes Create! skip it (the stock regression path).  No tap here.
    {0.4f, 0, 0, 0, 0, -1, -1, -1, "fe: created", "fe_entering"},
    {9.6f, 0, 0, 0, 0, -1, -1, -1, "fe: entering", "fe_world"},
    {3.0f, 0, 150, 0, 0, -1, -1, -1, "fe: run in world", "fe_run"},
};
constexpr int kFrontEndSteps = sizeof(kFrontEndScript) / sizeof(kFrontEndScript[0]);

// Kept alive while main.cpp drives the server-led tutorial scenes.  Unlike the ordinary
// wall-clock test steps this ends only when TutorialManager.allDone succeeds.
const Step kTutorialScript[] = {
    {900.0f, 0, 0, 0, 0, -1, -1, -1, "tutorial: play", "tutorial*"},
};

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
        else if (!strcmp(line, "tutorial"))
            tutorial_ = true;
        else if (!strcmp(line, "skip"))
            game::FrontEnd::setTestSkipTutorial(true);  // the old stock path: Create! skips the Toontorial
        else if (!strncmp(line, "from ", 5))
            snprintf(from, sizeof(from), "%.63s", line + 5);
        else if (!strncmp(line, "until ", 6)) snprintf(until, sizeof(until), "%.63s", line + 6);
        else if (!strncmp(line, "only ", 5)) snprintf(only_, sizeof(only_), "%.191s", line + 5);
        else if (!strncmp(line, "drop at ", 8)) snprintf(dropAt_, sizeof(dropAt_), "%.63s", line + 8);
        else if (!strncmp(line, "battle hold ", 12)) battleHoldMs_ = static_cast<u32>(atoi(line + 12)) * 1000u;
        else if (!strncmp(line, "pose ", 5) && numPoses_ < kMaxPoses) {
            Pose& p = poses_[numPoses_];
            if (sscanf(line + 5, "%u %f %f %f %f %f %f %f %f %f %f", &p.zone, &p.eye[0], &p.eye[1], &p.eye[2],
                       &p.target[0], &p.target[1], &p.target[2], &p.toon[0], &p.toon[1], &p.toon[2],
                       &p.toon[3]) == 11)
                numPoses_++;
        }
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

bool Runner::skipped(const char* label) const {
    if (!strncmp(label, "pose: ", 6) && atoi(label + 6) > numPoses_) return true;
    return only_[0] ? !selected(label) : !strncmp(label, "returning:", 10);
}

const Pose* Runner::pose() const {
    const char* l = label();
    if (strncmp(l, "pose: ", 6)) return nullptr;
    const int n = atoi(l + 6);
    return n >= 1 && n <= numPoses_ ? &poses_[n - 1] : nullptr;
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
    while (step_ < numSteps_ && skipped(script_[step_].label))
        step_++; // "only" filter; "returning:" steps need one; "pose: N" needs N pose lines
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
        int touchX = s.touchX;
        int touchY = s.touchY;
        if (touchX >= 0) {
            touch.px = static_cast<u16>(touchX);
            touch.py = static_cast<u16>(touchY);
            held |= KEY_TOUCH;
        }
        if (s.stressToons >= 0) stressToons = s.stressToons;
        stepStarted_ = true;
    }
    // "book touch" steps hold the stylus for 6 frames like a real tap: down on the first frame,
    // then held with the same point (hardware never reports a one-frame touch).
    if (s.touchX >= 0 && stepFrames_ > 1 && stepFrames_ <= 6 && !strncmp(s.label, "book touch", 10)) {
        touch.px = static_cast<u16>(s.touchX);
        touch.py = static_cast<u16>(s.touchY);
        held |= KEY_TOUCH;
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
        if (tutorial_ && frontEnd_ && step_ == numSteps_ && script_ != kTutorialScript) {
            script_ = kTutorialScript;
            numSteps_ = sizeof(kTutorialScript) / sizeof(kTutorialScript[0]);
            step_ = 0;
            TT_LOG("tutorial: autotest selected Play it");
        }
    } else if (series && t_ >= nextSeriesShot_ + interval) {
        nextSeriesShot_ += interval;
        snprintf(seriesName_, sizeof(seriesName_), "%.*s_%03d", int(shotLen), s.shot, ++seriesIndex_);
        pendingShot_ = seriesName_;
    }
    return true;
}

} // namespace tt::autotest
