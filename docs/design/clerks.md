# Service clerks

Native protocol/UI implementation for the pinned, unmodified sv1.0.47.38 AI.
This document distinguishes intended upstream exchanges from services actually
implemented by this pin. No AI patch enables purchases, pets, fish or parties.

## Sources and transport

Read-only specifications under `ref/ref/open-toontown/`:

- `toontown/toon/DistributedNPC{Petclerk,KartClerk,Fisherman,PartyPerson}{,AI}.py`
- `toontown/toon/NPCToons.py` (movie constants, NPC IDs/zones)
- `toontown/pets/{PetUtil,PetDNA,PetTraits,PetConstants,PetNameGenerator,PetshopGUI,PetManagerAI}.py`
- `toontown/racing/{KartDNA,KartShopGlobals,KartShopGui,Kart}.py`
- `toontown/fishing/{FishGlobals,FishTank,FishSellGUI}.py`
- `toontown/uberdog/DistributedPartyManagerAI.py`,
  `toontown/parties/PartyGlobals.py`, `toontown/ai/ToontownAIRepository.py`
- `etc/toon.dc` and resource DNA/anchored NPC placements.

Every clerk inherits `avatarEnter()` and `freeAvatar()` from
DistributedNPCToonBase. A real anchored NPC must exist in the active zone. Enter
sends one `avatarEnter`; the AI checks that the sender's avatar exists and the
clerk is not busy. A busy clerk sends `freeAvatar`, with no START or transaction.
The AI watches unexpected avatar exit and clears its reservation on exit.

All four override `setMovie(u8 mode,u32 npcId,u32 avId,u32[] extraArgs,i16 time)`.
The time is network centiseconds, not a client deadline. Most extras are empty;
exceptions are fishing trophy `[collectedSpecies,totalSpecies]` and party COMPLETE
`[partyHood,allocatedZone]`. CLEAR has avatar zero. Broadcast movies for somebody
else never open our panel. A bounded FIFO preserves a terminal movie immediately
followed by CLEAR in the same network tick. Extras are bounded to eight u32s,
seeds to six; incomplete/malformed arrays do not publish partial state.

DC IDs are generated, hash **547155168**. Every outgoing action uses
`TT_NET_UPDATE` with a compile-time clsend check, live NPC class membership,
InZone, a local-avatar START, and a nonexpired session. No client action grants
money, tickets, ownership, fish or a pet. Owner resends are authoritative.
Pending enter retries are blocked for five seconds. Owner state survives a zone
change; sessions and queued movies do not. Disconnect resets both.

## Movie tables and clocks

SELL numbering is different from PARTY numbering. In particular SELL_NOFISH is
**3**, SELL_TROPHY is **4**; PARTY_ONLYPAID is 5 and PARTY_MINCOST is 7.

| SELL mode | Value | Meaning / client response |
|---|---:|---|
| CLEAR | 0 | Release interaction; retain any terminal result notice |
| START | 1 | Open the appropriate panel, permit guarded answers |
| COMPLETE | 2 | Successful fish sale or kart checkout; no further answer |
| NOFISH | 3 | Empty tank / declined fisherman sale; no further answer |
| TROPHY | 4 | Fishing trophy, extras `[collected,total]`; no answer |
| TIMEOUT | 8 | Session expired; no further answer |
| PETRETURNED | 9 | Return confirmation after transactionDone |
| PETADOPTED | 10 | Adoption confirmation after transactionDone |
| PETCANCELED | 11 | Pet visit ended without adopting/returning |

| PARTY mode | Value | Meaning / client response |
|---|---:|---|
| CLEAR | 0 | End reservation |
| START | 1 | Native client permits only `answer(0)` |
| COMPLETE | 2 | Extras `[hood,zone]`; unsupported planner is not entered |
| ALREADYHOSTING | 3 | Cannot plan another party |
| MAYBENEXTTIME | 4 | `answer(0)` result |
| ONLYPAID | 5 | Paid account required |
| COMINGSOON | 6 | Parties unavailable (the pinned stock response) |
| MINCOST | 7 | Not enough total money |
| TIMEOUT | 8 | Session expired |

PetConstants.PETCLERK_TIMER and KartShopGlobals.KARTCLERK_TIMER are **180 s**.
FishermanAI and PartyPersonAI schedule **30 s** after START. The upstream Python
clients display NPCToons.CLERK_COUNTDOWN_TIME (**120 s**) regardless of those AI
limits. Native deadlines follow the actual AI windows and subtract synchronized
movie age; the panel attempts the valid cancel/done action two seconds before
expiry. AI timeout sends TIMEOUT then CLEAR. Terminal notices persist for five
seconds, with B/A to dismiss. HOME, disconnect, NPC removal or zone change closes
the interaction rather than leaving valid sends available.

## Pet shop

1. `avatarEnter` requests `petMgr.getAvailablePets(3,2)`. AI multiplies the list by
   two (PetGenders), sorts it, sends `setPetSeeds(u32[])`, then START.
2. Sell a nonempty tank: `fishSold()` credits through fishManager and sends
   COMPLETE or TROPHY, then CLEAR. **Do not send transactionDone afterward**;
   fishSold itself clears the busy avatar.
3. Adopt: select one of three sorted seed pairs and its gender, a valid pet name,
   inspect body/pattern, colour and exact cost, then confirm. Send
   `petAdopted(u8 index,u32 nameIndex)` followed by `transactionDone()`.
4. Return: explicit destructive-return confirmation, then `petReturned()` and
   `transactionDone()`.
5. Cancel: `transactionDone()` without an action yields PETCANCELED then CLEAR.

AI adoption checks busy sender, seed index, cost versus wallet+bank, and name
index `[0,2713)`. It deletes an existing pet before its name-index check, so native
validation and replacement confirmation occur before any send. The manager
creates the pet with gender `index % 2` and canonical safezone. Payment consumes
bank first, then wallet. `transactionDone` selects PETADOPTED/PETRETURNED based on
the recorded action, then clears. Return calls deleteToonsPet and offers no
refund. Native prevents a second action during the same adoption/return visit.

### Exact generation

`clerk_logic.cpp` implements CPython Random's integer seeding, MT getrandbits,
randbelow, 53-bit random(), and cached Box-Muller gauss, using the existing
PyRandom state. PetUtil saves/restores the DNA RNG around randomName; names use a
separate seed `seed + safezone`. DNA has nine fields (head, ears, nose, tail,
body, colour, colour scale, eyes, generated gender). Weighted body draws preserve
Python dict insertion order and weightedRand's subtraction order. Then traitSeed
is `int(random()*0x7fffffff)`. Cost regenerates all 13 truncated/quantized traits;
surprise contributes no worth although the denominator remains 13. There is no
simplified rarity approximation.

| Safezone | Cost range |
|---:|---:|
| TTC 2000 | 100–500 |
| DD 1000 | 600–1700 |
| DG 5000 | 1000–2500 |
| MM 4000 | 1500–3000 |
| BR 3000 | 2500–4000 |
| DDL 9000 | 3000–5000 |

`tools/gen_clerk_tables.py` generates names, palette, body probabilities, prices
and fish rarities from pinned modules. The name dictionary has 2,698 entries in
IDs 0–2712 with holes. Source getName falls back to name zero for holes; native
matches that. Suggested names reproduce PetUtil, including its generated-gender
convention; final pet gender is the server's index parity. The name picker permits
all valid upstream IDs. Colour text includes PetDNA.ColorScales.

### What this pin actually supports

PetManagerAI.getAvailablePets returns **[]** and has no createNewPetFromSeed or
deleteToonsPet methods. Consequently stock sends empty setPetSeeds then START,
with no three offers and no functional adoption or return service. Native shows
"No Doodles offered by this server", allows cancel, and blocks adoption/return.
It does not fabricate offers or call the absent manager methods. Upstream's GUI
itself waits indefinitely on an empty list; native cancellation is intentional.

Owner `DistributedToon.setPetId` updates the Toon tab immediately. If the pet DO
is present, its authoritative name is shown; otherwise "Owned (at estate)".
Existing estate Doodle rendering now applies actual PetDNA body-colour palette
and colour scale. The existing generic Doodle geometry is retained; no claim is
made that all head/ear/tail/pattern variants were baked by this clerk task.

## Kart shop

`avatarEnter` → START. `buyKart(u8 body)` and `buyAccessory(u8 id)` can be sent
while START remains open; there is no per-purchase COMPLETE. `transactionDone()`
produces COMPLETE then CLEAR. Busy ownership and enough tickets are checked by
AI. The native client also applies the GUI's catalogue/ownership rules.

| Body | ID | Tickets |
|---|---:|---:|
| Cruiser | 0 | 100 |
| Roadster | 1 | 7500 |
| Toon Utility Vehicle | 2 | 2500 |

The generated table preserves exact localized names (the table is authoritative
if capitalization differs here). There are 92 accessory IDs:

| IDs | KartDNA field index |
|---|---:|
| Engine 0–10 | 3 |
| Spoiler 11–18 | 4 |
| Front wheel well 19–26 | 5 |
| Back wheel well 27–39 | 6 |
| Rims 40–54 | 7 |
| Decals 55–64 | 8 |
| Colours 65–91 | 1 (body colour) |

Field 2 is accessory colour; both colour slots are read from owner DNA. The
catalogue, costs and all RGBA values are generated from KartDNA; no synthetic
prices. A current body is not offered for repurchase. Accessories require a body,
space in the 16-item trunk, sufficient tickets and absence from owned IDs. Rim
40 is the default, omitted by getAccessoryDictFromOwned. AI also checks busy
sender, trunk capacity, valid catalogue ID and funds; unlike the GUI, some of its
checks do not exclude duplicate/default purchases, so native uses stricter GUI
rules. Accessories are auto-equipped by the AI's updateKartDNAField.

Each purchase waits for owner resends before another purchase is enabled. No
optimistic ticket decrement: setTickets supplies the new count, body and accessory
fields supply actual DNA, fixed int8[16] setKartAccessoriesOwned supplies trunk
ownership (-1 padding is ignored). A missing response leaves the purchase pending
until exit/timeout, without duplicate sends. The stock kart AI implements these
purchases; ticket availability depends on the account.

### 3D preview assets

The preview composes the existing upstream kart model/attachment transforms,
without changing racing code. The offline baker reads the pinned Kart methods and
exports each LOD1 body plus every non-colour component, with separate tint slots.
198 TTM files plus manifest are committed as an SD kit under:

`sdmc-out/3ds/toontown3ds/data/clerks/karts/`

Copy that directory to `sdmc:/3ds/toontown3ds/data/clerks/karts/`. Names are
`kart_<0..2>_body.ttm` and `kart_<body>_accessory_<0..64>.ttm`. Colours need no extra
model. The panel previews selected body/accessory/paint; B backs out without a
purchase. Missing assets display an explicit installation hint; transactions
continue to use the server. Components stream serially through AssetLoader and
publish/release only after the GPU fence. At most seven models are retained.

| Body | Max triangles | Max batches | Geometry bytes | Texture bytes |
|---:|---:|---:|---:|---:|
| 0 | 1231 | 21 | 25018 | 223648 |
| 1 | 1161 | 21 | 24214 | 252288 |
| 2 | 1141 | 24 | 24526 | 207264 |

Each column independently sums the worst component in every category. Header
preflight and baker enforce **512 KiB geometry / 1 MiB textures**, including all
seven component models. These sit within the shared zone linear pools; the
existing global loader budget still applies. CPU metadata and cached panel text
use regular heap, not per-frame allocations. ARM-checked fixed protocol state is
56 bytes/movie, 100/session, 36/profile and 896/FIFO; counters and holiday state
bring the added Client fields to approximately 1,048 bytes. Worst resident kart
texture descriptor count is 16. The final ELF text/data/bss totals 2,599,876 bytes
(1,641,552 / 53,764 / 904,560), below the 4 MiB code/static budget. See clerk_karts.json and
clerk_reproducibility.json for all hashes, byte counts and per-model costs.

## Fisherman and fish selling

AI computes fishTank.getTotalValue on entry. Empty tank → NOFISH(3) → CLEAR,
without START or a reply. Positive tank → START; sell `completeSale(1)`, decline
`completeSale(0)`. Sale invokes fishManager.creditFishTank, then COMPLETE(2) or
TROPHY(4,[collectionCount,70]), then CLEAR. Decline sends NOFISH then CLEAR.
The AI checks the same busy avatar before accepting the answer.

The base per-fish quote is
`ceil(15*((0.2*rarity)^1.5 + (0.003125*weight)^1.1))`, summed over the tank. Weights
are the upstream integer net units. New species arrive through the existing
setFishCollection owner update and book display; this pin has no separate
"new species" movie number. Trophy counts are rendered from the two movie args.
Fish/money changes are accepted only from owner resends.

FishGlobals doubles value on Jellybean Fishing holidays 98 and 114. Native reads
NewsManager.setHolidayIdList on generate and update, applies that exact multiplier,
and refreshes the quote. The bounded array publishes only after complete decode;
removing the manager resets the multiplier. Actual credits always come from AI,
never this quote.

**Pinned limitation:** ToontownAIRepository does not create fishManager. Both
positive clerk-sale paths call that missing attribute. Stock fishing also spawns
no usable spots and practice catches do not populate the AI tank. Thus the normal
stock fisherman path is empty-tank NOFISH; successful positive sales are tested
with explicit fixtures, not claimed working on the unmodified pin. Pet empty-tank
sales are disabled rather than invoking the missing manager needlessly. This task
does not add an AI fish manager or fabricate earned jellybeans.

## Party planner

DistributedPartyManagerAI.canBuyParties returns False. Normal stock entry always
sends COMINGSOON(6), then CLEAR. The panel displays "Parties are not available
yet" and sends no answer after this terminal movie.

The dormant upstream path first checks totalMoney against MinimumPartyCost and
canPlanParty (hosted-party limits), then START with 30 s timeout. answer(0) yields
MAYBENEXTTIME then CLEAR. answer(1) checks AccessFull; failure gives ONLYPAID,
success allocates a zone and sends COMPLETE `[PartyHood,zone]`, then CLEAR. Native
only permits decline if an alternate server sends START; it cannot send answer(1)
or enter a party planner. Other terminal modes have explanatory notices. No party
system or invented "coming soon" gameplay is implemented.

## Controls and diagnostics

D-pad up/down or touch Prev/Next navigates; A/Choose selects or confirms; B/Back
backs out, then closes a visit. Pet Y switches gender; left/right selects a name.
Adoption replacement, return and kart purchases each have explicit confirmation.
Movement and jump are frozen while the bottom panel owns input; busy state,
countdown, wallet+bank, tickets and fish totals are visible.

To run just these steps with the existing world autotest, write `only clerk:`
to `sdmc:/3ds/toontown3ds/autotest`. Ensure `autotest_fe` is absent, because its
presence selects the separate front-end script. The filter selects all five labels.

Autotest steps locate the actual network NPC and DNA anchor, request the normal
zone through the existing teleport request, walk away briefly to release any
previous contact, then walk toward the clerk. The usual contact handler sends
avatarEnter. The diagnostic calls the same guarded actions used by the panel and
never creates fake clerk objects or grants currency.

| Clerk | Zone | Real NPC IDs and anchors | Autotest label |
|---|---:|---|---|
| TTC pet shop | 2522 | 2013–2015, npc_origin_0 / 1 / 2 | clerk: pet shop; clerk: adopt |
| Speedway kart shop | 8501 | 8001–8004, npc_origin_0 / 1 / 2 / 3 | clerk: kart shop |
| TTC fisherman | 2000 | 2012, npc_fisherman_origin_0 | clerk: fisherman |
| TTC party gate | 2000 | 2016/2017, party_person_left / right | clerk: party planner |

Every send logs `clerks: send <field> ...`; every received movie logs kind (1 pet,
2 kart, 3 fisherman, 4 party), clerk doId, numeric mode, npcId, avId, arg count,
each arg and timestamp. setPetSeeds logs count and each seed. Owner pet/DNA/trunk
resends are logged. DO IDs vary, NPC IDs and mode numbers do not.

Expected stock log sequences:

- **pet shop:** send avatarEnter kind 1 → recv setPetSeeds count 0 → recv setMovie
  mode 1 → send transactionDone → recv modes 11,0 (with an empty stock tank).
- **adopt:** same entry; `autotest adopt unavailable` records zero server seeds;
  transactionDone → modes 11,0. **No petAdopted send is expected on this pin.**
- **kart shop:** send avatarEnter kind 2 → mode 1 → send buyKart/body or
  buyAccessory/id if affordable → authoritative tickets/DNA resends → send
  transactionDone → modes 2,0. Without enough tickets, only checkout occurs.
- **fisherman:** send avatarEnter kind 3 → modes 3,0; no completeSale send when
  there is no START. With a functional server tank/manager, START → send
  completeSale sell 1 → modes 2 or 4,0.
- **party planner:** send avatarEnter kind 4 → modes 6,0; no answer send.

The pet/adopt diagnostic does not claim it adopted when seeds are absent. The
kart diagnostic can spend earned tickets on one eligible item, like a confirmed
manual purchase; use a development account. Main-agent Azahar checks must also
exercise manual B cancellation and confirmations, pending-purchase input lock,
all body/accessory/paint previews, owner ticket updates, and leaving/reentering
without a stale panel. Verify the no-assets installation hint by temporarily
removing only the preview kit. Record Old 3DS heap, linear, VRAM, frame time and
FPS in docs/test-log.md; this task intentionally did not run Azahar.

## Host verification

Run all commands from this worktree, with Python `-I -B`:

- `bash tools/clerks_test/run.sh`: 1,530 exact PetUtil DNA/name/trait/cost cases over
  all six hoods; 5,000 real KartDNA catalogue/ownership cases; 1,000 real FishTank
  totals; actual NPCToons SELL constants; every truncated movie/seed/owner field;
  wrong avatar/class, expired session, funds, reset, holiday generate/update/removal
  and ordered terminal/CLEAR;
  real Client+Connection against the extended strict Panda/DC fake server.
- `python -I -B tools/clerks_test/assets.py --rebake`: every header, batch/index,
  texture, resident budget and SHA; all 199 files byte-identical across processes.
- `python -I -B tools/clerks_test/preview.py`: independent TTM decoder/offscreen
  render of all three bodies with attached accessories; no Azahar.
- `bash tools/nettest/run_tests.sh`: required baseline net/unit/two-client/reconnect.
- `python -I -B tools/clerks_test/regressions.py`: existing host suites, including
  tools/shop_test. Local copies adapt collision resource paths and boss/Cogdo
  name_tables.cpp link inputs without changing protected production/test files.

Fake server option `--clerks stock|offers` is off by default. It adds real DC NPC
objects, enforces clsend membership and Panda argument decoding, and responds with
owner updates. Its positive fish-sale and Doodle create/delete branches are
**fixtures**, not upstream implementations. First party visit mirrors COMINGSOON;
second START/answer(0) is a fixture for the defensive decline path. The ordinary
nettest server behavior is unchanged when the option is omitted.

### Verification availability

On this worktree, 39 existing host suites (including nettest and shop) pass. The
40th, suit-path ground truth, is unavailable: tools/server/.venv and its
panda3d.toontown fork are absent. Its original runner also assumed ref/resources;
the local runner corrects that path and returns 77 with the missing dependency,
rather than substituting a native implementation for the upstream reference.
No other checkout was opened to borrow that environment. The existing nettest's
suit spawn/path update checks still pass. Supply the fork inside this worktree
(or TT_SERVER_PY) to run that separate reference suite.
