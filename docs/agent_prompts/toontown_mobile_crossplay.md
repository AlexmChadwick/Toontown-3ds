# Prompt for the Toontown Mobile (web) agent: cross-play with the 3DS client

You are working on Toontown Mobile (`/Users/alexchadwick/toontown-mobile`). It is a three.js browser
client that plays online against a stock Open Toontown server through `server/bridge/`. A native
Nintendo 3DS client (`/Users/alexchadwick/toontown-3ds`, read-only for you) plays on the same
server:
- DC hash 547155168
- the same Astron client protocol
- no server changes

The goal is full cross-play: a web toon and a 3DS toon must see each other correctly and be able
to share every activity both clients implement.

## What to read first
- `/Users/alexchadwick/toontown-3ds/docs/design/crossplay.md` is the per-minigame contract. It
  covers:
  - which fields each client sends, and how often
  - what each client must do with what the AI sends
  - which local results other clients see, and so must match upstream exactly
- `/Users/alexchadwick/toontown-3ds/AGENTS.md` section 2 (Network): the anim-state rule, the 5 Hz
  `setSmPosHpr` rate, and the anti-cheat field whitelist.
- The upstream client code (`toontown/minigame/Distributed*Game.py`) is the specification, as it is
  for the 3DS client.

## Do this, in order
1. **Shared playground (already online in your Phase 1).** Verify against the 3DS client's
   behaviour, not only against your Python reference client:
   - toon DNA encoding
   - `setAnimState` state names: only `ToontownGlobals.ToonAnimStates`. An invalid name crashes the
     stock AI.
   - `setSmPosHpr` / `setSmStop` units and rate
   - SpeedChat and emote ids
   - nametags

   Write down any place where you deviate from upstream.
2. **The trolley and every minigame (your Phase 5).** Implement them online exactly as
   `crossplay.md` specifies:
   - the shared `DistributedMinigame` base flow
   - the PurchaseManager exit
   - each game's fields

   Where the contract marks a game "shared simulation", port the upstream math exactly (same
   constants, same integration order, same random-number use). Do not write your own physics.
   Ice Slide, Photo Fun's judging, Toon Escape's walker and Cannon need special care.
3. **Prove it.** For each game, run a two-client test: your client against the 3DS client in the
   Azahar emulator, or against a scripted bot that follows the same contract. Check both clients
   show the same positions, scores and results, and that the AI log has no errors or ejections.
   Record every result in your docs.

## Rules
- Never send a field that is not `clsend` (or `ownsend` on your own avatar): the server ejects
  the client.
- Never invent results client-side when the AI is authoritative.
- If upstream is unclear, the "Unclear" list in `crossplay.md` says so. Resolve it from the
  upstream source and report what you found, so the 3DS client can match.
