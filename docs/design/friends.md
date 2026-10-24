# Friends, whispers and teleport-to-friend

Status: implemented in `source/net/client_friends.cpp` (+ hooks in `client.cpp`),
`source/ui/friends_panel.*` (+ hooks in `render/ui.*`, `main.cpp`). Verified on the host
against `tools/nettest/fake_server.py` only (see section 7); not yet run against the real
server in Azahar.

All upstream citations are paths under `ref/open-toontown/` (`etc/` = the `.dc` files).

## 0. Verdict (read this first)

**The stock Open Toontown server this client targets (sv1.0.47.38, `astron/config/astrond.yml`)
has no working friends back end.** The wire protocol and the client-side logic exist upstream,
but every server-side manager that would answer them is an empty stub:

| Piece | Upstream status | Evidence |
|---|---|---|
| `FriendManager` (friend requests) | AI class is a stub; **nothing instantiates it** | `otp/friends/FriendManagerAI.py` (5 lines, no methods); no `FriendManager` anywhere in `toontown/ai/ToontownAIRepository.py` or `toontown/uberdog/ToontownUDRepository.py` |
| `AvatarFriendsManager` (uberdog 4686) | stub | `otp/friends/AvatarFriendsManagerUD.py` (empty class); listed in `astron/config/astrond.yml` |
| `TTPlayerFriendsManager` (uberdog 4687) | stub | `toontown/friends/TTPlayerFriendsManagerUD.py` (empty class) |
| Friends list fetch (`CLIENT_GET_FRIEND_LIST`) | **not implemented for Astron** | `toontown/distributed/ToontownClientRepository.py:855-857`: `if __astron__: print('sendGetFriendsListRequest TODO')` |
| `CLIENT_FRIEND_ONLINE/OFFLINE` presence | legacy OTP client-agent messages; Astron's client agent does not send them | `ToontownClientRepository.py:552-559` handles them, but nothing on Astron produces them |
| Making/removing friendships on the AI | no code calls `extendFriendsList` / `d_setFriendsList` | `grep` over `toontown/` and `otp/`: only the setters in `DistributedToonAI.py:596-618` |
| `EstateManager.removeFriend` | declared (`etc/toon.dc:1152`), not implemented in `EstateManagerAI.py` | |
| `DistributedChatManager` (OTP_DO_ID 4681, `speedChatTo`, `whisperSCTo`...) | not an uberdog in `astrond.yml` | |

What **does** work with no manager at all, because it is peer-to-peer through Astron:

* **SpeedChat whispers** (`setWhisperSCFrom` etc.): a `clsend` field sent straight at the
  *other toon's* DistributedToon object.
* **teleportQuery / teleportResponse / teleportGiveup**: same mechanism.
* `friendsNotify` (a peer "I removed you" ping): same mechanism.
* **Our own `setFriendsList`**: a `required ownrecv db` field of our toon, so it arrives
  with the owner view if the DB ever holds friends (e.g. a patched or later server).

Design consequence: the client implements the full protocol faithfully and keeps it
dormant where the server lacks the counterpart:

* Whispers and teleport queries are fully functional against the stock server (to toons we
  can see).
* "Add Friend" probes for a `FriendManager` object. On the stock server none is ever
  generated, so the panel says "Friends aren't available on this server." and sends
  **nothing** (`FriendInviter.enterCheckAvailability` does the same:
  `'No FriendManager available.'` -> state `down`). With a server that provides one (the
  `tools/nettest` fake server does), the whole request/accept/decline flow runs.
* The friends list shows whatever `setFriendsList` delivers; it is empty on the stock server.

## 1. Fields (keywords from `etc/otp.dc`, `etc/toon.dc`)

`DistributedToon` inherits `DistributedPlayer` (`otp.dc:232`) and `DistributedAvatar`
(`otp.dc:219`). Keyword key: **C** = `clsend` (a client may send it, to any object it can see),
**O** = `ownsend`, **R** = `ownrecv` (delivered to the owning client), **B** = `broadcast`.

| Field | Signature | Keywords | Direction / use |
|---|---|---|---|
| `setFriendsList` (`otp.dc:245`) | `FriendEntry[]` = `{u32 friendId, u8 friendCode}[]` | required, R, db, airecv | server -> owner: part of our owner view; resent on change. `friendCode` bit 0 = `OTPGlobals.FriendChat` (open-chat friend). Cap `MaxFriends = 50` (`otp/otpbase/OTPGlobals.py:19`) |
| `friendsNotify` (`otp.dc:222`) | `i32 avId, i8 status` | R, airecv, **C** | peer -> peer. status 1 = "left your friends list", 2 = "now your True Friend" (`DistributedPlayer.friendsNotify`) |
| `setWhisperSCFrom` (`otp.dc:237`) | `u32 fromId, u16 msgIndex` | R, **C** | peer -> peer; `msgIndex` is the SpeedChat static-text index (same table as `setSC`) |
| `setWhisperSCCustomFrom` (`:238`) | `u32 fromId, u16 msgIndex` | R, **C** | custom phrase index (`SpeedChatCustomText`) |
| `setWhisperSCEmoteFrom` (`:239`) | `u32 fromId, u16 emoteId` | R, **C** | emote whisper |
| `setTalkWhisper` (`otp.dc:205`) | `u32 fromAV, u32 fromAC, string name, string chat, {u16,u16}[] mods, u8 flags` | R, **C** | typed (whitelist) whisper; we only *receive* it |
| `setSystemMessage` (`otp.dc:240`) | `u32 aboutId, string text` | R only | server -> owner, shown as a whisper popup; **not** `clsend` |
| `teleportQuery` (`toon.dc:513`) | `u32 requesterId` | R, **C** | requester -> target toon object |
| `teleportResponse` (`toon.dc:514`) | `u32 avId, i8 available, u32 shardId, u32 hoodId, u32 zoneId` | R, **C** | target -> requester toon object |
| `teleportGiveup` (`toon.dc:516`) | `u32 requesterId` | R, **C** | requester -> target ("you moved, I gave up") |
| `teleportResponseToAI` (`toon.dc:515`) | | O, airecv | party/estate only; unused |
| `setHp`, `setMaxHp` (`toon.dc:450-451`) | `i16` | required, **B**, R, db | so remote toons' laff **is** broadcast (the detail panel shows it) |

`FriendManager` (`otp.dc:308`, no required fields, `neverDisable` global object; upstream
`otp/friends/FriendManager.py`):

| Field | Signature | Keywords | Direction |
|---|---|---|---|
| `friendQuery` | `i32 inviteeId` | airecv, **C** | inviter -> manager |
| `cancelFriendQuery` | `i32 context` | airecv, **C** | inviter -> manager |
| `inviteeFriendConsidering` | `i8 yesNo, i32 context` | airecv, **C** | invitee -> manager |
| `inviteeFriendResponse` | `i8 yesNoMaybe, i32 context` | airecv, **C** | invitee -> manager |
| `inviteeAcknowledgeCancel` | `i32 context` | airecv, **C** | invitee -> manager |
| `friendConsidering` | `i8 yesNoAlready, i32 context` | (none: recv only) | manager -> inviter |
| `friendResponse` | `i8 yesNoMaybe, i32 context` | recv only | manager -> inviter |
| `inviteeFriendQuery` | `i32 inviterId, string inviterName, blob inviterDna, i32 context` | recv only | manager -> invitee |
| `inviteeCancelFriendQuery` | `i32 context` | recv only | manager -> invitee |

(`requestSecret`/`submitSecret` "friend secrets" are out of scope.) The `FriendManager`
object's doId, parent and zone are server defined; upstream's id constant is
`OTP_DO_ID_FRIEND_MANAGER = 4501` (`otp/distributed/OtpDoGlobals.py:3`). The client therefore
recognises it by **class** when any object of class `FriendManager` is generated, wherever.

## 2. Flows

### 2.1 Whisper (`DistributedPlayer.whisperSCTo`, `otp/avatar/DistributedPlayer.py:138-159`)

Sender: `sendUpdate('setWhisperSCFrom', [self.doId, msgIndex], sendToId)`, i.e. a
`CLIENT_OBJECT_SET_FIELD` whose doId is **the receiver's toon** and whose first argument is
**our** doId. Astron routes it to the receiver's owning client (`ownrecv`). No manager is
involved. The receiver (`DistributedToon.setWhisperSCFrom`, `toontown/toon/DistributedToon.py:534`)
looks the sender up (`identifyFriend`: friends map, then `doId2do`, then cache); unknown
senders are dropped, ignored senders are dropped, and when
`acceptingNonFriendWhispers` is false only friends get through (default true,
`LocalToon.py:167`). It then shows a whisper popup and logs it via the TalkAssistant.

Astron constraint (strings in the shipped `astron/darwin/astrond`: "Client tried to send
update to nonexistent object", "Client tried to manipulate unknown object"): the target
object must be one the client currently sees (or recently saw). Sending to an unseen doId
**ejects** us. So the 3DS client only whispers to toons in `toons_` (those in our interest).
That is why the Nearby list is the whisper entry point; whispering to an offline or
elsewhere-online friend is impossible on Astron without a relay (`TTSpeedchatRelay`, a stub).

### 2.2 Friend request (`FriendInviter`, `FriendInvitee`, `FriendManager`)

```
inviter                    FriendManager (AI)                    invitee
friendQuery(inviteeId) ->
                           <- friendConsidering(1, ctx)   (1 = asking; ctx names the request)
                                                    inviteeFriendQuery(inviterId, name, dna, ctx) ->
                                                    <- inviteeFriendConsidering(1, ctx)   (we are not busy)
                                                    ... dialog ...
                                                    <- inviteeFriendResponse(1 yes | 0 no | 3 full, ctx)
                           <- friendResponse(1 | 0 | 3, ctx)
(AI also d_setFriendsList on both toons -> setFriendsList)
cancel: cancelFriendQuery(ctx) -> inviteeCancelFriendQuery(ctx) -> inviteeAcknowledgeCancel(ctx)
```

`friendConsidering` codes (`FriendInviter.__friendConsidering`): 0 not available, 1 asking
(store `ctx`), 2 already friends, 3 self, 4 ignored, 6 not accepting new friends, 10 no,
13 other has too many friends. `friendResponse`: 1 yes, 0 no, 3 other too many, anything else
"maybe". The inviter first checks locally: not self, not already a friend, fewer than 50
friends, target visible (`enterCheck`/`enterCheckAvailability`).
Invitee (`FriendManager.inviteeFriendQuery`): reply `inviteeFriendConsidering(available, ctx)`
(0 if busy/no avatar, 4 ignored, 6 not accepting) and show the dialog; if it is closed without a
button the cleanup sends `inviteeFriendResponse(2, ctx)` ("maybe"); at 50 friends it answers 3.

### 2.3 Friends list, names, DNA, online state

Upstream (non-Astron) fetches the list with the legacy `CLIENT_GET_FRIEND_LIST` datagram and
gets `{doId, name, dnaBlob, petId}` per friend (`handleGetFriendsList`,
`ToontownClientRepository.py:896-927`), with `CLIENT_FRIEND_ONLINE/OFFLINE` datagrams for
presence (`:956-978`). **None of that exists on Astron** (section 0). What a 3DS client can
know:

* **ids + codes**: `setFriendsList` (owner view).
* **name + DNA**: only from the friend's own DistributedToon, when it is in our interest (it
  broadcasts `setName` and `setDNAString`). The client caches them per session by doId; a
  friend we have not seen this session shows as "Toon <id>" with no preview.
* **online**: unknowable. The panel shows **Here** (visible now, so definitely online) or
  **Away** (unknown). It never claims "offline".
* Removing a friend (`ToontownClientRepository.removeFriend`) needs the legacy
  `CLIENT_REMOVE_FRIEND` and the unimplemented `EstateManager.removeFriend`; not offered.

### 2.4 Teleport to a friend (`ToonTeleportPanel`, `Place.handleTeleportQuery`)

1. Requester: `teleportQuery(ourId)` sent at the target toon object (`ToonTeleportPanel.py:147-151`).
2. Target answers by sending at the **requester's** toon object
   `teleportResponse(targetId, available, shardId, hoodId, zoneId)` with `available` 1 plus its
   shard, canonical hood and zone (`Place.py:158-165`), 0 = busy/unavailable, 2 = ignored.
   It also shows "<name> is coming to visit you." as a system whisper.
3. Requester: available 0 -> "busy"; 2 -> ignored; other shard -> ask first; same shard ->
   `place.requestTeleport(hoodId, zoneId, shardId, avId)`, then on arrival `gotoNode(avatar)` and
   `b_teleportGreeting(avId)`. If the friend is gone: `teleportGiveup(ourId)`.

Same Astron visibility rule as whispers: the query can only reach a toon we can see.

## 3. The 3DS implementation

### 3.1 Net layer (`source/net/client.h` "friends" block, `client_friends.cpp`)

State lives in `Client::friends_` (fixed arrays, no allocation). Additive hooks in
`client.cpp` (one line each): `handleEnter` (recognise `FriendManager`), `handleSetField`
(manager fields), `applyOwnField` (our friends/whisper/teleport fields), `handleLeaving`,
`update`, `resetWorld`; plus `RemoteToon::hp/maxHp` captured in `applyToonField`.
Events use their own queue (`pollFriendEvent`), not `EventType`, to avoid touching the shared
enum. Everything sent goes through whitelisted macros (`static_assert` clsend):

| API | Sends | Preconditions |
|---|---|---|
| `sendWhisperSC(avId, msgIndex)` | `setWhisperSCFrom(ourId, idx)` at `avId` | in zone; `avId` is a visible toon |
| `requestFriend(avId)` | `FriendManager.friendQuery(avId)` | manager known; not self/friend; < 50 friends; toon visible |
| `cancelFriendRequest()` | `cancelFriendQuery(ctx)` | request in flight |
| `answerInvite(bool)` | `inviteeFriendResponse(1/0, ctx)` | invite pending |
| `sendTeleportQuery(avId)` | `teleportQuery(ourId)` at `avId` | visible toon |
| `sendTeleportGiveup(avId)` | `teleportGiveup(ourId)` at `avId` | visible toon |
| (automatic) | `inviteeFriendConsidering`, `inviteeAcknowledgeCancel`, and a `teleportResponse` to any `teleportQuery` we receive | |

Receiving: whispers (SC, custom, emote, typed) go to a 16-entry log; `friendConsidering` /
`friendResponse` drive `FriendRequest::state`; `inviteeFriendQuery` fills `FriendInvite`
(one at a time: a second one is answered "busy" 0); `teleportResponse` fills `TeleportResult`.

Our answer to an incoming `teleportQuery`: available 1 with `(shardId, hood, zone)` when
`InZone`; 0 otherwise. `hood = zone - zone % 1000` for the exterior zone range
(`ZoneUtil.getCanonicalHoodId`); interiors report the hood of their street.

### 3.2 UI (`source/ui/friends_panel.*`)

The tab bar is full (6 tabs), so there is no seventh tab. The least intrusive entry: a
**Friends** button on the **Toon** tab (the tab already means "me and my toon"). It opens the
`FriendsPanel` in place of the Toon tab body; the tab bar stays, and **B** or the panel's
Back button returns. An incoming friend request or whisper switches to the Toon tab and
surfaces the relevant panel page so it cannot be missed (the request needs an answer).

Pages: **Nearby** (remote toons, nearest first, name + here-distance; tap -> Detail),
**Detail** (name, laff `hp/maxHp`, body/head color swatches from the DNA, **Whisper**,
**Add Friend**, **Visit** when they are a friend), **Whisper** (the reused `ChatMenu`
SpeedChat tree; picking a phrase sends it), **Friends** (list with Here/Away, tap ->
Detail; status line for a request in flight), **Log** (received/sent whispers), and an
**Invite** popup page (Accept / Decline). The top screen shows a toast for each incoming
whisper and request (`main.cpp` hooks, using the existing toast).

Visit goes through the existing travel path: a same-shard `teleportResponse` with an
installed zone sets `uiState.teleportRequest` (the Map-tab teleport), so the fade, zone
load and `Client::changeZone` all reuse `main.cpp`'s code. We arrive at the zone's default
arrival point, not next to the friend (`gotoNode` is not implemented).

## 4. Out of scope / gaps

* A real friends back end. A minimal server patch would be: instantiate a `FriendManagerAI`
  that implements the 9 fields above and calls `extendFriendsList` + `d_setFriendsList` on
  both toons and `d_friendsNotify` on removal. Not done: the server stays unmodified.
* Persisting the friend name/DNA cache across sessions (the SD card would be the place).
* Whispering/teleporting to friends who are not in our interest (needs a relay).
* True Friends / secrets, ignore list, `setWhisperSCToontaskFrom`, sleep auto-reply.
* Joining a friend on another shard (the 3DS client has one district; the panel says so).

## 5. Verification

Host: `tools/nettest/run_tests.sh` runs a "friends" scenario against the fake server
(`--friends`): whisper to Walker (answered with a whisper back), a friend request that is
accepted (`setFriendsList` arrives), an incoming invite that the test accepts, a teleport
query answered by the server, and a teleportQuery to the client that it must answer.
In Azahar (not run by this change): see `docs/test-log.md` for the checklist.
