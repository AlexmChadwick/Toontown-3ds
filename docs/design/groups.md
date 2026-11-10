# Groups

Groups is a Shticker Book page: create a group, see the list, join with one press, leave. It does not start invasions and it does not pick a district.

The wire matches the Android client. `tools/groups/groups.dc` is the same file. Field numbers below are the DCFile numbers when the server reads `otp.dc`, `toon.dc`, `platform.dc`, then `groups.dc`. `avatarGone` is field 2294. That is class-local index 11, not the wire id. The other eleven fields are numbered 2283 through 2293 on the assumption that `platform.dc` leaves no gap before `groups.dc`. This tree does not contain `platform.dc`, and Panda is not installed here, so that run of numbers was not recomputed with `DCFile`. The 3DS `CLIENT_HELLO` hash stays the stock otp+toon value **547155168**. `source/net/dc_generated.h` is not regenerated for this page.

The UberDOG object is DoId **4722**. A stock Open Toontown server never generates it. Until it does, the page shows "Groups are not available right now." and the client sends nothing. Sending a field to an object that was never generated gets the session ejected.

## What the 3DS client sends

All of these are `clsend`. Each one is a `CLIENT_OBJECT_SET_FIELD` (120) to DoId 4722.

| Field | Wire id | Body |
|---|---|---|
| `requestBoard` | 2283 | empty |
| `stopBoard` | 2284 | empty |
| `setContext` | 2285 | `u32` hood, `u32` shard, `u8` activity, `u16` byte length of task hoods |
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

The picker asks for a department (four cells) or a task playground (six). It covers the tab bar so the six cells and Back (44px, y 190) fit on the 240px screen. B closes the picker. B closes the book when the picker is not open.

After a successful join the client does not teleport. The status line comes from the same plan Android uses: already here, "Go to %s to meet your group.", or "You joined the group!".

`setContext` sends no task hoods (the `u16` length is 0). This client does not resolve NPC quest zones. Create still does not send a place. The UberDOG reads the avatar's location.

## Where it differs from Android

- The page is C++ and citro2d. `tools/groups/ttgroups` is the host reference. It is not linked into the 3DS binary.
- No book-button badge and no always-on subscription. The board is watched only while this page is the open page.
- No auto-teleport after join. The status line says where to go.
- Task hoods in `setContext` are empty, so a task group's "for your task" match will not light up from quest state.
- The client cache holds 32 groups. The server still allows 200.
- The picker hides the book tab bar. Android's 320x240 overlay was a separate window. Back stays on screen, and Join, Leave and the filters are at least 48px.
- B closes the picker first, then the book.
- Groups is the last page. The tenth tab is text, not a baked glyph.
- A stock server with no DoId 4722 shows the unavailable line and sends nothing.

## Memory

Measured on the host, 2026-11-09:

| Piece | Bytes | When |
|---|---|---|
| `GroupsState` | 2684 | static `Client`, whether or not the page is open |
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

That runs `python3 -I tools/groups_test/test_groups.py` and `g++` on `tools/groups_test/groups_test.cpp`. No server is started. The Panda DCFile check that `avatarGone` is 2294 is skipped when Panda or `platform.dc` is absent. The constant 2294 and the class-local index 11 are still asserted.

`python3 tools/groups/render_3ds.py` writes `layouts/3ds/{empty,loading,own,leave,error,picker}.png` from the rectangles in `groups_logic.h`. Those pictures are a software drawing of the sheet, not an Azahar capture.

`tools/padnav_test` still passes with the ten book-page buttons. The grid is `kY0 = 30`, `kBtnH = 32`, `kStepY = 36`, so the fourth row ends at y 170, above the tab bar.

This work was not run in Azahar and not run on hardware. devkitARM was not on the machine that added the page, so `make` was not run here.
