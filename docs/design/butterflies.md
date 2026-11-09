# Playground butterflies

The stock AI spawns `DistributedButterfly` objects in Toontown Central (4 areas x 6) and Daisy
Gardens (1 area x 36) (`HoodDataAI.createButterflies`). Each one flies between the points of
its area in `ButterflyGlobals.ButterflyPoints`, chosen by the AI and sent as
`setState(state, curIndex, destIndex, time/10, timestamp)`; `setArea(playground, area)` names
the table.

- Assets: `tools/pipeline/bake_butterfly.py` bakes `SZ_butterfly-mod` once per wing set
  (`doId % 6` picks it upstream) to `data/butterfly/wing_<n>.ttm`, the wing on tint slot 1 so
  the runtime tint is the `setColor` colour, with `flutter` and `land` vertex tracks at 12 fps.
- Points: `tools/gen_butterfly_points.py` writes `source/world/butterfly_points.h`.
- Network: `net/client_butterfly.cpp` keeps a fixed pool of 40.
- Runtime: `world/butterflies.*` replays `enterFlying` (rise to the higher point plus
  `BUTTERFLY_HEIGHT` over the take-off time, cross, descend over the landing time, heading by
  `headsUp(dest)`) and `enterLanded`, inside the LODNode's 100 units; flutter / land loop at the
  per-butterfly play rate inside 40, a landed one past 40 holds one land frame.

Simplified: the `glide` blend and the take-off / landing cross-fade between clips (the morph
shader blends two frames of one clip), the tiny drop shadow, and `avatarEnter` (a toon
walking into a butterfly makes it fly off; the client never sends it, the AI moves them on its
own timer).
