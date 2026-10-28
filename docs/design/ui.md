# UI design: Make-a-Toon, Shticker Book, HUD, friends, whispers

Status: design spec for Phase 4+. Research only. It describes no code that exists yet.
Paths are relative to `ref/open-toontown/` unless they start with `ref/resources/`.
DC field numbers are never hand-copied: take them from `source/net/dc_generated.h`.

## 1. Key facts (read first)

1. **Make-a-Toon is a pure client feature.** The server only receives one message,
   `createAvatar(blob dna, uint8 slot)`, and validates nothing beyond
   `ToonDNA.isValidNetString` and the slot (`otp/login/AstronLoginManagerUD.py:415-435`).
2. **The server names the toon `"<Color> <Species>"`**, for example "Bright Red Dog".
   The colour comes from `headColor` (`AstronLoginManagerUD.py:464-470`, with
   `NumToColor` / `AnimalToSpecies` at `toontown/toonbase/TTLocalizerEnglish.py:5474,5501`).
   The name shop can be skipped entirely.
3. **Friends do not exist on this server.**
   - Every friends UD and AI class is a 5-line stub:
     - `otp/friends/AvatarFriendsManagerUD.py`
     - `otp/friends/PlayerFriendsManagerUD.py`
     - `toontown/friends/TTPlayerFriendsManagerUD.py`
     - `otp/friends/FriendManagerAI.py`
   - The UD repository only instantiates `AstronLoginManager`
     (`toontown/uberdog/ToontownUDRepository.py:47`).
   - The AI never generates a `FriendManager` (`toontown/ai/ToontownAIRepository.py:196-233`).
   - Upstream's own client stubs the request: `sendGetFriendsListRequest` prints "TODO"
     under Astron (`toontown/distributed/ToontownClientRepository.py:855-858`).
   - **Decision:** ship no friends UI that talks to 4686/4687. A "Nearby toons" list
     replaces it.
4. **SpeedChat whispers do work.** A whisper is a `clsend ownrecv` field update sent
   **to the recipient's own DistributedToon doId** (`otp/avatar/DistributedPlayer.py:138-143`).
   No uberdog is involved.
5. **Never send the legacy OTP message types** `CLIENT_GET_FRIEND_LIST` or
   `CLIENT_GET_AVATAR_DETAILS`. Upstream still has code paths for them
   (`ToontownClientRepository.py:425-430`, `:860-863`), but Astron does not know these
   message types. Sending one is a protocol violation that ejects the client.

## 2. Make-a-Toon (MAT)

### 2.1 Protocol

DC definitions are in `etc/otp.dc:517-540`.

```
struct PotentialAvatar { uint32 avNum; string avName; blob avDNA; uint8 avPosition; uint8 nameState; };
dclass AstronLoginManager : DistributedObject {            // doId 4670 (astron/config/astrond.yml:38-40)
  requestAvatarList() clsend;           avatarListResponse(PotentialAvatar[]);
  createAvatar(blob, uint8) clsend;     createAvatarResponse(uint32);
  setNamePattern(uint32, int16, uint8, int16, uint8, int16, uint8, int16, uint8) clsend;
  namePatternAnswer(uint32, uint8);
  setNameTyped(uint32, string) clsend;  nameTypedResponse(uint32, uint8);
  acknowledgeAvatarName(uint32) clsend; acknowledgeAvatarNameResponse();
  requestRemoveAvatar(uint32) clsend;   requestPlayAvatar(uint32) clsend; };
```

Message sequence. The client mirrors `otp/login/AstronLoginManager.py:28-33`.

1. Send `createAvatar(dna15, slot)`.
   - `slot` must be less than 6 **and** empty in `ACCOUNT_AV_SET`. Otherwise the server
     closes the connection (`AstronLoginManagerUD.py:417-420`, `:453-457`).
   - Take the slot from the `avPosition` values that are missing from `avatarListResponse`.
2. The server creates a `DistributedToonUD` with:
   - `setName = "<Color> <Species>"`
   - `WishNameState = 'OPEN'`
   - `setDNAString = dna`

   Then it replies `createAvatarResponse(avId)` (`:464-505`). A zero avId or a database
   failure closes the connection (`:480-503`).
3. Optionally rename the toon (§2.4). Then send `requestPlayAvatar(avId)` as in the
   Phase 3 flow.
4. **Tutorial.** A new toon has `tutorialAck = 0`. Upstream asks the `TutorialManager` (an
   AI object in the management zone, `ToontownAIRepository.py:228-229`) to skip it:

   ```
   TutorialManager.requestSkipTutorial() airecv clsend → skipTutorialResponse(uint8)
   ```

   This is defined at `etc/toon.dc:1497-1505`, called from
   `ToontownClientRepository.py:505-517`, and handled at
   `toontown/tutorial/TutorialManagerAI.py:298-307`. That path also adds the matching
   quest history.
   - AGENTS.md already says the server tolerates going straight to the playground.
   - Calling `requestSkipTutorial` once after the first `requestPlayAvatar` is cheap and
     keeps the server state consistent. **Recommended.**

`nameState` in `PotentialAvatar` (`AstronLoginManagerUD.py:384-400`):

| Value | Meaning |
|---|---|
| 0 | LOCKED or unknown |
| 1 | OPEN (default name, may still rename) |
| 2 | PENDING |
| 3 | APPROVED (`avName` is the WishName) |
| 4 | REJECTED |

### 2.2 DNA netString (exactly 15 bytes, all `uint8` unless stated)

Written by `toontown/toon/ToonDNA.py:2437-2465` and validated at `:2467-2519`. On the wire
it is a DC `blob`, so a `u16` length (=15) comes first.

| Byte | Field | Valid range accepted by the server | What MAT actually offers |
|---|---|---|---|
| 0 | type | char `'t'` | `'t'` |
| 1 | head | < 34, indexing `toonHeadTypes` (`:18`) | species start (`toonHeadAnimalIndices` `:97` = 0,4,8,12,14,18,22,26,30) + variant 0-3. **Mouse has only 2 heads** (12 `mls`, 13 `mss`) |
| 2 | torso | < 9, indexing `toonTorsoTypes` (`:168`): `ss ms ls sd md ld s m l` | boys and girls in shorts use 0-2; girls in a skirt use 3-5. Never send 6-8: those are MAT's "no clothes yet" preview torsos (`makeatoon/BodyShop.py:181-185`) |
| 3 | legs | < 3, indexing `toonLegTypes` `s m l` (`:177`) | 0-2 |
| 4 | gender | 1 = male, anything else = female | |
| 5 | topTex | < 151 (`Shirts` `:178`) | MAT: `bss1`/`bss2` give 0/1 (boy); `gss1`/`gss2` give 0/1 (girl) |
| 6 | topTexColor | < 31 (`ClothesColors` `:637`) | from the style's colour list (`ShirtStyles` `:668`) |
| 7 | sleeveTex | < 138 (`Sleeves` `:376`) | equals topTex for the MAT styles |
| 8 | sleeveTexColor | < 31 | equals topTexColor |
| 9 | botTex | boy < 58 (`BoyShorts` `:514`); girl < 63 (`GirlBottoms` `:574`) | boy: `bbs1`/`bbs2` give 0/1. Girl: skirt `gsk1` gives 0 and needs torso 3-5; shorts `gsh1` gives 5 and needs torso 0-2 (`BottomStyles` `:1236`) |
| 10 | botTexColor | < 31 | style's list, for example `bbs1`: 0,1,2,4,6,9-20 |
| 11 | armColor | < 27 (`allColorsList` `:1905`) | 1-24 (`defaultBoyColorList`/`defaultGirlColorList` `:1932,1956`) |
| 12 | gloveColor | **must be 0** (white) | 0 |
| 13 | legColor | < 27 | 1-24 |
| 14 | headColor | < 27 | 1-24. This value also picks the server's default name |

The MAT clothing sets come from `TailorCollections[MAKE_A_TOON]` (`ToonDNA.py:1669-1673`).
The exact colour index lists are `ShirtStyles` `bss1` (`:668`), `bss2` (`:682`),
`gss1` (`:857`) and `gss2` (`:876`), plus `BottomStyles` `bbs1` (`:1236`), `bbs2` (`:1253`),
`gsk1` (`:1393`) and `gsh1` (`:1522`). Bake these lists into a const table rather than
porting the dicts.

**3DS rule.** Build the DNA only from the MAT tables above, even though the server
accepts more. Torso/bottom mismatches pass validation but render wrongly upstream: a
skirt texture on a shorts torso. The torso/bottom pairing is enforced in
`BodyShop.py:42-47`.

### 2.3 Screen flow (upstream FSM `makeatoon/MakeAToon.py:56-62`)

Upstream goes Gender → Body → Color → Clothes → Name → Done. The 3DS keeps the same
order, minus the name step.

- **Top screen:** the toon preview in the neutral animation on a plain backdrop. Rotate it
  with L/R.
  - Skip the 154 KB MAT room (`phase_3/models/makeatoon/tt_m_ara_mat_room.bam`) and its
    drop/squish animations (`MakeAToon.py:185-208`).
  - `tt_m_ara_mat_protoMachine` (`:631`) is not in `ref/resources`.
- **Bottom screen:** one page per step with left/right arrows and a list of swatches.

  | Page | Contents |
  |---|---|
  | 1. Gender | Boy/Girl. Seed the DNA as `newToonRandom(gender, stage=1)` does (`GenderShop.py:100`) |
  | 2. Body | species (9), head (4, or 2 for mouse), torso size (3), legs (3) |
  | 3. Color | head/arm/leg, each from colours 1-24, plus an "all parts" toggle (`ColorShop.py:21-47`) |
  | 4. Clothes | top style × colour, bottom style × colour. Girls choose skirt or shorts, which flips the torso between 3-5 and 0-2 |
  | 5. Confirm | shows the default name the server will assign (NumToColor[headColor] + species), "Create" |

**Assets.**
- Preview: every species head file from `ref/resources/phase_3/models/char/`, at LOD 500:
  - `*-heads-500.bam` for cat, horse, mouse, rabbit, duck, monkey, bear and pig. Each file
    holds the head variants as sub-nodes.
  - `dog{SS,MM,LL}_Shorts-headMuzzles-500.bam` for dogs.
- Bodies: `tt_a_chr_dg{s,m,l}_{shorts,skirt}_torso_*` and `tt_a_chr_dg{s,m,l}_shorts_legs_*`.
  The whole char directory is 2.7 MB raw.
- **Only one preview toon is resident at a time.** Bake per-part `.ttm` files and load them
  on demand, each under 300 KB.
- Clothes textures: MAT needs only `desat_shirt_1/2`, `desat_sleeve_1/2`,
  `desat_shorts_1/2` and `desat_skirt_1`, plus the shorts texture at `GirlBottoms[5]`.
  All are under `ref/resources/phase_3/maps/`.
- GUI:
  - `phase_3/models/gui/tt_m_gui_mat_mainGui.bam` (29 quads, 7 textures, 1.6 MB raw).
    Bake it to an RGBA4 sprite sheet.
- Audio:
  - `phase_3/audio/bgm/create_a_toon.ogg`
  - `phase_3/audio/sfx/GUI_create_toon_back.ogg`

### 2.4 Optional: Pick-a-Name (cheap)

`setNamePattern(avId, p1, f1, p2, f2, p3, f3, p4, f4)`:
- Each `p` is an id into `ref/resources/phase_3/etc/NameMasterEnglish.txt`, whose lines are
  `id*category*text`.
- The parts are title, first, last-prefix and last-suffix. Category ids and counts:

  | Category | Ids | Count |
  |---|---|---|
  | titles (boy/girl/neutral) | 0/1/2 | 7/6/20 |
  | firsts (boy/girl/neutral) | 3/4/5 | 65/38/106 |
  | capPrefix | 6 | 2 |
  | lastPrefix | 7 | 94 |
  | lastSuffix | 8 | 97 |
- Pass `-1` for an unused part. Unknown ids become `''` (`AstronLoginManagerUD.py:551-562`).
- `f` = 1 capitalises the part. The server joins `p3+p4` without a space.
- The server only checks `WishNameState == 'OPEN'` (`:542-546`). It sets `LOCKED` and
  replies `namePatternAnswer(avId, 1)` (`:568-574`).
- Bake the list as a 435-entry string table (about 4 KB).
- **Do not use `setNameTyped`.** It moves the name to `PENDING` forever, because there is no
  approver (`:623-632`, `status = 1 # TODO`).

## 3. HUD (top screen)

The upstream positions are laff meter `(-1.18, 0, -0.87)`, scale 0.075
(`toontown/toon/LocalToon.py:370-376`).

### Laff meter (`toontown/toon/LaffMeter.py`)

- **Source:** `setMaxHp(int16 = 15)` and `setHp(int16 = 15)` (`etc/toon.dc:450-451`). Both
  are `required broadcast ownrecv db`, so we get them in the owner generate and on every
  change.
- **Model:** `phase_3/models/gui/laff_o_meter.bam` (19 quads, 4 textures).
  - Head silhouettes per species: `**/doghead`, `cathead`, `mousehead`, `horsehead`,
    `bunnyhead`, `duckhead`, `monkeyhead`, `bearhead`, `pighead` (`:34-56`). They are
    tinted with the DNA `headColor` (`:57-59`).
- **Face:**

  | HP | Shown | Tint |
  |---|---|---|
  | < 1 | `frown` | `deathColor` (0.58, 0.80, 0.34) (`:7`, `:151-153`) |
  | ≥ max | `smile` + `eyes` | head colour |
  | otherwise | `open_smile` + `eyes`, `hp`/`max` numbers on the eyes | head colour |

  In the "otherwise" case, `tooth_1..6` are hidden as HP falls below 5/6, 4/6, …, 0 of max
  (`fractions` `:80-85`, `:114-120`).
- **Effect:** pulse larger or smaller on a change (`:128-137`). The 3DS uses a 0.2 s
  scale tween.
- **Bake:** a single RGBA4 sheet of about 64×64 per element, drawn with citro2d, overlaying
  the top screen after `Renderer::endWorld()`.

### Jellybeans

- **Fields:** `setMoney(int16 = 0)`, `setMaxMoney(int16 = 40)`,
  `setBankMoney(int16 = 0)` and `setMaxBankMoney(int16 = 12000)`, all
  `required broadcast ownrecv db` (`etc/toon.dc:446-449`).
- Upstream only shows the jar inside the book (`toontown/shtiker/InventoryPage.py:27-28`)
  and on reward screens. It uses `phase_3.5/models/gui/jar_gui.bam` `**/Jar` with yellow
  text (0.95, 0.95, 0).
- **3DS:** jar plus count on the Toon tab, and a "+N beans" toast on the top screen when
  `setMoney` increases.

## 4. Shticker Book → bottom-screen tabs

Upstream pages are added in `LocalToon.py:330-356`: Options, District (Shard), Map,
Inventory, ToonTasks, Gag Tracks, Cog Gallery, Photo and Fish, with more later. The book
GUI is `phase_3.5/models/gui/stickerbook_gui.bam` (23 quads, 12 textures, 2.7 MB raw).
**Do not bake the book frame.** The bottom screen *is* the book. Reuse only small icons.

| 3DS tab (AGENTS.md §5) | Upstream source | Data / protocol |
|---|---|---|
| **Map** | minimap (Phase 2), plus a "World" sub-view from `shtiker/MapPage.py` | world map image `phase_3.5/models/gui/toontown_map.bam` (one 256 KB texture; bake at 256×192 RGB565) and `cloud.bam` for unvisited hoods. A hood is visible if it is in `setHoodsVisited(uint32[] = [2000])` (`toon.dc:467`). It is teleportable if it is in `setTeleportAccess(uint32[])` (`toon.dc:510`) (`MapPage.py:193-211`). Tapping a hood sends `checkTeleportAccess(uint16 hoodId)` `airecv ownsend` (`toon.dc:511`, `MapPage.py:235-240`), then runs the zone-change flow (see `docs/design/zones.md` and `playground.md`). "Back to playground" uses `setLastHood` (`toon.dc:469`) (`MapPage.py:223-226`) |
| **Chat** | SpeedChat tree, plus a whisper target picker (§6) | `setSC(uint16)` `broadcast ownsend airecv` (`otp.dc:243`). Whisper: `setWhisperSCFrom` |
| **Toon** | LaffMeter, the jar and the AvatarPanel | name (`setName`, `otp.dc:221`), laff `hp/maxHp`, beans `money/maxMoney`, bank, DNA preview, **Nearby toons** list (§5) |
| **Gags** | `shtiker/InventoryPage.py` | gag grid from `setInventory(blob)` (`toon.dc:460`), track progress from `setExperience(blob)` (`toon.dc:455`), `setTrackAccess(uint16[7])` (`toon.dc:457`), `setMaxCarry(uint8)` (`toon.dc:456`). The blob format and icons (`phase_3.5/models/gui/inventory_icons.bam`, 50 quads, 4 textures) are specified in `docs/design/combat.md` |
| **Settings** | OptionsPage, plus a District picker from `shtiker/ShardPage.py` | districts and populations from the `DistributedDistrict`/`ToontownDistrictStats` objects already under interest (4618, 3) (`ShardPage.py:90-110`). Switching district is a full shard change: leave the zone, then `CLIENT_OBJECT_LOCATION` with the new parent |
| Debug | unchanged | |

**Not ported:** ToonTasks, Cog Gallery, Fish, Photo, Kart, Golf, Disguise, Events, News,
Garden, and NPC SOS. They belong to later features.

## 5. Friends: what to build given the stubbed server

**DC (reference only).**
- `FriendManager` (`otp.dc:308-322`): `friendQuery(int32)`, `inviteeFriendResponse(int8, int32)`
  and so on. These are AI-side, and **no instance exists**.
- `AvatarFriendsManager` (`otp.dc:448-462`) at doId 4686 and `TTPlayerFriendsManager`
  (`toon.dc:2853`, base `otp.dc:464-477`) at doId 4687. These are uberdogs declared in
  `astron/config/astrond.yml:18-24`. Their UD classes are empty stubs and are never generated.

What does work:
- **`setFriendsList(FriendEntry[] = [])`** `ownrecv required db airecv` (`otp.dc:245`),
  with `struct FriendEntry { uint32 friendId; uint8 friendCode; }` (`otp.dc:227-230`).
  - It arrives in our owner generate as a `u16` byte-length array of 5-byte entries. For
    new toons it is always `[]`.
  - Parse it and store the ids. There is no way to resolve offline names: no friend-list or
    avatar-details query exists under Astron.
- **`friendsNotify(int32, int8)`** `ownrecv airecv clsend` (`otp.dc:222`). Ignore it.
- **`teleportQuery`/`teleportResponse`/`teleportGiveup`** on DistributedToon
  (`toon.dc:513-516`). These are friend teleport. Out of scope.

**3DS design.**
- The Toon tab gets a **Nearby toons** list: the remote `DistributedToon`s currently in the
  DO table, sorted by distance.
- Each row shows the name, a laff mini-meter (their `setHp`/`setMaxHp` are `broadcast`),
  and a **Whisper** button.
- If a future server implements friends, add a Friends sub-list then. Reserve the UI slot
  but send nothing.
- **Never send to 4686/4687.** No harm results, but nothing answers.

## 6. Whispers

DC: `DistributedPlayer` (`otp.dc:232-256`). All of these are `ownrecv clsend`:

```
setWhisperFrom(uint32 fromAvId, string(0-256) text, uint32 senderAccount)    // typed
setWhisperWLFrom(uint32, string(0-256), uint32)                               // whitelist typed
setWhisperSCFrom(uint32 fromAvId, uint16 msgIndex)                            // SpeedChat static
setWhisperSCCustomFrom(uint32, uint16)    setWhisperSCEmoteFrom(uint32, uint16)
setSystemMessage(uint32 aboutId, string(0-256)) ownrecv                       // server → us only
setWhisperSCToontaskFrom(uint32, uint32, uint32, uint32, uint8) ownrecv clsend   (toon.dc:504)
```

**Send** (`otp/avatar/DistributedPlayer.py:138-143`):
- Send `CLIENT_OBJECT_SET_FIELD` with:
  - `doId` = the **recipient's** avId
  - `fieldId` = `DistributedPlayer.setWhisperSCFrom`, resolved against the recipient's
    class, `DistributedToon`
  - args `[ourAvId: u32, msgIndex: u16]`
- The State Server forwards `ownrecv` fields to the owning client. The AI never sees the
  field, because it is not `airecv`.
- **Constraints.**
  - Only whisper toons whose doId is currently visible to us. Astron's client agent
    rejects updates to objects it has not sent us.
  - **Verify against the local server before relying on whispers to off-zone toons.**
  - Add `setWhisperSCFrom`, plus later `setWhisperSCEmoteFrom`, to the outgoing-field
    whitelist as "clsend on others". Do not add `setWhisperFrom`, because typed chat is out
    of scope (AGENTS.md §7).
- **Rate limit:** at most 1 whisper per second, client-side.

**Receive.**
- `setWhisperSCFrom(fromId, msgIndex)` arrives as a field update on **our own** toon.
- Upstream (`DistributedPlayer.py:145-158`) drops it if `fromId` is unknown or ignored.
  Otherwise it decodes `msgIndex` through `SCDecoders.decodeSCStaticTextMsg`, which uses
  the OTPLocalizer SpeedChat static text table. Use the same baked table as the Chat tab.
- Show a top-screen toast: "Whisper from <name>: <text>". Keep the last 20 in a Chat-tab
  history. Tapping one sets that sender as the reply target.
- `setSystemMessage` uses the same toast with a different colour.
- The SpeedChat colour/style comes from `setSpeedChatStyleIndex(uint8 = 1)`
  (`toon.dc:508`). It is cosmetic.

## 7. Assets summary

All of these were checked to exist under `ref/resources/`.

| Path | Use | Raw size |
|---|---|---|
| `phase_3/models/gui/tt_m_gui_mat_mainGui.bam` | MAT arrows, swatches, buttons | 7 tex, 1.6 MB |
| `phase_3/models/gui/tt_m_gui_ups_mainGui.bam` | upsell (skip) | |
| `phase_3/models/char/*-heads-500.bam`, `dog*-headMuzzles-500.bam`, `tt_a_chr_dg*_{torso,legs}_*` | MAT preview | 2.7 MB dir |
| `phase_3/maps/desat_{shirt,sleeve,shorts,skirt}_*.jpg` | MAT clothes | |
| `phase_3/etc/NameMasterEnglish.txt` | Pick-a-Name (optional) | |
| `phase_3/models/gui/laff_o_meter.bam` | laff meter | 4 tex, 0.9 MB |
| `phase_3.5/models/gui/jar_gui.bam` | jellybean jar | 1 tex |
| `phase_3.5/models/gui/toontown_map.bam`, `cloud.bam` | world map | 256 KB + 1 tex |
| `phase_3.5/models/gui/inventory_icons.bam` | gag icons | 4 tex, 1 MB |
| `phase_3.5/models/gui/friendslist_gui.bam`, `avatar_panel_gui.bam` | list/panel icons (optional) | 3 tex |
| `phase_3/audio/bgm/create_a_toon.ogg` | MAT music | |
| `phase_3.5/audio/sfx/GUI_stickerbook_{open,turn,delete}.ogg` | tab sounds | |

**Bake rule.** These GUI `.bam` files are all textured quads. The baker should:
- extract each named node's texture sub-rectangle into one RGBA4 sprite atlas per
  feature, `.t3x`-like;
- keep the UI+font sub-budget at 2 MB or less (AGENTS.md §3).

## 8. Effort (dev-days, one engineer)

| Item | Days |
|---|---|
| DNA tables + netString builder + validation mirror (unit-testable on PC) | 1 |
| MAT pages (5 bottom-screen pages) + preview toon part swapping + `createAvatar` flow + slot pick + `requestSkipTutorial` | 4-5 |
| Pick-a-Name (optional) | 1.5 |
| Laff meter + beans HUD/toasts | 1.5 |
| Toon tab (stats, nearby toons) | 1 |
| Map world-view + teleport button (excludes the zone-change machinery) | 1.5 |
| Gags tab (display only; depends on combat.md inventory parser) | 1 |
| District picker | 1 |
| SpeedChat whispers send/receive + history | 1.5 |
| Friends list | 0 (server does not support it; reserve the UI slot only) |

## Minimap

M2.5. The Map tab draws a baked top-down picture of the current exterior zone under the
existing markers, our arrow and the teleport row. No picture means the old marker-only view.

- **Bake:** `.venv/bin/python -I tools/pipeline/bake_minimap.py [ZONE ...]` reads each
  `sdmc-out/3ds/toontown3ds/data/zones/<zone>.tz`, decodes the far-LOD chunk TTMs (near
  if a chunk has none, via `street_preview.load_model`), renders them orthographically
  from above with Panda's software offscreen buffer (4x supersampled, +X right, +Y up),
  box-filters, and encodes one ETC1 level. Options: `--zones-dir`, `--out-dir`,
  `--preview-dir` (PNGs, default `sdmc-out/previews/minimap/`). Output is deterministic and
  independent of zone order; ETC1 uses at most 4 threads. All 25 zones take about 75 s.
- **Image size:** per aspect ratio of the floor bounds plus 8 units margin: 256x256,
  256x128 / 128x256 (>= 1.5:1), 256x64 / 64x256 (>= 3:1). Pixels are square.
- **`.tmap` v1** (little endian, `<zone>.tmap` beside `<zone>.tz`): `"TMAP"`, u16 version
  = 1, u16 flags; u16 width, height; u8 gpuFormat (0xC ETC1), u8 mips = 1, u16 pad;
  f32 minX, minY, maxX, maxY (the world rectangle the image covers exactly); u32
  dataOffset (128), u32 dataSize; zero padding to 128; then PICA-tiled ETC1 data.
  World to image: `u = (x - minX) / (maxX - minX)`, `v = (maxY - y) / (maxY - minY)`.
- **Install:** copy to `sdmc:/3ds/toontown3ds/data/zones/` with the `.tz` files (25 files,
  16-33 KB each, about 0.6 MB). Street cells use their branch's file (2101 -> 2100);
  interiors have none.
- **Runtime:** `Ui::setMapZone(fileZone)` (called from `main.cpp` every frame, a no-op while
  the id is unchanged) loads the texture with `render::loadTexture` into a `C3D_Tex`
  (about 32 KB linear) and `drawMap` places markers with the `.tmap` rectangle.
- **Markers** (2026-10-28): built once per zone from the zone's `.pts` anchors by
  `ui::placeMarkers` (`ui/map_view.*`): Toon HQ (white), landmark shops (yellow, playgrounds
  only), trolley (purple; from `TrolleyRide` when the anchor set predates `trolley_station`),
  fishing docks (blue, one per dock) and tunnels (orange), plus other players (cyan). A legend
  in the map's right margin lists the kinds present. Interiors show toons only.
- **Teleport row and back button** (MapPage rules): hoods are blue (in `setTeleportAccess`),
  paper (in `setHoodsVisited` only: walk there) or grey (not visited / not installed); a
  locked press shows a short reason in the place label. A map teleport sends
  `checkTeleportAccess(hood)` first. The left button is "Playground" ("Cog HQ" in Cog HQ
  lobbies) off a playground (`MapPage.backToSafeZone`), else "Go Home", greyed when the
  server has no EstateManager. Offline, and in autotest runs (fresh test toons), every hood
  stays open.
