# Interior and Street Life Specification

This document specifies the runtime behaviour, network messages, asset paths, and timings for
dynamic interior and street elements in Toontown 3DS:
1. Toon HQ Leaderboard (`DistributedHQInterior`)
2. Animated Interior Props (Pet Shop fish and Toon HQ periscope)
3. Swinging Door Leaves (`DistributedDoor`)
4. Knock-Knock Doors (`DistributedKnockKnockDoor`)
5. Landmark Interior Signs (`DistributedToonInterior`)

---

## 1. Toon HQ Leaderboard

### 1.1 Upstream Architecture
- **Distributed Class**: `DistributedHQInterior` (inherits from `DistributedObject`, DC ID 111).
- **Python Source**:
  - Client: `ref/ref/open-toontown/toontown/building/DistributedHQInterior.py`
  - AI: `ref/ref/open-toontown/toontown/building/DistributedHQInteriorAI.py`
  - Trophy Manager: `ref/ref/open-toontown/toontown/building/DistributedTrophyMgrAI.py`
- **DC Definition** (`etc/toon.dc`):
  ```
  dclass DistributedHQInterior : DistributedObject {
    setZoneIdAndBlock(uint32, uint16) required broadcast ram;
    setLeaderBoard(blob) required broadcast ram;
    setTutorial(uint8) required broadcast ram;
  };
  ```

### 1.2 Network Protocol and Server Behaviour
- **`setZoneIdAndBlock(zoneId, block)`**:
  Sent on object generation to identify the HQ zone and building block number.
- **`setLeaderBoard(blob)`**:
  The stock Open Toontown server sends a pickled Python 3-tuple `(avIds, names, scores)` using Python Pickle Protocol 1:
  - `avIds`: List of avatar IDs (`uint32`)
  - `names`: List of avatar names (`string`)
  - `scores`: List of trophy/building floor scores (`uint32`)
  In stock Open Toontown (`DistributedTrophyMgrAI.py`), `self.leaderInfo` initializes to `([], [], [])` (empty lists).
  When empty, `pickle.dumps(([], [], []), 1)` encodes to the 14-byte bytearray:
  `b'(]q\x00]q\x01]q\x02tq\x03.'` (hex: `28 5d 71 00 5d 71 01 5d 71 02 74 71 03 2e`).
  When scores are present, up to 10 entries (`numLeaders = 10`) are populated.
- **`setTutorial(uint8 flag)`**:
  - `flag != 0`: Tutorial mode active. Hides the periscope and wall speakers (`find('**/periscope').hide()`, `find('**/speakers').hide()`).
  - `flag == 0`: Normal HQ. Shows periscope and speakers. Default on stock server is `0`.

### 1.3 Board Geometry & Layout
- **Model Anchor**: In `phase_3.5/models/modules/HQ_interior.bam`, the board model node is `empty_board` located at:
  - Position: `(-47.4427, 29.6598, 8.15647)`
  - HPR: `(0, 0, 0)`
- **Board Transform**:
  `self.leaderBoard` is attached to `empty_board.getChild(0)` with transform:
  - Pos: `(0.1, 0, 4.5)`
  - HPR: `(90, 0, 0)` (facing into the HQ room towards the center)
  - Scale: `(0.9, 0.9, 0.9)`
- **Row Positioning**:
  - Row 0 (Title): `Z = 0.0`
    - Text: `TTLocalizer.LeaderboardTitle` = `"Toon Platoon"`
    - Color: `(0.5, 0.75, 0.7, 1.0)`
    - Align: Center (`ACenter`) at local `(0, 0, 0)`
  - Rows 1..10 (Leaders): `Z = -(row_index + 1) * 1.0` (scaled by 0.9)
    - Name: `ALeft` at `DHQInamePathPos = (-4, 0, 0)`, scale `DHQInamePath = 0.9`, color `(1, 1, 1, 0.7)`
    - Score: `ARight` at `DHQIscorePathPos = (-4.6, 0, 0)`, color `(1, 1, 0.1, 0.7)`
    - Trophy Star: At `DHQItrophyStarPos = (-6.6, 0, 0.3)`
    - Unfilled rows show `"-"` for name and score, and hide the trophy star.
- **Trophy Star Thresholds & Spinning**:
  - Asset: `phase_3.5/models/gui/name_star.bam`
  - Levels (`ToontownGlobals.TrophyStarLevels`): `[10, 20, 30, 50, 75, 100]`
  - Score < 10: Hidden
  - Score >= 10: Bronze star `(0.9, 0.6, 0.2, 1.0)`, scale 0.6. If >= 20: spins at 8 deg/s.
  - Score >= 30: Silver star `(0.8, 0.8, 0.8, 1.0)`, scale 0.6. If >= 50: spins at 10 deg/s.
  - Score >= 75: Gold star `(1.0, 1.0, 0.0, 1.0)`, scale 0.8. If >= 100: spins at 15 deg/s.

---

## 2. Animated Interior Props

### 2.1 Pet Shop Fish
- **Upstream Source**: `ref/ref/open-toontown/toontown/building/DistributedPetshopInterior.py:74-85`
- **Assets**:
  - Model: `phase_4/models/props/interiorfish-zero.bam`
  - Animation: `phase_4/models/props/interiorfish-swim.bam` (160 frames @ 24 fps)
- **Transform & Parameters**:
  - Anchor: `fish_origin` in Pet Shop interior at `(0, 6, -4)`
  - Scale: `0.8`
  - Color scale: `(0.8, 0.9, 1.0, 0.8)` (translucent blue-green aquatic tint)
  - Play rate: `0.7` (looping swim)
- **Bake Format**:
  - Baked to `interiorfish.ttm` and `interiorfish_swim.tta` with ETC1 textures.
  - Runtime animates via `render::Anim` and `Renderer::drawMorph`.

### 2.2 Toon HQ Periscope
- **Upstream Source**: `ref/ref/open-toontown/toontown/hood/HQPeriscopeAnimatedProp.py`
- **Assets**:
  - Model: `phase_3.5/models/props/HQ_periscope-mod.bam`
  - Animation: `phase_3.5/models/props/HQ_periscope-chan.bam` (149 frames @ 24 fps)
- **Sequence & Timing**:
  - Wait 2.0 s
  - Frames 0..40 (swivel out)
  - Wait 0.7 s
  - Frames 40..90 (scan left)
  - Wait 0.7 s
  - Frames 91..121 (scan right)
  - Wait 0.7 s
  - Frames 121..91 (scan center)
  - Wait 0.7 s
  - Frames 90..40 (re-center)
  - Wait 0.7 s
  - Frames 40..90
  - Wait 0.7 s
  - Frames 91..121
  - Wait 0.5 s
  - Frames 121..148 (retract)
  - Wait 3.0 s
  - Total loop duration: ~22.38 s.
- **Tutorial Mode**:
  When `setTutorial(1)` is active, periscope rendering is disabled.

### 2.3 SillyMeter
- Non-holiday operation uses the static flat model `phase_3.5/models/modules/tt_m_ara_int_sillyMeterFlat.bam`.
- Dynamic meter phases (0..15) are holiday-only (`SILLYMETER_HOLIDAY`) and skipped outside holidays per task instructions.

---

## 3. Swinging Door Leaves

### 3.1 Upstream Behaviour (`DistributedDoor.py`)
- Standard doors (`door_double_round_ur` in `phase_3.5/models/modules/doors_practical.bam`) have:
  - `door_*_flat`: Door frame and wall casing (static, baked into zone `.ttm`).
  - `door_*_right`: Right swinging panel, hinge at `(3.591, 0, 3.913)`.
  - `door_*_left`: Left swinging panel, hinge at `(-3.577, 0, 3.073)`.
- **Enter Movie (Toon enters building / departs zone)**:
  - The right door leaf swings open from 0° to +100° over 0.6 s (after 0.4 s latch delay).
  - Toon walks inside from `doorStartX` to `(1.5, 2.0, 0.025)`.
  - Right door closes from 100° to 0° over 1.0 s.
- **Exit Movie (Toon exits building / arrives in zone)**:
  - The left door leaf (`exitDoor`) starts open at -100° and swings closed to 0° over 1.0 s (`blendType='easeInOut'`).
  - Toon walks forward out of door from `(-1.5, 0)` to `(-1.5, -6.0)`.

### 3.2 3DS Implementation
- Both leaves are baked into:
  - `romfs/baked/door_right.ttm`
  - `romfs/baked/door_left.ttm`
- `source/world/transition.cpp`:
  - Entering: animates right leaf at `(3.591, 0, 3.913)` with `+doorSwing_`.
  - Exiting: animates left leaf at `(-3.577, 0, 3.073)` with `-doorSwing_`.

---

## 4. Knock-Knock Doors

### 4.1 Upstream Architecture
- **Distributed Class**: `DistributedKnockKnockDoor` (inherits from `DistributedAnimatedProp`, DC ID 239).
- **Python Source**:
  - `ref/ref/open-toontown/toontown/building/DistributedKnockKnockDoor.py`
  - `ref/ref/open-toontown/toontown/building/DistributedKnockKnockDoorAI.py`
  - `ref/ref/open-toontown/toontown/building/KnockKnockJokes.py`
  - `ref/ref/open-toontown/toontown/toonbase/TTLocalizerEnglish.py`

### 4.2 Joke Selection Algorithm
- Each knock-knock door on a street has a `propId` equal to the building `block` number.
- Default Joke:
  `KnockKnockJokes[propId % len(KnockKnockJokes)]` (118 jokes total).
- Contest Jokes by Street Branch (`KnockKnockContestJokes`):
  - Silly Street (`2100`): If `propId == 44` -> `['Wally', "Wally's not looking, hit him with a pie!"]`
  - Loopy Lane (`2200`): Dict of 4 jokes for blocks `[10, 11, 23, 24]`
  - Punchline Place (`2300`): If `propId == 1` -> `['Justin', 'Justin other couple of Cog parts and off we go!']`
  - Polar Place (`3300`): Dict of 5 jokes for blocks `[12, 17, 23, 24, 25]`

### 4.3 Sequence Timings & Dialog
- Interaction radius: 6.0 units from door trigger.
- Overall duration: 8.0 s (`d = 1.0 s` per beat):
  - `t = 0.0 s`: Door chat balloon: `"Knock, knock."` (`TTLocalizer.DoorKnockKnock`)
  - `t = 0.5 s`: SFX `GUI_knock_3.ogg`
  - `t = 1.0 s`: Avatar chat balloon: `"Who's there?"` (`TTLocalizer.DoorWhosThere`)
  - `t = 2.0 s`: Door chat balloon: `joke[0]` (Setup, e.g. `"Who"`)
  - `t = 3.0 s`: Avatar chat balloon: `joke[0] + " who?"` (e.g. `"Who who?"`)
  - `t = 4.0 s`: Door chat balloon: `joke[1]` (Punchline), rimshot SFX `AA_heal_telljoke.ogg`
  - `t = 8.0 s`: Cleanup and chat balloons fade out.

---

## 5. Landmark Interior Signs

### 5.1 Upstream Behaviour (`DistributedToonInterior.py:105-126`)
- In standard toon buildings (`TI_room`, `TI_room_2`, `TI_room_L`, `TI_room_T`), standard interiors have an anchor `sign_origin`.
- The sign from the exterior landmark building `**/tb%s:*_landmark_*_DNARoot/**/sign;+s` is copied to `sign_origin`.
- Upstream bounds and scaling:
  ```python
  SIGN_LEFT = -4.0
  SIGN_RIGHT = 4.0
  SIGN_BOTTOM = -3.5
  SIGN_TOP = 1.5
  ```
  The sign is fitted to maintain aspect ratio within `width <= 8.0` and `height <= 5.0`:
  - `scale = min((SIGN_RIGHT - SIGN_LEFT) / width, (SIGN_TOP - SIGN_BOTTOM) / height)`
  - Centered horizontally and vertically in the frame at `Y = -0.1`.

### 5.2 3DS Pipeline Implementation
- During interior baking (`tools/pipeline/interior_assemble.py`), when assembling standard interiors for street buildings:
  - If the building's street DNA contains a landmark sign with baselines, the sign and lettering are assembled using `sign_text.py` and fitted directly into `sign_origin`.
  - Reuses the existing sign text atlas and letter geometry with zero runtime overhead.
