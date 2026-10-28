# Pad navigation: every bottom-screen control with buttons

AGENTS.md section 5 promises that the D-Pad drives the bottom screen for players who do not use
touch. This page lists every bottom-screen control, how it was reached before this change, and how
it is reached with buttons now. Touch is unchanged everywhere.

## Focus model (`source/ui/focus.*`, `source/ui/pad_tables.*`)

- A `FocusGroup` is a fixed array (80 items) of rectangles with ids. No allocation.
- **D-Up / D-Down** go to the nearest control in the same column. If there is none, they go to
  the nearest control above or below (for example from the gag grid down to Pass), and only
  then wrap.
- **D-Left / D-Right** stay on the current row and wrap at its ends.
- Disabled controls are skipped. With nothing focused, the first press focuses the first control.
- **A** presses the focused control. **B** backs out (cancel, close, or let go of the focus).
  **L / R** turn pages where upstream has pages.
- A touch on a control focuses it. In panels that own the screen, the touch also presses it.
- `FocusGroup::rebuild()` lets panels whose controls change every frame (battle, quest) rebuild
  their table without losing the focus or logging every frame.
- The highlight has a dark 1 px outer frame (for the light book paper), a 2 px gold frame and a
  translucent gold wash (for the navy panels).
- `ui/pad_tables.*` holds every panel's focus table. It is plain C++, so
  `tools/padnav_test/run.sh` checks each table on the host. Panels take their layout constants
  from it, or `static_assert` that they match.
- Log lines (one per change, never per frame):
  - `padnav: <panel> focus <index>`
  - `padnav: <panel> activate <id>`
  - `padnav: <panel> blur`
  - Panel-specific: `padnav: battle target <n>`, `padnav: fishing aim <deg>`,
    `padnav: chat activate whisper`, `padnav: clerk activate cancel`

### Ownership (`ui/panel_owner.h`)

Panels that own the bottom screen take buttons as before: battle, purchase, info, quest talk,
clerk, shop, tailor, closet, fishing and the book.

On the plain tabs (owner `Tabs`), **nothing is focused until D-Up or D-Down is pressed** on the
Map, Toon or Setup tab. Until then A jumps, B walks and D-Left / D-Right change tabs, exactly as
before. While a tab control is focused:
- the D-Pad moves between its controls and A presses the focused one;
- B lets go of the focus (it does not walk that frame);
- X, Y and START keep their meaning.

The focus is dropped on a tab change and whenever another panel takes the screen. A touch on the
tabs presses the control as before. It only moves the focus if a control was already focused, so
a touch player's A still jumps.

## Surface table

| Surface (file) | Control | Before | With buttons now |
|---|---|---|---|
| Tab bar (`render/ui.cpp`) | Map / Chat / Toon / Gags / Setup / Debug | touch, D-Left / D-Right, X = Chat, Y = Map | unchanged (D-Left / D-Right move inside a focused tab instead) |
| Map tab (`render/ui.cpp`) | Home | touch only | D-Up / D-Down focus, A presses (`homeRequest`) |
| Map tab | 8 hood teleport buttons | touch only | D-Down from Home to the row, D-Left / D-Right (wraps), A teleports; hoods without teleport access stay focusable and explain why (`ui::pressHoodControl`) |
| Chat tab (`ui/chat_menu.cpp`) | SpeedChat rows | D-Up / D-Down, A, B, touch | unchanged; now logged `padnav: chat focus/activate` |
| Chat tab (`render/ui.cpp`) | "To:" whisper target button | touch only | X while already on the Chat tab |
| Toon tab, online (`render/ui.cpp`) | Open Shtiker Book | touch, START | D-Down focuses it, A opens (START still works) |
| Toon tab, offline | Body / Shirt / Shorts targets and the 27 colour swatches | touch only | D-Pad across targets and the swatch grid, A picks |
| Setup tab (`render/ui.cpp`) | Stereo 3D, Stress [-], Stress [+] | touch only | D-Pad focus, A presses |
| Gags tab, Debug tab | display only (no controls) | — | — |
| Battle (`ui/battle_panel.cpp`) | gag grid (7 tracks × 7 levels) | touch only | starts on the first usable gag (like TownBattle); D-Pad moves, locked or unusable gags skipped; A attacks |
| Battle | target cog (up to 4) | touch only | L / R cycle the target; or D-Up to the cog row and A |
| Battle | Pass, Run | touch only | D-Down below the grid, A presses (Run is skipped when not allowed) |
| Battle | reward Continue | touch only | A or START (Battle ignores a second rewardDone) |
| Battle | Fire / SOS | not in this client's battle panel | — |
| Shtiker Book (`ui/book.cpp`) | page turn, page tabs, close | touch; L / R and D-Left / D-Right turn pages; START closes | L / R turn pages; B or START closes; D-Left / D-Right turn pages unless the page uses them |
| Book: Map page | Home, 8 hood buttons | touch only | D-Pad focus, A presses |
| Book: Options page | Stereo, LOD, Music −/+, SFX −/+, then (only while SELECT has the Debug tab on) Debug HUD and Stress −/+, Quit | touch only | D-Pad focus, A presses |
| Book: Cogs, Disguise pages | 4 department tabs | touch only | D-Left / D-Right step through them (highlighted) |
| Book: Fish page | genus < / > | touch; D-Left / D-Right were unreachable (the book took them) | D-Left / D-Right press < / > (highlighted) |
| Book: SOS page | "[Page n/2]" (more than 8 cards) | touch only | D-Left / D-Right flip it |
| Book: Gags, Tasks, Trophies pages | display only | — | D-Left / D-Right turn the page |
| Quest talk (`ui/quest_panel.cpp`) | dialog Next / Done | A or a tap | unchanged; logged |
| Quest talk | quest cards, Cancel | touch; B cancels | nothing is focused until the D-Pad is used (so mashing A through the pages never accepts a quest); D-Pad, A chooses; B cancels |
| Quest talk | track cards (2 columns), Later | touch; B cancels | D-Pad over the 2-column grid, A chooses; B cancels |
| Closet (`ui/closet_panel.cpp`) | Save, Cancel | A = Save, B = Cancel, touch | Save focused on open (A saves, as before); D-Left / D-Right, A presses; B cancels |
| Tailor (`ui/tailor_panel.cpp`) | shirt row, shorts row | D-Up / D-Down pick, D-Left / D-Right style, touch | unchanged, highlighted |
| Tailor | Done, Cancel | START, B, touch | D-Down from the shorts row, D-Left / D-Right, A presses |
| Gag Shop (`ui/shop_panel.cpp`) | gag grid | D-Pad, A buys, X removes | unchanged; logged as `shop_grid` |
| Gag Shop | [-] [+] Done Cancel | touch; X, A, START, B as shortcuts | D-Down past the last track, D-Left / D-Right (wraps), A presses, D-Up back to the grid |
| Minigame purchase (`ui/minigame_purchase_panel.cpp`) | gag grid, [-] [+] Done Cancel | as the Gag Shop | as the Gag Shop (`purchase_grid`, `purchase`) |
| Service clerks (`ui/service_clerk_panel.cpp`) | menu, pets, bodies, accessories, buttons | D-Pad, A, B, L / R, Y, touch | unchanged; now logged `padnav: clerk ...` |
| Fishing (`world/fishing.cpp`) | aim, cast power, cast, leave | Circle Pad aims; hold / release A casts; B leaves; no touch | D-Left / D-Right also aim (the Circle Pad wins) |
| Trolley / minigame info panel | display only | — | — |
| Boss battles (`world/boss_battle.cpp`, `cj_battle.cpp`) | pie throw / evidence fire | B (hold / release) and a touch shortcut | unchanged: B already does it; their Cog rounds use the battle panel above |
| CEO, CFO, Cogdo (`world/ceo_battle.cpp`, `cfo_*`, `cogdo_runtime.cpp`) | no bottom-screen touch input besides the battle panel | buttons | unchanged |
| Golf, racing, minigames (`game/*`) | out of scope (other agents own these files) | — | — |

Book page rectangles that differ from their touch areas (the touch areas are kept as they were):
- the Cogs / Disguise department tabs are drawn 6 px left and 6 px lower than their touch areas;
- the Fish genus arrows are drawn smaller and lower.

The highlight follows the drawn controls.

## Autotest (`core/autotest.cpp`, hooks in `main.cpp`)

The `pad: ...` steps press only buttons. The main.cpp hooks only move the toon to where a panel
opens (as the existing steps do), switch battle autoplay off during the pad battle turn, and press
A once per dialog page in `pad: quest pages`. Each block sits right after the step that sets it
up, so run the whole script, or an `only` line that keeps those steps too, for example:
`only tab:,pad:,book:,go fishing,move: door,visit clerk,see cog,fight cog,toon hq: talk,tailor visit`.

| Steps | Expected log lines |
|---|---|
| `pad: tab map` … `pad: map release` | `padnav: tab_map focus 0` (Home), a hood focus, `padnav: tab_map blur` |
| `pad: tab chat`, `pad: chat focus`, `pad: chat down`, `pad: chat open hello`, `pad: chat say hello` (shot `pad_chat_said`: the recent-chat strip shows "You: Hi!"), `pad: chat whisper`, `pad: chat back` | `padnav: chat focus 0`, `padnav: chat focus 1`, `padnav: chat activate 1`, `padnav: chat activate 0`, `padnav: chat activate whisper` (only with toons in view), `padnav: chat back` |
| `pad: tab toon`, `pad: toon focus`, `pad: book from toon tab` | `padnav: tab_toon focus 0`, `padnav: tab_toon activate 200` (the book opens online) |
| `pad: book next page`, `pad: book page turn`, `pad: book close` | the book title page changes in `pad_book_turn.png`; the book closes |
| `pad: tab setup` … `pad: setup release` | `padnav: tab_setup activate 2`, then `activate 1` (stress 0 → 1 → 0), `blur` |
| `pad: book options focus` … `pad: book close map` | `padnav: book_options focus 0`, `activate 2` twice (Debug HUD on, off), `padnav: book_map focus 0` |
| `pad: fish aim` | `padnav: fishing aim <deg>` on release (non-zero once the spot is Ready) |
| `pad: shop open` … `pad: shop press` | `padnav: shop_grid focus <id>` moves, `padnav: shop focus 2` (Done), `focus 3`, `padnav: shop activate 3` (cancel) |
| `pad: battle start` … `pad: battle rounds` | `padnav: battle focus <n>` moves, `padnav: battle target <n>` (with 2+ cogs), `padnav: battle activate <gag id>` and the attack request |
| `pad: quest pages` … `pad: quest choose` | `padnav: quest activate 98` per page, `padnav: quest focus 0/1/0`, `padnav: quest activate 0` |
| `pad: tailor open` … `pad: tailor press` | `padnav: tailor focus 1`, `focus 2`, `focus 3`, `padnav: tailor activate 3` (cancel) |

Not driven by the autotest:
- The closet: the stock server's estate closet flow is not in the script.
- The minigame purchase panel: its steps are gated by the trolley progress tracker.

Both are covered by host tests of their tables.
