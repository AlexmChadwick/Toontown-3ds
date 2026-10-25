# Pad Navigation Design & Surface Audit

## Overview
This document audits every bottom-screen surface and control in Toontown 3DS, detailing how each is reached today (touch or buttons) and how it is reached using button navigation (D-Pad, A, B, L, R) per AGENTS.md section 5.

Every control must be fully reachable and operable with gamepad buttons alone without requiring touch/stylus input. Touch input continues to function identically, and tapping any control immediately updates focus to that element.

## Audit Table

| Surface / Panel | Control Element | How reached today | How reached with buttons | Notes |
|---|---|---|---|---|
| **Main UI - Tab Bar** (`render/ui.cpp`) | Tab selection (Map, Chat, Toon, Gags, Settings, Debug) | Touch tab bar at bottom (`y >= 208`) or D-Left / D-Right | D-Left / D-Right cycles tabs when unfocused; X opens Chat; Y opens Map | Preserves 3D controls (A jumps, B walks) when unfocused |
| **Main UI - Map Tab** (`render/ui.cpp`) | "Home" button (`4, kHomeY, 76, kHomeH`) | Touch only | D-Pad navigation highlights Home, A activates (`homeRequest`) | B clears focus |
| **Main UI - Map Tab** (`render/ui.cpp`) | Hood teleport buttons (TTC, DD, DG, MM, TB, DDL) | Touch only | D-Pad navigation moves between hood buttons, A activates (`teleportRequest`) | B clears focus |
| **Main UI - Chat Tab** (`ui/chat_menu.cpp`) | Whisper target header button (`kW-100, 2, 96, 24`) | Touch only | D-Up from top menu row focuses whisper button, A cycles target | B exits back to menu |
| **Main UI - Chat Tab** (`ui/chat_menu.cpp`) | SpeedChat menu rows | D-Up / D-Down, A (activate), B (back), Touch | D-Up / D-Down moves focus, A enters submenu/says phrase, B goes back/blurs | Already supports buttons |
| **Main UI - Toon Tab** (`render/ui.cpp`) | "Open Shtiker Book" button (`60, 175, 200, 24`, online) | Touch only | D-Pad moves focus to button, A activates (`book_.open()`), or START | B clears focus |
| **Main UI - Toon Tab** (`render/ui.cpp`) | Body part target buttons (Body, Shirt, Shorts, offline) | Touch only | D-Pad moves focus to target row, D-Left/D-Right switches target, A activates | B clears focus |
| **Main UI - Toon Tab** (`render/ui.cpp`) | Color swatches grid (3 rows x 9 cols, offline) | Touch only | D-Pad moves across grid, A selects color for active target | B clears focus |
| **Main UI - Settings Tab** (`render/ui.cpp`) | Stereo 3D toggle (`kBtnX, kStereoY, kBtnW, kBtnH`) | Touch only | D-Pad focuses Stereo 3D button, A toggles stereo mode | B clears focus |
| **Main UI - Settings Tab** (`render/ui.cpp`) | Stress Toons [-] button (`kBtnX, kStressY, 60, kBtnH`) | Touch only | D-Pad focuses [-] button, A decreases stress count | B clears focus |
| **Main UI - Settings Tab** (`render/ui.cpp`) | Stress Toons [+] button (`kBtnX+80, kStressY, 60, kBtnH`) | Touch only | D-Pad focuses [+] button, A increases stress count | B clears focus |
| **Main UI - Gags Tab** (`render/ui.cpp`) | Inventory display | Display only | Display only (no interactive buttons) | Read-only |
| **Main UI - Debug Tab** (`render/ui.cpp`) | Profiler / Memory display | Display only | Display only (no interactive buttons) | Read-only |
| **Battle Panel** (`ui/battle_panel.cpp`) | Cog Targets (0..numCogs-1) | Touch only | L/R shoulder buttons cycle target cog directly; or D-Pad navigation to top row, A/D-Pad targets cog | Gold highlight indicates target |
| **Battle Panel** (`ui/battle_panel.cpp`) | Gag Grid (7 tracks x 7 levels) | Touch only | D-Pad moves across tracks and levels (skipping locked/empty), A selects attack | Follows TownBattle key flow |
| **Battle Panel** (`ui/battle_panel.cpp`) | Pass button (`2, kBtnY, 100, kBtnH`) | Touch only | D-Down from bottom gag row moves to Pass, A executes Pass | B also can cancel/pass |
| **Battle Panel** (`ui/battle_panel.cpp`) | Run button (`110, kBtnY, 100, kBtnH`, if canRun) | Touch only | D-Pad moves focus to Run button, A executes Run | Only enabled if `canRun` |
| **Battle Panel** (`ui/battle_panel.cpp`) | Reward Continue button (`218, kBtnY, 100, kBtnH`) | Touch only | Focus defaults to Continue button, A activates Continue | Advances reward screen |
| **Shtiker Book - Nav** (`ui/book.cpp`) | Page turning & bottom tabs | L / R shoulder buttons, touch top/bottom buttons | L / R turn pages, START/B closes book; D-Pad navigates within page | L/R page turn upstream |
| **Book - Map Page** (`book_pages/page_map.cpp`) | "Home" button (`4, kHomeY, 76, kHomeH`) | Touch only | D-Pad focuses Home, A activates (`homeRequest`) | Consistent with Map Tab |
| **Book - Map Page** (`book_pages/page_map.cpp`) | Hood teleport buttons (TTC, DD, DG, MM, TB, DDL) | Touch only | D-Pad navigates between hoods, A activates (`teleportRequest`) | Consistent with Map Tab |
| **Book - Tasks Page** (`book_pages/page_quests.cpp`) | ToonTasks cards | Display only | Display only (read-only) | |
| **Book - Gags Page** (`book_pages/page_inventory.cpp`) | Gag carry / exp bars | Display only | Display only (read-only) | |
| **Book - Cogs Page** (`book_pages/page_cogs.cpp`) | 4 Department tabs (Boss, Law, Cash, Sell) | Touch only | D-Left / D-Right or focus moves between dept tabs, A selects dept | L/R turns book page |
| **Book - Disguise Page** (`book_pages/page_disguise.cpp`) | 4 Department tabs (Boss, Law, Cash, Sell) | Touch only | D-Left / D-Right or focus moves between dept tabs, A selects dept | L/R turns book page |
| **Book - Fish Page** (`book_pages/page_fish.cpp`) | Prev Genus (<) / Next Genus (>) buttons | Touch only (or D-Left/D-Right) | D-Pad focuses < or >, A activates; D-Left / D-Right cycles genus | L/R turns book page |
| **Book - SOS Page** (`book_pages/page_sos.cpp`) | Page flip button `[Page 1/2]` (>8 cards) | Touch only | D-Pad focuses page toggle button, A toggles page | |
| **Book - Options Page** (`book_pages/page_options.cpp`) | Stereo 3D toggle (`15, 30, 135, 20`) | Touch only | D-Pad moves focus to item, A toggles | |
| **Book - Options Page** (`book_pages/page_options.cpp`) | Debug HUD toggle (`15, 54, 135, 20`) | Touch only | D-Pad moves focus to item, A toggles | |
| **Book - Options Page** (`book_pages/page_options.cpp`) | LOD Bias toggle (`15, 78, 135, 20`) | Touch only | D-Pad moves focus to item, A toggles | |
| **Book - Options Page** (`book_pages/page_options.cpp`) | Music Volume [-] and [+] (`80/126, 102, 24, 20`) | Touch only | D-Pad focuses [-] or [+], A adjusts; or D-Left/D-Right adjusts | |
| **Book - Options Page** (`book_pages/page_options.cpp`) | SFX Volume [-] and [+] (`80/126, 126, 24, 20`) | Touch only | D-Pad focuses [-] or [+], A adjusts; or D-Left/D-Right adjusts | |
| **Book - Options Page** (`book_pages/page_options.cpp`) | Stress Toons [-] and [+] (`80/126, 150, 24, 20`) | Touch only | D-Pad focuses [-] or [+], A adjusts; or D-Left/D-Right adjusts | |
| **Book - Options Page** (`book_pages/page_options.cpp`) | Quit Game button (`15, 176, 135, 20`) | Touch only | D-Pad focuses Quit Game button, A activates (`quitRequested`) | |
| **Book - Trophies Page** (`book_pages/page_trophies.cpp`) | Trophies & Records | Display only | Display only (read-only) | |
| **Quest Talk** (`ui/quest_panel.cpp`) | Dialog pages advance (`Talk::Phase::Pages`) | A or Touch | A advances dialog page | |
| **Quest Talk** (`ui/quest_panel.cpp`) | Quest choice cards (`Talk::Phase::QuestChoice`) | Touch only | D-Up / D-Down moves focus across quest cards, A accepts choice | B cancels |
| **Quest Talk** (`ui/quest_panel.cpp`) | Quest cancel button (`Talk::Phase::QuestChoice`) | Touch only or B | D-Down past bottom quest card focuses Cancel, A activates; B cancels | |
| **Quest Talk** (`ui/quest_panel.cpp`) | Track choice cards (`Talk::Phase::TrackChoice`) | Touch only | D-Pad moves across 2-column track grid, A selects track | B cancels |
| **Quest Talk** (`ui/quest_panel.cpp`) | Track cancel button (`Talk::Phase::TrackChoice`) | Touch only or B | D-Down past bottom track focuses Cancel, A activates; B cancels | |
| **Closet Panel** (`ui/closet_panel.cpp`) | "Done" button (`160, 196, 76, 36`) | Touch or KEY_A | D-Left / D-Right moves focus between Done and Cancel, A activates | Visible focus border |
| **Closet Panel** (`ui/closet_panel.cpp`) | "Cancel" button (`240, 196, 76, 36`) | Touch or KEY_B | D-Left / D-Right moves focus between Done and Cancel, A activates; B cancels | Visible focus border |
| **Tailor Panel** (`ui/tailor_panel.cpp`) | Shirt section selector (`0, kTopY, 320, 48`) | Touch or D-Up | D-Up focuses Shirt row, D-Left / D-Right changes style | |
| **Tailor Panel** (`ui/tailor_panel.cpp`) | Shorts section selector (`0, kBottomY, 320, 48`) | Touch or D-Down | D-Down focuses Shorts row, D-Left / D-Right changes style | |
| **Tailor Panel** (`ui/tailor_panel.cpp`) | "Done" button (`160, kButtonY, 76, 36`) | Touch or KEY_START | D-Down to button row, D-Left/D-Right to Done, A activates; START activates | |
| **Tailor Panel** (`ui/tailor_panel.cpp`) | "Cancel" button (`240, kButtonY, 76, 36`) | Touch or KEY_B | D-Down to button row, D-Left/D-Right to Cancel, A activates; B cancels | |
| **Gag Shop** (`ui/shop_panel.cpp`) | Gag Grid (7 tracks x 7 levels) | D-Pad, A (buy), X (remove) | D-Pad moves across tracks/levels, A buys, X removes | Already supports D-Pad |
| **Gag Shop** (`ui/shop_panel.cpp`) | [-], [+], Done, Cancel buttons | Touch or KEY_START/KEY_B | D-Down from bottom gag row moves to button row (Minus, Plus, Done, Cancel), A activates | Makes visual buttons reachable |
| **Purchase Panel** (`ui/minigame_purchase_panel.cpp`) | Gag Grid (7 tracks x 7 levels) | D-Pad, A (buy), X (remove) | D-Pad moves across tracks/levels, A buys, X removes | Already supports D-Pad |
| **Purchase Panel** (`ui/minigame_purchase_panel.cpp`) | [-], [+], Done, Cancel buttons | Touch or KEY_START/KEY_B | D-Down from bottom gag row moves to button row (Minus, Plus, Done, Cancel), A activates | Makes visual buttons reachable |
| **Service Clerks** (`ui/service_clerk_panel.cpp`) | Menu choices, Pet choices, Bottom buttons | D-Up/D-Down, D-Left/D-Right, A, B, Touch | D-Pad navigates choices/buttons, A activates, B cancels, L/R cycles | Already supports buttons |
| **Fishing** (`ui/fishing_panel.cpp` / `world/fishing.cpp`) | Power charging & Aiming | Circle Pad (aim), KEY_A (hold charge, release cast), KEY_B (leave) | D-Left / D-Right adjusts aim angle; D-Up / D-Down adjusts power; A casts; B leaves | Fully playable with D-Pad or Circle Pad |
| **Boss / Facility HUDs** (`world/boss_battle.cpp`, `cj_battle.cpp`, etc.) | Pie throwing, evidence firing, cranes, golf | KEY_A, KEY_B, Circle Pad, Touch | Buttons already mapped for all gameplay actions; touch is supplementary shortcut | |

## Focus Model Specification (`ui/focus.*`)
- Zero dynamic allocation: fixed structures with no heap allocations per frame.
- Spatial 2D navigation: Automatically connects controls based on geometry, supporting grids, lists, and heterogeneous layouts.
- Auto-skips disabled items.
- High-contrast highlight: Draws a double border (contrasting dark outline + bright gold border) plus semi-transparent gold fill that stands out cleanly against both dark panels and bright paper cards.
- Touch synchronization: Any touch inside an element immediately sets the focus index to that element.
- Standardized logging:
  - `padnav: <panel> focus <n>`
  - `padnav: <panel> activate <id>`
