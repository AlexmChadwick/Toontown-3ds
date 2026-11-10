# Groups

Groups is a Shticker Book page: create a group, see the list, join with one press, leave. It does not start invasions and it does not pick a district.

The wire matches the Android client. `tools/groups/groups.dc` is the same file, and `tools/platform/platform.dc` is the Android platform file loaded ahead of it. Field numbers below are the Panda `DCFile` numbers when the server reads `otp.dc`, `toon.dc`, `platform.dc`, then `groups.dc`. `TTPlatformManager` is 2270 through 2273. The nine `TTGroupInfo` struct members take 2274 through 2282, because Panda numbers struct fields. `TTGroupsManager` therefore starts at 2283. `avatarGone` is field 2294. That is class-local index 11, not the wire id. `tools/groups_test/test_groups.py` loads those four files with `DCFile` and pins every id the 3DS hardcodes, including the struct numbers. The 3DS `CLIENT_HELLO` hash stays the stock otp+toon value **547155168** (`dc_generated.h` is not regenerated). The four-file hash is a different number and is not sent.

The UberDOG object is DoId **4722**. Astron does not send an ENTER for an uberdog, so the client never learns that id from a generate. Once the session is logged in (district list through a zone change) the client adopts 4722 locally, the same way the Android client calls `generateGlobalObject(4722, 'TTGroupsManager')`. The page then leaves "Groups are not available right now." `requestBoard` is still sent only while the Groups page is open and the client is in a zone. A server with no object at 4722 can eject a client that opens the page, because that update is addressed to 4722.

## What the 3DS client sends

All of these are `clsend`. Each one is a `CLIENT_OBJECT_SET_FIELD` (120) to DoId 4722.

| Field | Wire id | Body |
|---|---|---|
| `requestBoard` | 2283 | empty |
| `stopBoard` | 2284 | empty |
| `setContext` | 2285 | `u32` hood, `u32` shard, `u8` activity, `u16` byte length, then the task hoods |
| `createGroup` | 2286 | `u8` kind, `u8` target, `u8` max size |
| `joinGroup` | 2287 | `u32` group id |
| `leaveGroup` | 2288 | empty |

`setBoard` (2289), `groupChanged` (2290), `groupGone` (2291) and `result` (2292) arrive from the UberDOG. `noteActivity` (2293) and `avatarGone` (2294) are not client fields. The 3DS process does not send them. `avatarGone` is the server's post-remove (`CLIENTAGENT_ADD_POST_REMOVE` 1110 around `STATESERVER_OBJECT_SET_FIELD` 2020). `tools/groups/ttgroups/wire.py` and `source/net/groups_logic.h` pack that datagram the same way, and the host test checks one vector, so a later login hook can reuse it. This client does not register the post-remove. A groups-enabled UberDOG registers it for every account, including a 3DS login.

`TTGroupInfo` on the wire is nine fields, then `uint32 members[]` as a Panda variable array: a `u16` byte length, then the ids. `setBoard` is a `u32` version plus that array. `groupChanged` is a version plus one struct. `groupGone` is a version plus a `u32` id. `result` is `u8` op, `u8` code, `u32` group id.

The client keeps at most 32 groups. The UberDOG allows 200. The parser still consumes the whole array.

## Page

`source/ui/book_pages/page_groups.cpp` is the sheet. It is the tenth book page, appended after Map, so pages 0 through 8 keep their indexes and the nine baked tab icons stay put. Page 9 has no icon. The tab falls back to the word Groups.

The page builds its text cache the first time it is used. `init()` does not allocate. There is no per-frame poll. The main loop subscribes when the book is open on this page and unsubscribes when it leaves. That is an edge, not a check that sends every frame.

On the 320x240 sheet the three filters (Boss, Building, Task) and the row's Join or Leave are 48px tall. One group is on screen. D-pad Down and Up move between groups. D-pad Left and Right on the row turn the book page, because the row is full width. A activates the focused control. Touch does too.

An empty board says "No groups yet. Start one with Boss, Building or Task." There is no Create button. Create is those three filters. At Impress scale 0.40 that line is about 211px, inside the 288px wrap. A disabled Join, Full, or filter draws `kInk` on `kGrey`. An enabled Join draws white on `kGreenDark`, Leave draws white on `kRedDark`, and an enabled filter draws white on `kBlueDark`. Each of those pairs is at least 4.5:1. Leader and member names are copied through `copyPlainName` before they are drawn, so Panda colour codes, newlines and tags from another player do not reach citro2d.

The picker asks for a department (four cells) or a task playground (six). It covers the tab bar so the six cells and Back (44px, y 190) fit on the 240px screen. B closes the picker. B closes the book when the picker is not open.

After a successful join the client follows Android's `teleport_plan` through the travel path that already exists.

- Same district and same hood: "You are already here!".
- The leader is a friend on this district: "You joined the group!" and `teleportQuery`, which only reaches a toon already in interest. When the reply is available on this district, the main loop sets `teleportRequest` to that zone. When the query cannot be sent, or the reply is not a same-district visit, the page uses the Map teleport to the group's hood if teleport access allows it.
- Same district, hood allowed: "You joined the group!", `checkTeleportAccess`, and `teleportRequest` set to the hood. That is the Map tab path (`teleportFromMap`).
- Another district, or a hood this toon cannot teleport to: "Go to <place> to meet your group." The district list on the Options page is display only, so there is no `choseShard` step.

`setContext` sends the hoods of the toon's current task targets. Each quest's `toNpc` is looked up in the baked `NPCToons` table (`quest::findNpc`). The zone's canonical hood is kept when it is one of the six task playgrounds. The list is unique, sorted, and capped at four, which is Android's `clean_task_hoods`. An NPC the table does not know (Toon HQ, Any, a bad id) is omitted, and an empty list is a `u16` length of 0, the same value Android sends when `task_hoods_of` finds nothing. Create still does not send a place. The UberDOG reads the avatar's location. The task hoods are for matching, not for choosing that place.

## Where it differs from Android

- The page is C++ and citro2d. `tools/groups/ttgroups` is the host reference. It is not linked into the 3DS binary.
- No book-button badge and no always-on subscription. The board is watched only while this page is the open page.
- Join on another district stays a status line. Android calls `ShardPage.choseShard` first. This client does not change district.
- A friend teleport only starts when the leader is already in interest. Android's `teleportQuery` has the same visibility limit on Astron. The fallback is the Map teleport to the hood.
- Cog HQ and other zones teleport only when `Place::available` has the zone installed. Otherwise the log says the zone is not installed and the toon stays put.
- The client cache holds 32 groups. The server still allows 200.
- The picker hides the book tab bar. Android's 320x240 overlay was a separate window. Back stays on screen, and Join, Leave and the filters are at least 48px.
- B closes the picker first, then the book.
- Groups is the last page. The tenth tab is text, not a baked glyph.
- DoId 4722 is adopted as a global object once the session is logged in or in a zone. The client does not wait for an ENTER. Opening the page on a server that has no object at 4722 can eject the session.

## Memory

Measured on the host, 2026-11-10:

| Piece | Bytes | When |
|---|---|---|
| `GroupsState` | 2700 | static `Client`, whether or not the page is open |
| `Ui` | 105056 | main-thread stack. The 112 KiB assert in `ui.cpp` still holds (limit 114688) |
| `GroupView` | 80 | inside that array, 32 slots |
| New textures | 0 | the page uses the shared Impress font |
| Scene-graph nodes | 0 | citro2d draws rectangles directly |
| `PageText` cache | about 28 KB heap | first time the page handles input or draws |
| citro2d glyph buffer | 768 glyphs | same first use, then kept |
| `FocusGroup` on the page | about 2.3 KB | inside `Ui` on the main stack, with the other pages |
| `tools/groups/ttgroups/*.py` | 37559 | host only, 0 bytes resident on the 3DS |

## Tests

```bash
tools/groups_test/run.sh
```

That runs `python3 tools/groups_test/test_groups.py` and `g++` on `tools/groups_test/groups_test.cpp`. No server is started. The Python file is not run under `python3 -I`, because the DCFile check imports `panda3d`. It fails, rather than skips, when Panda or `otp.dc` is missing. Set `OTT` to an open-toontown checkout, or init `ref/open-toontown`, so `etc/otp.dc` and `etc/toon.dc` are on disk. `tools/platform/platform.dc` is in this tree.

`tools/ui_size/run.sh` compiles `source/render/ui.cpp` with `arm-none-eabi-g++` and prints `sizeof(Ui)`. The 112 KiB `static_assert` in `ui.cpp` is the same check.

`python3 tools/groups/render_3ds.py` writes `layouts/3ds/{empty,loading,own,leave,error,picker}.png` from the rectangles in `groups_logic.h`. Those pictures are a software drawing of the sheet, not an Azahar capture.

`tools/padnav_test` still passes with the ten book-page buttons. The grid is `kY0 = 30`, `kBtnH = 32`, `kStepY = 36`, so the fourth row ends at y 170, above the tab bar.

This commit was not run in Azahar and not run on hardware. `make` with devkitARM r68 produced `toontown3ds.3dsx` with no warnings in `source/`. `tools/ui_size/run.sh` compiles `source/render/ui.cpp` with `arm-none-eabi-g++` (`-Wall -Wextra`) and the 112 KiB assert. The host check `adoptGlobalManager` covers taking DoId 4722 without an ENTER.
