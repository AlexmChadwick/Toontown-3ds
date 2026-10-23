# Toontown 3DS

Unofficial native Nintendo 3DS client for [Open Toontown](https://github.com/open-toontown/open-toontown),
written in C++ on devkitARM + libctru + citro3d/citro2d. Targets **Old 3DS** as the
baseline; New 3DS gets optional extras only.

Status: **Phase 1** (renderer skeleton). It shows the Toontown Central playground
terrain, the gazebo, the mailboxes and a controllable, tinted toon on the top screen,
and a five-tab touch UI on the bottom screen. There is no collision, animation or
networking yet. See [AGENTS.md](AGENTS.md) for architecture, budgets and the phase
plan, and [docs/test-log.md](docs/test-log.md) for measured results.

> Toontown assets belong to The Walt Disney Company. This repo ships **no** game
> assets. You bake them locally from the upstream `resources` repo; the output
> (`romfs/baked/`, `sdmc-out/`) is gitignored and must not be redistributed.

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
2. **For .cia builds:** `makerom` and `bannertool` on `PATH`
   (releases from the Project_CTR and bannertool GitHub projects).
3. **For the asset pipeline (Phase 1+):** Python 3.12 + stock `panda3d==1.10.15`.
   ```bash
   uv venv -p 3.12 .venv
   uv pip install --python .venv panda3d==1.10.15
   ```
4. **Upstream reference repos** (read-only, gitignored):
   ```bash
   git clone --depth 1 https://github.com/open-toontown/open-toontown.git ref/open-toontown
   ```
   ```bash
   git clone --depth 1 https://github.com/open-toontown/resources.git ref/resources
   ```

## Build

Bake the assets first. They come from `ref/resources` and are gitignored:
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
| `ref` | upstream clones (gitignored) |
