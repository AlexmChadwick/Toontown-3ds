# Gag shop and the jellybean economy

Spec for buying gags from a Gag Shop clerk (`DistributedNPCClerk`, for example in the
interior zones `…519`). All citations are paths under `ref/open-toontown/`. Client code:
`source/net/client.*` (the "shop" section), `source/ui/shop_rules.*` (pure rules),
`source/ui/shop_panel.*` (bottom-screen panel), hooks in `source/render/ui.*`,
`source/world/zone_actors.*` (`touchingNpc`) and `source/main.cpp`.

## 1. Cast and DC fields

| Class (id) | Field | Keywords | Signature |
|---|---|---|---|
| `DistributedNPCToonBase` (227) | `avatarEnter` (1343) | clsend airecv | `()` |
| `DistributedNPCToonBase` | `freeAvatar` (1344) | (AI to one client) | `()` |
| `DistributedNPCClerk` (232) | `setMovie` (1354) | broadcast ram | `u8 mode, u32 npcId, u32 avId, i16 timestamp` |
| `DistributedNPCClerk` | `setInventory` (1355) | clsend airecv | `blob inventory, i16 money, u8 done` |
| `DistributedToon` (65, our own object) | `setInventory`, `setMoney`, `setMaxCarry`, `setExperience`, `setTrackAccess` | owner view | already parsed into `OwnStats` |

All IDs come from `source/net/dc_generated.h` (generated; the hash stays 547155168).
`dcgen` `WANTED` already listed every one of these fields, so no regeneration was needed.
`Client` pins the layouts with `TT_SIG` static asserts and sends only through the
`TT_NET_UPDATE` whitelist macro (`clsend` required).

## 2. The flow (DistributedNPCClerk.py / DistributedNPCClerkAI.py)

1. **Trigger.** `DistributedNPCToonBase.__initCollisions`: a non-tangible `CollisionTube`
   from `(0,1,0)` to `(0,1,5)` in the NPC's frame, radius 3.25 (`getCollSphereRadius`). The
   toon's event sphere entering it calls `DistributedNPCClerk.handleCollisionSphereEnter`:
   `place.fsm.request('purchase')` then `sendUpdate('avatarEnter', [])`. The event fires once
   per contact, so you must leave the tube and re-enter to shop again. (The upstream
   `allowedToEnter` TeaserPanel gate is for unpaid accounts; ignored, our server is full access.)
   Client: `ZoneActors::touchingNpc` (tube vs. GravityWalker event sphere) + `lastTouchedClerk`
   in `main.cpp`, then `Client::sendClerkAvatarEnter`.
2. **AI `avatarEnter`.** If the toon is unknown: warn and return. Else it listens for the
   avatar's exit. If the clerk `isBusy()` (`busy > 0`): `freeAvatar(avId)` and stop (the client
   sets its place back to walk; we did not stop walking until the movie, so nothing to undo).
   If the toon has money (`av.getMoney()` non-zero): `sendStartMovie`; otherwise
   `sendNoMoneyMovie`.
3. **`sendStartMovie`.** `busy = avId`; broadcast `setMovie(PURCHASE_MOVIE_START = 1, npcId,
   avId, networkTime)`; `doMethodLater(CLERK_COUNTDOWN_TIME = 120 s, sendTimeoutMovie)`.
4. **Client `setMovie` START** (`NPCToons.py`: CLEAR 0, START 1, COMPLETE 2, NO_MONEY 3, TIMEOUT 8;
   START_BROWSE 9 and START_NOROOM 10 exist but this AI never sends them). `remain =
   CLERK_COUNTDOWN_TIME - localElapsedTime(timestamp)`. For the local toon: face the clerk,
   camera lerp, clerk says "Choose what you want to buy." and 1 s later the `ClerkPurchase` GUI
   opens with a countdown of `remain`. We open the panel immediately on START
   (`ShopPanel::onMovie`) and compute the deadline from the timestamp
   (`Client::handleClerkMovie`, never later than now + 120 s).
5. **Buying.** `PurchaseBase.handlePurchase(track, level)`: ignored if `money <= 0`; else
   `inventory.addItem(track, level)`; on success `money -= 1` and the client sends
   `setInventory(blob, money, 0)` (the AI only validates and updates its own copy then, no
   broadcast, no completion). "Done Shopping" (`__handleBackToPlayground`) or the timer
   expiring (`__timerExpired`) sends `setInventory(blob, money, 1)`.
   **Deliberate difference:** we do not send the per-purchase `done = 0` updates; we send
   one `done = 1` at the end. Fewer messages, nothing lost, and a client that vanishes
   mid-shop loses nothing server-side. If the AI times out first, a late `done = 1` is
   silently ignored (`busy != avId`), so the panel auto-submits 2 s before the deadline.
6. **AI `setInventory(blob, newMoney, done)`.**
   * `busy != avId`: if `busy != 0`, log `suspicious`; return. Nothing else happens.
   * Else `newInventory = av.inventory.makeFromNetString(blob)`, `currentMoney =
     av.getMoney()`, and `validatePurchase(newInventory, currentMoney, newMoney)` (section 4).
   * **Valid:** `av.setMoney(newMoney)`; if `done`: `d_setInventory(...)` and `d_setMoney(...)`
     (our `OwnStats` updates from those two owner-view fields; the `Stats` event fires).
   * **Invalid:** `writeServerEvent('suspicious', ...)`, warning, then it resends the
     authoritative `setInventory` and `setMoney` to us. No eject (a track-access violation
     only bans when `want-ban-gagtrack` is configured).
   * If `timedOut`: return. If `done`: remove the timeout task, `completePurchase`:
     broadcast `setMovie(COMPLETE = 2, ...)` then `CLEAR (0, npcId, avId 0)`.
7. **Client `setMovie` COMPLETE:** clerk says "See you later!", `resetClerk`, `freeAvatar`.
   **NO_MONEY:** clerk says "You need to ride the Trolley to get some jellybeans.", then
   CLEAR (the AI sends both back to back). **TIMEOUT:** the purchase GUI is closed (a done
   handler sends the current inventory), clerk says "Need more time to think?".
   We show the clerk lines as a top-screen toast (`ShopPanel::takeNotice`).
8. **Unexpected exit** (our avatar leaves): the AI's exit handler calls `sendTimeoutMovie`.
   `Client` drops the session on reconnect (`resetWorld`, `dropZoneObjects`) and the panel closes
   when `shopOpenFor(clerk)` becomes false.

## 3. Economy rules

* **Cost.** Every gag costs exactly **1 jellybean**, whatever its track or level. There is
  no `AvPropCost`-style table anywhere in `ToontownBattleGlobals.py`, `PurchaseManager.py`
  or `InventoryBase.py`; `PurchaseBase.handlePurchase` hard-codes `money - 1`. (The AI's
  check is `newItems - oldItems <= currentMoney - newMoney`, so the toon may even spend more
  than it needs. We never do.) `PurchaseManager.py` is the post-minigame screen; it does not
  matter here.
* **Selling.** The store has no sell-back. Upstream's separate `storePurchaseDelete` mode lets
  you throw gags away with no refund. We only let `-` undo gags bought *in this visit*, with a
  refund of the jellybean (the net effect on the wire is just a smaller purchase). Throwing away
  owned gags is not offered (gap).
* **Which levels are buyable.** `Experience.getExpLevel(track)` = highest level whose
  threshold in `ToontownBattleGlobals.Levels[track]` is reached by the track experience
  (`OwnStats::experience`). `addItem` needs `getExpLevel(track) >= level` and
  `hasTrackAccess(track)` (`OwnStats::trackAccess`; a new toon has only Throw and Squirt, the
  `setTrackAccess` default `[0,0,0,0,1,1,0]`). Thresholds (`gag_data.cpp` `kLevelExp`):
  Toon-Up 0/20/200/800/2000/6000, Trap and Lure 0/20/100/800/2000/6000, Sound
  0/40/200/1000/2500/7500, Throw and Squirt 0/10/50/400/2000/6000, Drop 0/20/100/500/2000/6000.
  Level 7 (the uber gag, index 6) is never buyable: `validateItemsBasedOnExp` rejects any
  increase of an uber count.
* **Per-gag carry limit.** `InventoryBase.getMax(track, level) =
  CarryLimits[track][getExpLevel(track)][level]` (`kCarry`). Example (Throw): with
  only level 1 unlocked 10 of the first gag; with 4 levels unlocked 20/15/10/5.
* **Total carry limit.** `toon.getMaxCarry()` (`setMaxCarry`, `OwnStats::maxCarry`; 20 for a
  new toon, up to 80 from tasks). Only levels 0-5 count (`LAST_REGULAR_GAG_LEVEL = 5`).
  Money limit (`maxMoney`) is irrelevant: jellybeans only go down here.
* **`addItem` result codes** (`PurchaseBase` messages): `-2` "Sorry, you have too many
  props", `-1` "Sorry, you have enough X already", `0` "You do not have enough skill for that
  yet". Order of checks: skill and track access, then per-gag max, then total carry.

## 4. What the AI validates (`InventoryBase.validatePurchase`)

In this order, rejecting at the first failure (`shop::validatePurchase`, exact mirror,
for a full-access toon, as a dev server grants):

1. `newMoney > currentMoney` ("lied about money").
2. `newTotal > oldTotal + currentMoney` ("overspent"). Totals are `countPropsInList`: levels 0-5.
3. `newTotal - oldTotal > currentMoney - newMoney` ("too many items for money spent").
4. `newTotal > maxCarry`.
5. `validateItemsBasedOnExp`: for **every** (track, level), not only changed ones: count `>
   getMax`, or count `> 0` without track access, or an uber count above the old one. (So a
   toon that already holds more than a limit allows cannot buy anything until it fixes that.)
6. `validateItemsBasedOnAccess`: unpaid accounts only; not mirrored.

Accepting calls `updateInventory(newInventory)` on the AI's copy (even for `done = 0`).
The blob is `InventoryBase.makeNetString`: one `uint8` per (track, level) in track-major
order, 7 x 7 = 49 bytes, sent as a DC `blob` (u16 length prefix, `Client::sendClerkSetInventory`).
Short blobs are zero padded by the AI (`makeFromNetString`); we always send all 49 bytes.

## 5. Client design

* **Net.** `Client::handleClerkMovie` (from `handleSetField`, field `DistributedNPCClerk.setMovie`)
  keeps `ShopMovie{clerkDoId, mode, npcId, avId, deadlineMs, seq}` and raises `EventType::ShopMovie`.
  Movies about other toons are ignored, except CLEAR for our clerk. `sendClerkAvatarEnter` only
  targets known NPCs of class 232; `sendClerkSetInventory` only while a START for us is open on that
  clerk (`shopOpenFor`).
* **Rules (`ui/shop_rules.*`).** `canBuy`, `Cart::add/remove`, `validatePurchase`, `encodeInventory`:
  host-testable, no libctru. The panel runs `validatePurchase` on its cart before sending and falls
  back to Cancel (original inventory and money) if it ever failed.
* **Panel (`ui/shop_panel.*`).** Replaces the tab UI while active and swallows all buttons
  (`Ui::handleInput` returns them; `main.cpp` also zeroes the circle pad and A/B hold so the toon
  stands still). Layout: header (title, beans, carry x/y, countdown), a 7 x 6 grid of
  `have/max` per gag (locked cells show "-"; cells bought this visit are gold), an info line
  (selected gag, or the refusal message for 2.5 s), and buttons `-`, `+`, `Done`, `Cancel`.
  Controls: touch a cell to select it (tap the selected cell again, or `+`, to buy); D-pad moves the
  cursor, A = buy, X = take back, START = Done, B = Cancel. Text is cached and re-parsed only when
  the cart changes or the countdown second ticks.
* **Done** sends `setInventory(cartBlob, cartMoney, 1)`. **Cancel** sends the *original* blob and
  money with `done = 1` (the AI accepts an unchanged inventory and ends the movie right away
  instead of waiting 120 s).

## 6. Tests

`tools/shop_test/run_test.py` builds `shop_test.cpp` + `shop_rules.cpp` and compares, over
tens of thousands of random cases, against the **real** upstream `InventoryBase` (a fake
toon plugged in; `panda3d.otp` stubbed): the 49-byte blob, `validatePurchase` accept/reject,
the `addItem` result code per purchase attempt, and whole buy/sell visits. Run:
`.venv/bin/python -I tools/shop_test/run_test.py --count 30000 --seed N`.

## 7. Not done / gaps

* No door-to-counter camera lerp, clerk head-turn or clerk speech bubble on the NPC (toast only).
* Throwing owned gags away (`storePurchaseDelete`) and the unpaid/velvet-rope handling.
* The clerk chat toast is top-screen only and uses the existing 3 s toast slot.
* Not run in Azahar yet. Verify: walk into the clerk in a Gag Shop interior (for example zone 2519 in
  TTC) with jellybeans; the bottom screen switches to the shop; buy a gag; Done; the Gags tab and laff/bean HUD
  update from the AI's resend. Try zero beans (toast), Cancel, waiting out the timer, and the carry limit.
