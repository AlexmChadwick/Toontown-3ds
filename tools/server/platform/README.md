# Platform badges: server pieces and protocol

Every toon's nametag shows a small badge left of its name when that toon's platform differs
from the viewer's own platform (exact compare; UNKNOWN means no badge, and our own toon never gets one).
Art lives in `tools/platform-badges/` (source of truth `/workspace/toontown/platform-badges-kit/`).

## Platform enum (wire value, uint8; also the atlas cell index)

| id | enum | reported by |
|----|------|-------------|
| 0 | UNKNOWN | anything else; old clients never report |
| 1 | WEB | web client (not in an immersive session) |
| 2 | MAC | native macOS |
| 3 | IOS | iOS and iPadOS |
| 4 | ANDROID | Android |
| 5 | WINDOWS | native Windows |
| 6 | VR | Quest APK, SteamVR launcher, web page while an immersive session is active |
| 7 | N3DS | Nintendo 3DS |
| 8 | LINUX | native Linux |

The server clamps any other value to 0. Every port has a debug override: `TT_PLATFORM` env
var or `tt-platform` prc var (Python ports), `?platform=` URL parameter (web/VR dev builds),
and `platform.txt` beside `server.txt` (3DS). Names or numbers are accepted (`mac`, `3ds`, `7`).

## DC: a third file, `platform.dc`

`otp.dc` and `toon.dc` are **not edited**. `platform.dc` is loaded **last** (after toon.dc)
by Astron, the UD and new clients, so every existing class/field number is byte-identical
and the new ones come after:

```
from ttplatform import TTPlatformManager/AI/UD

dclass TTPlatformManager : DistributedObject {
  setMyPlatform(uint8) clsend;          // client -> UD
  requestPlatforms(uint32 []) clsend;   // client -> UD, at most 50 avIds
  setPlatforms(uint32 [], uint8 []);    // UD -> one client (parallel arrays)
  clearPlatform(uint32);                // server internal (disconnect post-remove)
};
```

| item | number |
|---|---|
| class `TTPlatformManager` | **398** (= number of classes in otp.dc + toon.dc) |
| `setMyPlatform` | **2270** |
| `requestPlatforms` | **2271** |
| `setPlatforms` | **2272** |
| `clearPlatform` | **2273** |
| global doId | **4721** (not used in OtpDoGlobals, ToontownGlobals or astrond.yml) |

The parent is `DistributedObject` (like every upstream uberdog). There is no
`DistributedObjectGlobal` dclass in the DC files; the Python classes are still
`DistributedObjectGlobal*`. `test_platform.py` checks these numbers.

### Hash pinning

Panda's `DCFile.getHash()` over otp.dc + toon.dc is **547155168**. Adding platform.dc changes
the computed hash, so:
- astrond.yml's clientagent role gets `manual_dc_hash: 547155168` (Astron's `ClientAgent.cpp`
  reads it from the role config: `roles: - type: clientagent ... manual_dc_hash:`). Old clients
  keep connecting unchanged.
- New clients must **keep sending 547155168** in `CLIENT_HELLO`, never the 3-file hash.
  Python ports override the hello hash; generated-table ports keep `DC_HASH = 547155168`.

## Wire messages (Astron client protocol, little endian)

Client -> server: `CLIENT_OBJECT_SET_FIELD (120)`: `u32 doId=4721, u16 field, args`
- `setMyPlatform`: `u8 platform`
- `requestPlatforms`: `u16 byteLength` then `u32 avId` x n (n <= 50)

Server -> client: `CLIENT_OBJECT_SET_FIELD (120)`, `doId 4721, field 2272`:
`u16 bytes, u32 avId[]`, `u16 bytes, u8 platform[]` (same count; parallel arrays).

Astron forwards it because 4721 is listed in `uberdogs` (`Client::lookup_object` knows
uberdogs). `anonymous: false` means only a logged-in client may send to it.

## Server behaviour (`ttplatform/TTPlatformManagerUD.py` + `registry.py`)

- The sender's avId is `air.getMsgSender() & 0xFFFFFFFF` (client id `account << 32 | avId`).
  Messages without a chosen avatar (avId 0) are dropped silently.
- `setMyPlatform`: stores avId -> platform (UNKNOWN deletes). If the value changed, it pushes
  `setPlatforms([avId],[p])` to every client that has asked about this avId. This covers both
  "requested before it registered" and VR enter/leave, so clients never poll.
- `requestPlatforms`: dedupes the ids, drops 0 and our own id, caps at 50, records the requester
  as a watcher, and replies **to the requester only** with the ids it knows. Unknown ids get no
  reply until they register.
- Rate limits per client channel (token buckets): `setMyPlatform` burst 4 then 1 per 5 s;
  `requestPlatforms` burst 20 then 4/s. Excess is dropped without an eject.
- Logout: the first `setMyPlatform` installs a `CLIENTAGENT_ADD_POST_REMOVE` that sends
  `clearPlatform(avId)` to 4721 when the client disconnects. The entry is then dropped and the
  watchers are pushed UNKNOWN. As a backstop (toon switches clear post-removes), the tables are
  bounded: 50k entries, 128 watchers per toon, LRU eviction.
- A client sending `setPlatforms` or `clearPlatform` is ejected by Astron (118, not clsend).

## Running it

Bootstrap without editing upstream: `ud_start.py` replaces `toontown.uberdog.UDStart` as the
UD entry. It adds `ttplatform/` to `sys.path` and makes the UD read `otp.dc, toon.dc,
platform.dc`, asserting that the first two still hash to 547155168. It wraps the repository's
`createGlobals` so it generates `TTPlatformManager` at 4721, then runs the stock UDStart. The
AI is stock: it never touches 4721 and does not need platform.dc (a `TTPlatformManagerAI`
stub exists in case a port's AI loads the file).

```
# local private server (CA 7298, MD 7299, EL 7300)
SERVER_PORT=7298 SERVER_RUN=/tmp/pb-srv PLATFORM=1 tools/server/start.sh
#   (OTT_DIR=/path/open-toontown RES_DIR=/path/resources if this checkout's ref/ is empty)
SERVER_RUN=/tmp/pb-srv tools/server/stop.sh
```

`PLATFORM=1` runs `patch_astrond.py`, which is idempotent and fails rather than writing a
half-patched file. It appends platform.dc after toon.dc in `dc_files`, adds the uberdog
`{class: TTPlatformManager, id: 4721, anonymous: false}` and adds `manual_dc_hash: 547155168` to
the clientagent role.

### Tests

```
tools/server/.venv/bin/python -I tools/server/platform/test_platform.py    # unit: enum, should-show,
                                                   # batching, registry, DC numbering + hash
tools/server/.venv/bin/python -I tools/server/platform/test_e2e.py --port 7298   # live server
```

`test_e2e.py` connects two new clients (N3DS and MAC, then VR), the stock `smoke_test.py` as an
OLD client (2-file DC, hash 547155168, plays in zone 2000 alongside them), a pre-avatar client
and a cheating client. It checks the pending push, the reply, the VR push, that the old client
gets no badge and no traffic, the anti-cheat eject, and the UNKNOWN push on disconnect.

### VPS

`deploy_vps.sh plan | apply | rollback DIR | verify` (HOST=vps). `plan` is read-only.
`apply` does the following:
- discovers the tt-* units and the astrond.yml path
- refuses if a client connected in the last 15 min (unless `FORCE=1`)
- backs up astrond.yml, the dc-file dir, the unit files, the astrondb dir and accounts.json to
  `/root/tt-platform-backup-STAMP`
- installs to `/opt/tt-platform` and patches the yml
- adds a UD drop-in that swaps `UDStart` for `ud_start.py`
- restarts astrond, then the UD, then the AI, then the bridge
- verifies, and rolls back automatically on any failure

It has not been run yet (phase C).

## Client integration (every port)

Rules: badge only if `should_show(own, other, isLocal)` = `!isLocal && other != 0 && other != own`.

1. After our avatar enters the world, send `setMyPlatform(own)`. VR builds send it again on
   immersive enter/leave.
2. When another DistributedToon generates (or its nametag is created), queue its avId. Every
   ~250 ms, flush at most 50 queued ids as one `requestPlatforms`. Never re-request an id
   already sent: the server pushes registrations and changes.
3. On `setPlatforms`, cache avId -> platform for the session and refresh that toon's nametag.
4. Draw the badge left of the name, inside the 2D and 3D nametags (not the chat balloon). Scale
   it to the name text height and give it the tag's alpha/fade.
5. No manager (offline mode, old server) means nothing is sent and no badge is shown. No errors.

| port type | how |
|---|---|
| Python (native, android, iOS with Panda) | vendor `ttplatform/` (`TTPlatformManager.py`, `platforms.py`); add `dc-file platform.dc` **after** toon.dc (check the final read order: the 3-file read must put it last); `cr.platformManager = cr.generateGlobalObject(4721, 'TTPlatformManager')`; override the hello hash to 547155168 (`otp/login/LoginAstronAccount.py` sends `cr.hashVal`: set `cr.hashVal = 547155168` after `readDCFile`); update any self-test to check otp+toon == 547155168 and that platform.dc loads; hook `noteToon` in DistributedToon generate and `shouldShowBadge` in the nametag. |
| Generated tables (web, VR, 3DS) | run the DC generator over otp.dc, toon.dc, **platform.dc**; keep `DC_HASH = 547155168`; add a test that every existing class/field number is unchanged (class 398 / fields 2270-2273 are the new ones); whitelist only `setMyPlatform` / `requestPlatforms` as outgoing on 4721. |
| 3DS (done, AGENTS.md "phase B") | `tools/dcgen` takes the third file; 16x16 (or 24x24) RGBA4 sprite from `tools/platform-badges/atlas_16.png` in the existing UI atlas; fixed array cache (no per-frame allocation); badge drawn left of the name in `ui::Balloons`. |

Shared pure logic for Python ports: `ttplatform/platforms.py` (`clamp`, `from_name`,
`should_show`, `detect_native_platform`, `RequestBatcher`). Non-Python ports mirror it and copy
its unit cases from `test_platform.py`.
