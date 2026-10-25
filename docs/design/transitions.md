# Place Transitions: Doors, Tunnels, Teleports, and Death

Status: design specification and implementation reference for place transitions.

Paths are relative to `ref/open-toontown/`, unless they start with `phase_`, which
means `ref/resources/`. `toon.dc` and `otp.dc` are `etc/toon.dc` and `etc/otp.dc`.

---

## 1. Overview and Transition Types

In Toontown, avatars navigate between zones and places via four primary transitions:
1. **Tunnels**: Street-to-playground and street-to-street transitions. Avatars walk into
   a curved tunnel geometry along a deterministic pivot path; the destination plays the
   arrival track in reverse, walking out of the tunnel mouth.
2. **Doors**: Entering and exiting buildings (shops, Toon HQ, landmark buildings, cog
   headquarters). Avatars walk toward the door threshold, the door leaf swings open,
   the screen irises out, the door shuts, the new zone loads, and the toon walks forward
   out of the interior door into the room.
3. **Teleports**: Fast travel via the Shticker Book map page, `teleportRequest`, whisper
   teleporting to friends, or facility completion exits. The toon pulls a portable hole
   from its right hand, drops it to the ground where it expands, dives headfirst into
   the hole, and the hole shrinks shut. The arrival plays the reverse: the hole expands,
   the toon leaps up into the air and lands on their feet.
4. **Died / Sadness**: Laff reaches 0 in battle or defeat. The toon plays the `lose`
   animation with sad eyes, droops, shrinks down into the ground, and teleports back
   to the safezone playground drop point.

---

## 2. Upstream Specification and Citations

### 2.1 Tunnel Transitions

#### Upstream Sources
- `toontown/hood/Place.py:588-630`: `enterTunnelIn`, `enterTunnelOut`, `exitTunnelIn`, `exitTunnelOut`
- `toontown/toon/DistributedToon.py:733-815`: `setTunnelIn`, `setTunnelOut`, `getTunnelInToonTrack`, `getTunnelOutToonTrack`, `handleTunnelIn`, `handleTunnelOut`
- `toontown/toon/LocalToon.py:565-612`: `handleTunnelIn`, `handleTunnelOut`, camera lerps and iris timing
- `etc/toon.dc:485-486`: `setTunnelIn`, `setTunnelOut` network field signatures

#### Parameters & Kinematics
- **Forward Speed**: `ToonForwardSpeed = 20.0` units/s (`16.0 * ToonSpeedFactor`, where `ToonSpeedFactor = 1.25`; `toontown/toon/Toon.py`).
- **Pivot Offset**: `tunnelPivotPos = (-14.0, -6.0, 0.0)` in `tunnelOrigin` space.
- **Pivot Angle**: `pivotAngle = 135.0°` (90° turn plus 45° lead-in).
- **Pivot Duration**: `pivotDur = 2.0` seconds.
- **Pivot Lerp Duration**: `pivotLerpDur = pivotDur * (90.0 / pivotAngle) = 2.0 * (90 / 135) = 1.3333` seconds.
- **Center Steering**: `tunnelCenterOffset = 9.0`, `tunnelCenterInfluence = 0.6`.
  Target X during turn:
  $$\text{targetX} = \text{tunnelCenterOffset} + (\text{currentX} - \text{tunnelCenterOffset}) \times (1.0 - \text{tunnelCenterInfluence})$$
- **Camera Offset**: In `tunnelOrigin` space, eye ends at `(0, 20, 12)` with heading `180°`, pitch `-20°`, roll `0°`. Camera lerp duration is `1.0` second (`easeInOut`).
- **Iris Duration**: `0.4` seconds. In `tunnelOut`, iris-out begins at `trackDuration - 0.5` seconds.

#### Phase Breakdown (Tunnel Out)
1. **Straight Approach** ($t = 0$ to $t_{\text{straight}}$):
   - $t_{\text{straight}} = |\text{startY} - (-6.0)| / 20.0$.
   - Toon walks straight along $Y$ to $Y = -6.0$, heading aligning to $180^\circ$ relative to `tunnelOrigin` over $0.8 \times t_{\text{straight}}$.
   - Camera lerps over $1.0$ s to `(0, 20, 12)` in `tunnelOrigin` space, looking down at the tunnel entrance.
2. **Curved Pivot** ($t = t_{\text{straight}}$ to $t_{\text{straight}} + 2.0$ s):
   - Pivot node at $(-14.0, -6.0, 0.0)$ rotates heading from $0^\circ$ to $-135^\circ$ over $2.0$ s.
   - Toon lerps inward toward centered X offset over $1.333$ s.
   - At $t_{\text{total}} - 0.5$ s, the top screen irises to black over $0.4$ s.
3. **QuietZone Load & Travel**:
   - Destination zone loaded behind black screen (`place.load(destZone)`).
   - Network location updated (`CLIENT_OBJECT_LOCATION`, `net.changeZone`).

#### Phase Breakdown (Tunnel In)
1. **Pivot Turn** ($t = 0$ to $t = 2.8$ s):
   - Camera placed at `tunnelOrigin: (0, 20, 12)`, heading $180^\circ$, pitch $-20^\circ$.
   - Screen irises in from black over $0.4$ s.
   - Delay $0.8$ s, then pivot node rotates heading from $-135^\circ$ to $0^\circ$ over $2.0$ s.
2. **Straight Exit** ($t = 2.8$ s to $t = 3.35$ s):
   - Straight lerp from pivot Y ($-6.0$) to $Y = 5.0$ at $20.0$ units/s ($11.0 / 20.0 = 0.55$ s).
   - At $t = 3.35$ s, camera returns to normal follow mode, player controller transitions to `walk`.

---

### 2.2 Door Transitions

#### Upstream Sources
- `toontown/building/DistributedDoor.py:255-272`: `avatarEnterDoorTrack`
- `toontown/building/DistributedDoor.py:441-469`: `avatarExitTrack`
- `toontown/building/DistributedDoor.py:531-585`: `enterOpening`, `enterClosing`
- `toontown/building/DistributedDoorAI.py:113-150`: AI door FSM states and transitions
- `etc/toon.dc:1803-1816`: `DistributedDoor` field definitions

#### Kinematics & Door Geometry
- **Door Frame**: Flat facade frame (`door_*_flat`) with opaque background decals (`door_*_hole_left/right`).
- **Swinging Leaves**: 3D door leaf (`door_*_right`) with hinge at local $X = 0$, $Y = 0$.
- **Swing Angle**: $\pm 100^\circ$ around hinge Z axis.
- **Door Offset**: $X_{\text{door}} = 1.5$ units from door origin center.
- **Floor Offset**: $Z = 0.025$ units above ground.

#### Sequence: Entering a Building (`DoorOut`)
1. **Trigger Hit & Request**:
   - Player enters `door_trigger_<block>`.
   - Client sends `DistributedDoor.requestEnter()` to door `doId`.
   - Input locked; place transitions to stopped.
2. **Door Opening & Walk-In** ($t = 0$ to $t = 1.0$ s):
   - Right door swings open from $0^\circ$ to $100^\circ$ over $0.6$ s after a $0.4$ s latch delay.
   - Sound `phase_3.5/audio/sfx/Door_Open_1.ogg` plays.
   - Toon walks forward in door space to $(1.5, 2.0, 0.025)$ over $1.0$ s (`easeIn`).
   - Camera follows toon at `(0, -8, height)`.
   - At $t = 0.5$ s, iris-out fades top screen to black over $0.5$ s.
3. **Door Closing & Zone Transition** ($t = 1.0$ s to $t = 2.0$ s):
   - Right door swings back from $100^\circ$ to $0^\circ$ over $1.0$ s (`easeInOut`).
   - Sound `phase_3.5/audio/sfx/Door_Close_1.ogg` plays.
   - Behind black frame, interior zone loads (`place.load(interiorZone)`).

#### Sequence: Exiting into Building / Street (`DoorIn`)
1. **Inside Door Spawn**:
   - Toon placed at threshold: $(-1.5, 0.0, 0.025)$ facing $179^\circ$ relative to interior `door_origin`.
   - Camera positioned behind toon at $(-1.5, 5.0, \text{height})$ facing $180^\circ$.
2. **Walk-Out & Fade-In** ($t = 0$ to $t = 1.0$ s):
   - Toon walks forward to $(-1.5, -6.0, 0.025)$ over $1.0$ s (`easeInOut`).
   - Top screen irises in from black over $0.4$ s.
   - Sound `Door_Close_1.ogg` plays as door shuts behind toon.
   - At $t = 1.0$ s, toon anim transitions to `neutral`, player control returned (`walk`).

---

### 2.3 Teleport Transitions

#### Upstream Sources
- `toontown/toon/Toon.py:1766-1798`: `getTeleportOutTrack`
- `toontown/toon/Toon.py:1929-1956`: `getTeleportInTrack`
- `phase_3.5/models/props/portal-mod.bam`: Portable hole mesh (flat quad)
- `phase_3.5/models/props/portal-chan.bam`: 82-frame portal animation at 24 fps ($3.416$ s)
- `phase_3.5/audio/sfx/AV_teleport.ogg`: Teleport sound effect

#### Teleport Out Sequence ($3.4$ s total)
1. **$t = 0.0$ s**:
   - Portable hole prop instantiated, attached to toon's right hand at scale $0.0$.
   - Toon begins `teleport` animation track (82 frames @ 24 fps).
   - Network broadcast sent: `setAnimState("TeleportOut", 1.0)`.
2. **$t = 0.5$ s**:
   - Sound `phase_3.5/audio/sfx/AV_teleport.ogg` plays.
   - Hole in hand expands to scale $0.4$.
3. **$t = 1.708$ s** (Frame 41):
   - Hole dropped from hand to floor beneath toon.
   - Hole rapidly expands on ground to full scale ($1.17$).
   - Toon dives forward headfirst into the hole.
4. **$t = 3.0$ s**:
   - Screen irises to black over $0.4$ s.
   - Toon geometry hidden as head passes through hole plane.
5. **$t = 3.125$ s to $3.4$ s**:
   - Hole shrinks down from scale $1.17$ to $0.0$.
   - Screen fully black at $t = 3.4$ s; destination zone loads.

#### Teleport In Sequence ($1.3$ s total)
1. **$t = 0.0$ s**:
   - Destination place loaded; toon positioned at spawn / teleport anchor.
   - Toon geometry hidden.
   - Hole placed on ground at $(0, 0, 0.025)$ relative to arrival point.
   - Hole expands from scale $0.0$ to $1.17$ over $0.3$ s (frames 81 down to 74 reversed).
   - Network broadcast sent: `setAnimState("TeleportIn", 1.0)`.
2. **$t = 0.3$ s**:
   - Screen irises in from black over $0.4$ s.
   - Toon geometry unhidden; toon begins `jump` animation from time $0.45$ s (rising leap).
3. **$t = 0.6$ s to $0.9$ s**:
   - Hole shrinks shut from scale $1.17$ to $0.0$.
4. **$t = 0.9$ s to $1.3$ s**:
   - Toon finishes jump arc and lands firmly on feet.
   - Animation transitions to `neutral`/`walk`. Control restored to circle pad.

---

### 2.4 Died / Sadness Transition

#### Upstream Sources
- `toontown/toon/Toon.py:1875-1883`: `getDiedInterval`
- `toontown/toon/Toon.py:1884-1905`: `enterDied`
- `phase_5/audio/sfx/ENC_Lose.ogg`: Sad death sound effect

#### Sequence ($6.8$ s total)
1. **$t = 0.0$ s**:
   - Laff reaches 0. Player input disabled.
   - Toon plays `lose` animation. Eyes switch to sad eyes.
2. **$t = 2.0$ s**:
   - Sound `ENC_Lose.ogg` plays.
3. **$t = 5.333$ s**:
   - Toon scale begins shrinking smoothly toward $(0.01, 0.01, 0.01)$ over $1.5$ s (`easeInOut`).
4. **$t = 6.4$ s**:
   - Screen irises out to black over $0.4$ s.
5. **$t = 6.833$ s**:
   - Screen fully black. Toon resets scale to $1.0$.
   - Zone changes to canonical playground drop point (e.g. zone 2000 for TTC).
   - Arrival plays `TeleportIn`. Laff set to 1.

---

## 3. Network Protocol (Astron DC Messages)

All transition fields are defined in `etc/toon.dc` and generated into `source/net/dc_generated.h`.

| Message | Class | DC Line | Field ID | Keywords | Arguments |
|---|---|---|---|---|---|
| `setTunnelOut` | `DistributedToon` | 486 | 478 | `ownsend broadcast` | `(int16 ts, int16/10 startX, int16/10 startY, int16/10 x, int16/10 y, int16/100 z, int32/100 h)` |
| `setTunnelIn` | `DistributedToon` | 485 | 477 | `ownsend broadcast` | `(int16 ts, int16/10 endX, int16/10 x, int16/10 y, int16/100 z, int32/100 h)` |
| `setAnimState` | `DistributedToon` | 487 | 479 | `ownsend broadcast` | `(string state, int16/1000 rate, int16 ts)` |
| `requestEnter` | `DistributedDoor` | 1809 | 495 | `airecv clsend` | `()` |
| `requestExit` | `DistributedDoor` | 1810 | 496 | `airecv clsend` | `()` |
| `avatarEnter` | `DistributedDoor` | 1811 | 498 | `broadcast` | `(uint32 avId)` |
| `avatarExit` | `DistributedDoor` | 1812 | 499 | `broadcast` | `(uint32 avId)` |
| `setState` | `DistributedDoor` | 1813 | 500 | `required broadcast ram` | `(string state, int16 ts)` |

### Packaging Rules
- Timestamps use Astron 16-bit centisecond clock (`tt::net::Client::networkTime`).
- Coordinate scale factors:
  - `startX`, `startY`, `endX`, `x`, `y`: packed fixed `scale = 10.0f` (`i16`).
  - `z`: packed fixed `scale = 100.0f` (`i16`).
  - `h`: packed fixed `scale = 100.0f` (`i32`).
- Sent via `TT_NET_OWN_UPDATE` macro ensuring compile-time verification of `ownsend` permissions.

---

## 4. 3DS Native Implementation Architecture

### 4.1 Class `Transition` (`source/world/transition.{h,cpp}`)

```cpp
namespace tt::world {

enum class TransitionType : u8 {
    None,
    Tunnel,
    Door,
    Teleport,
    Died
};

enum class TransitionState : u8 {
    Idle,
    Out,         // Playing departure animation / walk / camera lerp
    FadingOut,   // Screen fading to black
    Loading,     // Screen fully black; place.load() executes here
    FadingIn,    // Screen fading in from black
    In           // Playing arrival animation / walk / camera return
};

class Transition {
public:
    void init();
    void reset();
    void update(float dt, Player& player, Place& place, net::Client& net);
    void draw(render::Renderer& renderer, const C3D_FVec& eye);
    void drawFade(render::Renderer& renderer, bool stereo);

    bool idle() const { return state_ == TransitionState::Idle; }
    bool active() const { return state_ != TransitionState::Idle; }
    float fade() const { return fadeAlpha_; }

    void startTunnelOut(u32 targetZone, const Exit& exit, const Player& player);
    void startDoorOut(u32 targetZone, const Exit& exit, const Player& player, u32 doorDoId);
    void startTeleportOut(u32 targetZone);
    void startDied(u32 safezoneId);

    void applyInput(circlePosition& pad, u32& held, u32& down);
    void applyAnimation(ToonAvatar& toon, float dt);
    const char* toonNetworkState() const;
};

} // namespace tt::world
```

### 4.2 Asset Pipeline & Memory Budgets

| Asset | File | Tris | Verts | Size | Memory Pool |
|---|---|---|---|---|---|
| Portal Quad | `romfs:/baked/portal.ttm` | 4 | 8 | 11.4 KB | Linear Heap |
| Door Right Leaf | `romfs:/baked/door_right.ttm` | 50 | 28 | 23.0 KB | Linear Heap |
| Teleport SFX | `romfs:/baked/audio/teleport.tsnd` | — | — | 43.9 KB | Linear Heap |
| Door Open SFX | `romfs:/baked/audio/door_open.tsnd` | — | — | 38.8 KB | Linear Heap |
| Door Close SFX | `romfs:/baked/audio/door_close.tsnd` | — | — | 36.5 KB | Linear Heap |
| Death SFX | `romfs:/baked/audio/enc_lose.tsnd` | — | — | 48.0 KB | Linear Heap |

Total transition asset footprint: **~200 KB**, strictly inside the 24 MB linear heap budget.
Draw call impact: +1 draw call for the portal hole or swinging door during transition; 0 additional draw calls during regular gameplay.
