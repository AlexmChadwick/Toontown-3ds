# Prompt: connect Toontown Mobile to the shared Open Toontown server

Give everything below this line to the AI working in `/Users/alexchadwick/toontown-mobile`.

---

You are working on **Toontown Mobile** (`/Users/alexchadwick/toontown-mobile`), a browser
Toontown built with three.js. It is single-player today: all game logic runs in the page, and
progress is saved in `localStorage` (`src/game/state.ts`).

**Goal:** add an online mode in which Toontown Mobile is a real client of the **same unmodified
Open Toontown server** that the Nintendo 3DS client (`/Users/alexchadwick/toontown-3ds`) uses.
3DS and mobile players then meet in the same world, on the same accounts and toons. The
single-player mode must keep working as the offline fallback.

## The server (already exists; do not modify it)
- **Stock Open Toontown:** Astron + UberDOG + AI district, server version `sv1.0.47.38`.
- **It runs locally** from the 3DS repo:
  - `tools/server/setup.sh` once, then `tools/server/start.sh`; `stop.sh` stops it.
  - With `AI_DEV=1`, `tools/server/ai_dev.py` relaxes some entry checks for testing.
  - Logs are in `tools/server/logs/`.
- **Client agent:** raw **TCP** on port **7198**. A browser cannot open raw TCP, so you need a
  bridge (below).
- **Never edit the server or the DC files.** Anything that is not a stock client behaviour gets
  the account ejected, or can crash the AI.

## Required reading (the 3DS repo is the reference implementation)
- `AGENTS.md` section 2, "Network (Astron client protocol)": framing, HELLO, DC hash, login,
  avatars, districts, zones, movement fields, heartbeat, anti-cheat.
- `tools/server/smoke_test.py`: a **minimal Python bot client** that logs in, plays a toon,
  enters a zone and moves. It is the shortest complete example of the protocol. Port its flow
  first.
- `source/net/client.cpp` / `client.h`: the full C++ client.
  - Framing and datagrams, HELLO, login, avatar list and create, play avatar, time sync, interest
    handling, zone changes, street cells, field parsing and the generate dispatch.
  - `tools/nettest/fake_server.py` is a fake server you can reuse for offline tests.
- `tools/dcgen/dcgen.py`: generates field IDs, signatures and the hash from
  `ref/open-toontown/etc/otp.dc` and `toon.dc`. The DC hash must be **547155168**.
- The `docs/design/*.md` specs: `ui.md` (front end, Pick-a-Toon, Make-a-Toon), `names.md`,
  `playground.md`, `streets.md`, `combat.md`, `trolley.md`, `shop.md`, `transitions.md`, and
  the rest as you reach each feature.

## Architecture
1. **Bridge (`server/bridge/`, Node, no framework):**
   - A WebSocket server that opens one TCP connection to `127.0.0.1:7198` per browser socket.
   - It forwards whole Astron messages both ways: the u16 length prefix on TCP, one binary
     WebSocket message per Astron message.
   - It is a transparent pipe, with no game logic and no rewriting. That keeps every
     server-side check intact.
   - Bind it to the LAN, so phones on the same network can reach it.
   - Add an `npm run bridge` script.
2. **Protocol layer (`src/net/`, TypeScript):**
   - A DC generator in `tools/` that reads the same `otp.dc` and `toon.dc` and emits
     `src/net/dc.generated.ts` (class and field IDs, argument types, the hash). Check the hash
     against 547155168 in a test. **Never hand-edit IDs.**
   - A datagram reader and writer. Everything is little endian. Strings and blobs carry u16
     length prefixes. Handle the arrays and structs the DC uses.
   - A connection state machine, as in `client.cpp`:
     - `CLIENT_HELLO(dcHash, "sv1.0.47.38")`
     - login through `AstronLoginManager` (doId 4670): `requestLogin(token)`. A dev server
       accepts any token.
     - interest (4618, zone 3), then `requestAvatarList`, then create or play the avatar
     - districts, time sync, the zone interest
     - heartbeat (`CLIENT_HEARTBEAT` every 10 s)
   - **Outgoing field whitelist at build time.** Send only `clsend` fields, or `ownsend` fields
     on our own toon. Anything else disconnects the account.
3. **Game integration:**
   - When online, the server is authoritative.
   - Replace the local simulation of what the server owns with the server's objects: other
     toons, Cogs walking the street, battles (`DistributedBattle`), treasures, the trolley,
     shops, tasks, laff, jellybeans and inventory.
   - Keep the local logic only for offline mode.

## Rules learned the hard way (these broke the 3DS client; do not repeat them)
- **One login-manager request at a time.** Duplicate logins, bad toon DNA and protocol
  violations get the account **ejected**.
- **`setAnimState` takes state names, never clip names.** Use only the names in
  `ToontownGlobals.ToonAnimStates`: "run" for all ground locomotion, "jumpAirborne", "Sad",
  "Died", "TeleportOut", ... An unknown name makes the stock AI crash in `banManager` and takes
  the **whole district** down for everyone. Whitelist the names.
- **Movement:**
  - `setSmPosHpr` at 5 Hz while moving, with X/Y/Z as int16/10, H/P/R as (int16 % 360)/10, and a
    16-bit centisecond timestamp.
  - `setSmStop` when movement stops.
  - Remote toons need buffered interpolation, about 200 ms behind their timestamps.
- **Street interest must include the branch zone** (for example 2100) next to the current cell
  and its vis list, as `Street.doEnterZone` does. Without it, every `DistributedBuilding` leaves
  when you change cells (doors, Cog buildings and field offices break).
- **Zone changes go through the quiet zone:** interest in the quiet zone, then the target zone
  plus its vis list.
- **Time sync:** use `TimeManager` / `ClockDelta` before trusting any network timestamp.
- **Trigger tests** use GravityWalker's event sphere: centre at feet + r - 0.1, radius r x 1.04,
  r = 1.4. NPC contact tubes use each class's `getCollSphereRadius` (base 3.25, pet clerk 4.0,
  kart clerk 2.25, fisherman 1.0, party planner 1.0, ...).
- **Read the AI before assuming.** Some fields are not required and arrive late. For example,
  `DistributedCogdoInterior.setToons` comes only after `setAvatarJoined`.

## Phases (each ends with a test against the real server and a short report)
1. **Bridge, DC codegen, login, Pick-a-Toon, play a toon in Toontown Central (zone 2000).** The
   toon shows on the 3DS client (or `smoke_test.py`) and vice versa: movement, animation states
   and SpeedChat.
2. **Make-a-Toon against `createAvatar`** (15-byte DNA netstring), plus the name shop. The
   toons created on mobile and on 3DS are interchangeable.
3. **Streets and zone changes:** tunnels, doors, interiors and teleports, with the cell
   interest above.
4. **Server-driven Cogs and battles:** `DistributedSuit`, `DistributedBattle` (join, choose a
   gag, movies from the server's attack list), and inventory, laff and beans from the toon's
   owner fields.
5. **Then the rest, in the order players meet it:** trolley and minigames, shops, ToonTasks,
   the book.

## Testing
- Unit-test the datagram reader and writer and the DC hash.
- Integration-test against `tools/nettest/fake_server.py`, then against the real server with a
  headless browser (Playwright is already a dev dependency).
- For each phase, record what was run, the server logs, and any ejection or AI traceback.
- **Never claim a feature works online without a real-server run.**
