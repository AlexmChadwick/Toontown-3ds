# Toontown 3DS

Unofficial native Nintendo 3DS client for [Open Toontown](https://github.com/open-toontown/open-toontown),
written in C++ on devkitARM + libctru + citro3d/citro2d. Targets **Old 3DS** as the
baseline; New 3DS gets optional extras only.

Status: **Phase 1** (renderer skeleton). It shows the Toontown Central playground
terrain, the gazebo, the mailboxes and a controllable, tinted toon on the top screen,
and a five-tab touch UI on the bottom screen. There is no collision, animation or
networking yet. See [AGENTS.md](AGENTS.md) for architecture, budgets and the phase
plan, and [docs/test-log.md](docs/test-log.md) for measured results.

> Toontown Online assets belong to The Walt Disney Company. The baked assets in
> `romfs/baked/` and the `ref/resources` submodule are included as-is and are **not**
> covered by this project's MIT license; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Prerequisites

1. **devkitPro** with the 3DS packages
   (<https://devkitpro.org/wiki/Getting_Started>).
   - If pkg.devkitpro.org is unreachable for you (it served us a Cloudflare block
     page), [tools/toolchain/bootstrap_from_source.sh](tools/toolchain/bootstrap_from_source.sh)
     builds the same toolchain from GitHub/GNU sources into `~/devkitpro`, without
     sudo, in about 30–60 min. The script collects the exact steps that worked on our
     machine but has not yet been re-run end to end from a clean state.
   - Otherwise, on macOS install the `devkitpro-pacman` .pkg, then:
   ```bash
   sudo dkp-pacman -S 3ds-dev
   ```
   and add to your shell profile:
   ```bash
   export DEVKITPRO=/opt/devkitpro DEVKITARM=/opt/devkitpro/devkitARM PATH=/opt/devkitpro/tools/bin:$PATH
   ```
2. **For .cia builds:** `makerom` and `bannertool`. The Makefile looks in
   `$DEVKITPRO/tools/bin` first, then on `PATH`. A devkitPro `3ds-dev` install normally
   puts both there already; check with `ls $DEVKITPRO/tools/bin`. If they are missing,
   download the release binaries yourself (the Makefile does not fetch anything):
   - `makerom`: <https://github.com/3DSGuy/Project_CTR> (Releases page)
   - `bannertool`: <https://github.com/Steveice10/bannertool> (Releases page; use a
     maintained fork if upstream is stale)

   Put them in `$DEVKITPRO/tools/bin` and make them executable. Plain `make` does
   not need them. `make cia` stops with a message naming the missing tool.
3. **For the asset pipeline (Phase 1+):** Python 3.12 + stock `panda3d==1.10.15`.
   ```bash
   uv venv -p 3.12 .venv
   uv pip install --python .venv panda3d==1.10.15
   ```
4. **Upstream reference repos** live in `ref/` as pinned, shallow git submodules.
   You need them only to re-bake assets or regenerate `dc_generated.h`. The
   resources submodule is about 670 MB:
   ```bash
   git submodule update --init --depth 1
   ```

## Build

Pre-baked Phase 1 assets are committed in `romfs/baked/`. To re-bake them from
`ref/resources` (needs the submodules and the Python venv), run:
```bash
make assets
```
Then build. Plain `make` produces `toontown3ds.3dsx` (Homebrew Launcher):
```bash
make
```
and

```bash
make cia
```
produces `toontown3ds.cia`.

To assemble the SD-card tree (the 3DSX, the SD data set and a placeholder `server.txt`)
in `dist/sd/`, run `make` and then:
```bash
tools/dev/make_sd_package.sh
```
It mirrors `sdmc-out/3ds/toontown3ds/data/` with rsync, prints the size and leaves
`server.txt` alone if you already edited it. Options: `--src DIR` (another data root),
`--3dsx FILE`, `--out DIR`. `dist/` is gitignored.

## Install on a 3DS

Copy the contents of `dist/sd/` to the root of the SD card. Merge the `3ds` folder,
do not replace it. The result looks like this:

```
sdmc:/3ds/toontown3ds/toontown3ds.3dsx   Homebrew Launcher app
sdmc:/3ds/toontown3ds/data/             baked SD data set, about 480 MB
sdmc:/3ds/toontown3ds/server.txt        server settings (see below)
```

1. **3DSX (Homebrew Launcher).** Launch "Toontown 3DS" from the Homebrew Launcher. It
   finds `/3ds/toontown3ds/toontown3ds.3dsx` by the folder-per-app convention.
2. **CIA (FBI).** Build `toontown3ds.cia` with `make cia` and copy it to the SD card,
   in any folder on the card. In FBI, choose *SD*, select the file, then *Install*.
   The CIA reads the same `sdmc:/3ds/toontown3ds/` data folder, so the data set still
   has to be on the card.
3. **SD data set.** It holds every toon species and clothing pack, the streets, all
   536 building interiors, the playgrounds with their music and the character and
   boat assets. Without it you only get the default toon and the TTC playground from
   RomFS. See "Bake the SD data set" for how to produce it.
4. **Sound.** Dump the DSP firmware once with the "DSP1" homebrew
   (`sdmc:/3ds/dspfirm.cdc`). Without it the game runs silently.
5. **Multiplayer: `server.txt`.** The game reads `sdmc:/3ds/toontown3ds/server.txt` at
   startup. It is `key=value` per line, `#` starts a comment, and a missing file or
   unknown key falls back to the defaults:

   ```
   host=192.168.1.20   # LAN IP of the PC running tools/server/start.sh
   port=7198           # astrond client agent
   token=my-toon       # any token works on a dev server; each one creates an account
   online=1            # 0 = offline, no network at all
   ```

   The defaults are `host=127.0.0.1`, `port=7198`, `token=3ds-player`, `online=1`.
   On the 3DS, `127.0.0.1` means the console itself, so set `host` to your PC's LAN
   address. The PC and the 3DS must be on the same network, and the firewall must allow
   TCP 7198 in.

## Bake the SD data set

The baker scripts need the upstream submodules and the Python venv from the
prerequisites (Python 3.12, `panda3d==1.10.15`). Run from the repo root:

```bash
make assets                 # bake_phase1.sh: TTC boot set into romfs/baked/,
                            # toon parts into sdmc-out/3ds/toontown3ds/data/toon/
.venv/bin/python -I tools/pipeline/bake_street.py --zone all   # zones/*.tz: playgrounds and streets
.venv/bin/python -I tools/pipeline/bake_hoods.py              # zones/*.ttm|ttc and audio/<zone>.tsnd (playground music)
.venv/bin/python -I tools/pipeline/bake_minimap.py            # zones/*.tmap (run after bake_street)
.venv/bin/python -I tools/pipeline/bake_suit_paths.py         # zones/*.tsp (cog walk graphs)
.venv/bin/python -I tools/pipeline/bake_boat.py --resources ref/resources  # Donald's Dock boat
.venv/bin/python -I tools/pipeline/bake_street.py --zone 1000 --separate-boat  # 1000.tz without the boat
.venv/bin/python -I tools/pipeline/bake_interior.py --all     # interiors/ (536 buildings)
.venv/bin/python -I tools/pipeline/bake_char.py              # chars/ (classic characters)
.venv/bin/python -I tools/pipeline/char_paths_bin.py sdmc-out/3ds/toontown3ds/data/chars  # chars/*.tcp walk graphs
```

Order matters. `bake_minimap.py` reads the zone files, and the 1000 street bake is
redone after `bake_boat.py`. Output goes to `sdmc-out/3ds/toontown3ds/data/`, which
`tools/dev/make_sd_package.sh` packages. The bakers are deterministic. Each one prints a
budget report and fails if a budget is exceeded. `bake_audio.py` (footstep and UI
sounds) writes to `romfs/baked/audio` by default. Pass `--out` to write elsewhere.

## Run the local server

The client talks to a real Open Toontown server (astrond, UberDOG and AI). It runs on
your PC from `ref/`:

```bash
tools/server/setup.sh       # once: venv and the Open Toontown Panda3D fork (builds it,
                            # about 10 min on an M1 Max; needs Rosetta for astrond)
tools/server/start.sh       # client agent on 0.0.0.0:7198
tools/server/stop.sh
tools/server/start.sh --reset   # wipe accounts and toons first
```

`start.sh` needs `ref/open-toontown` and `ref/resources`. The prebuilt `astrond` is
x86_64, so on Apple silicon it runs under Rosetta. Logs go to `tools/server/logs/`.

On Linux x86_64, run `tools/server/setup_linux.sh` once instead of `setup.sh`. It needs
only curl and git (no sudo, no system compiler): it fetches a conda-forge toolchain with
micromamba into `tools/server/.deps/`, builds `astrond` from Astron and the Panda3D fork
from source, and creates `tools/server/.venv`. `start.sh` and `stop.sh` work on both
systems.
Test the server from the PC with `tools/server/smoke_test.py` before you involve the
3DS. Then set `host=` in `server.txt` to the PC's LAN address.

## Autotest triggers (scripted test pass)

The game runs a scripted pass when one of these empty files exists on the SD card (names
from `source/core/autotest.cpp`):

| File | What it runs |
|---|---|
| `sdmc:/3ds/toontown3ds/autotest_fe` | front end: title, Play online, Make a Toon, create, run in the world |
| `sdmc:/3ds/toontown3ds/autotest` | world script: moves, jump, tabs, stress 8 to 64 toons, treasure, NPC, doors, tunnels, teleports |

`autotest_fe` takes precedence if both exist. Create an empty file with, for example,
`touch /Volumes/<card>/3ds/toontown3ds/autotest` on the mounted card. The game log is
written to `sdmc:/3ds/toontown3ds/log.txt`.

## Run in Azahar

Load `toontown3ds.3dsx` via File → Load File, or install the `.cia` with
`azahar -i toontown3ds.cia`. Use the Old 3DS system model (Emulation → Configure →
System → *Old 3DS*) for all budget measurements. An unattended scripted test exists;
see [docs/test-log.md](docs/test-log.md). Record results there.

## Controls

| Input | Action |
|---|---|
| Circle Pad | run (up/down) / turn (left/right) |
| B (hold) | walk |
| A | jump |
| L / R | orbit camera |
| C-Stick | orbit/pitch camera (New 3DS only) |
| D-Pad left/right or touch | switch bottom-screen tab |
| X | Chat tab |
| Y | Map tab |
| START | quit |

## Layout

| Path | Purpose |
| --- | --- |
| `source/core` | memory budgets, timing, logging, APT/lifecycle |
| `source/render` | citro3d world renderer, citro2d UI (Phase 1) |
| `source/world` | baked zone loading, collision, toon controller (Phase 2) |
| `source/net` | Astron client protocol, DC subset (Phase 3) |
| `shaders` | PICA200 `.v.pica` vertex shaders |
| `tools/pipeline` | offline bam/DNA → 3DS asset baker (Python) |
| `tools/dev` | dev helpers (render-target dump decoder) |
| `tools/toolchain` | from-source devkitARM bootstrap |
| `meta` | icon, banner, CIA `app.rsf` |
| `ref` | upstream submodules: open-toontown (code) and resources (assets) |

## License

This project's code is MIT-licensed ([LICENSE](LICENSE)). Parts derived from Open
Toontown keep its BSD 3-Clause notice, and the Toontown game assets remain Disney's.
See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
