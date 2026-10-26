# Chip 'n Dale's MiniGolf

The golf path uses the stock Open Toontown `DistributedGolfKart`,
`DistributedGolfCourse`, `DistributedGolfHole`, and `DistributedPhysicsWorld`
objects. The kart is boarded in Acorn Acres (zone 6000); the AI assigns a
dynamic golf zone with `setGolfZone(zone, courseId)`. The client changes interest
to that zone, answers the course/hole readiness barriers, and returns to Acorn
Acres (17000) after `setCourseExit` or the local B exit request.

## Swing contract

Stock golf runs a 90 Hz physics step with gravity -25, ERP 0.8, CFM 1e-5,
QuickStep iterations 8, a 0.25-unit ball radius, and a 0.75-unit cup acceptance
radius. The server selects a trusted golfer and accepts that client's
`ballMovie2AI`; it does not independently recompute the trajectory.

The current native client has the course join/ready/tee barriers and does not
send `ballInHole` itself. Its local `golf_logic` recording is a deterministic,
collision-free host-tested approximation only; it is not a substitute for the
upstream ODE collision world and must not be used to claim a cup. Full trusted
`assignRecordSwing` playback still requires a generated DC constant plus baked
hard/soft/cup collision meshes for every hole.

## Assets

`tools/pipeline/bake_golf.py` reads `GolfGlobals.HoleInfo`, bakes the 18 source
terrain models into `sdmc-out/3ds/toontown3ds/data/golf/holeN.ttm`, and writes all
36 logical hole records to `holes.json` with par, max swings, tee, cup, and
physics-data names. Textures are emitted through the existing TTM pipeline and
are ETC1-compatible under the repository texture configuration. The runtime
loads only the selected hole model and keeps the existing Old 3DS linear-memory
budgets.

## Autotest / Azahar

The unattended labels are `golf: board`, `golf: swing`, `golf: finish hole`, and
`golf: exit`. Run the normal `autotest` pass against a stock server with a toon
in Acorn Acres; the board step walks to the generated golf kart. A solo course
is supported by the stock AI: board one toon, press A, and wait for the kart's
departure countdown to finish. For an immediate forced run in a development AI
console, call the seated kart's `trolleyLeft()` method; it calls
`GolfManagerAI.readyGolfCourse([avId], golfCourse)` and sends the normal
`setGolfZone` update. Then observe the dynamic-zone handoff, hole readiness,
swing/movie, score/course barrier, reward/exit, and return to zone 17000.
Azahar was not run by this branch.
