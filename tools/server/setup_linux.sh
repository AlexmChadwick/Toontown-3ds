#!/bin/bash
# One-time setup for the local Open Toontown server (Astron + UberDOG + AI) on
# Linux x86_64. The macOS equivalent is setup.sh; start.sh/stop.sh serve both.
#
# Produces (all gitignored, all inside tools/server/):
#   .deps/micromamba    static micromamba binary (pinned release, sha256-checked)
#   .deps/env/          conda-forge toolchain + libs: Python 3.9, gcc/g++ 13,
#                       make, cmake, patchelf, libuv, yaml-cpp, boost headers,
#                       openssl, freetype, harfbuzz, ODE, zlib
#   .deps/astron/       astrond built from github.com/Astron/Astron (pinned
#                       commit, yaml DB backend, no Mongo); rpath -> .deps/env/lib
#   .deps/panda3d-ott/  clone of github.com/open-toontown/panda3d (pinned),
#                       built with makepanda into a self-contained wheel
#                       (server-only feature set; ~10 min with -j6)
#   .venv/              venv on .deps/env's Python 3.9 with that wheel + pytz
#
# Needs only: bash, curl, git, coreutils (sha256sum), tar. No sudo, no system
# compiler, no Docker. Idempotent: finished steps are skipped on re-run.
# JOBS=<n> sets build parallelism (default 6); builds run under nice -n 19.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"

[ "$(uname -s)" = Linux ] && [ "$(uname -m)" = x86_64 ] || { echo "setup_linux.sh is for Linux x86_64 (macOS: setup.sh)"; exit 1; }
for t in curl git sha256sum; do command -v $t >/dev/null || { echo "$t not found"; exit 1; }; done

JOBS="${JOBS:-6}"
MM_VER=2.10.0-0
MM_URL=https://github.com/mamba-org/micromamba-releases/releases/download/$MM_VER/micromamba-linux-64
MM_SHA=701f39c06f8e6a670a0e186c8479b9e5c0502a2f0cda6cef65d83bdd2875e8bd
ASTRON_REPO=https://github.com/Astron/Astron.git
ASTRON_COMMIT=9a8ebc1449dc15ae69c2f1ef3176b674e656a2a0   # verified 2026-10-27
PANDA_REPO=https://github.com/open-toontown/panda3d.git
PANDA_COMMIT=06615744481946654610833679a0893589114504    # same pin as setup.sh
# Versions verified to build and run the server (2026-10-27).
PKGS=(python=3.9.23 gxx_linux-64=13.4.0 make=4.4.1 cmake=4.4.4 pkg-config=0.29.2
      patchelf=0.19.2 libuv=1.52.1 yaml-cpp=0.8.0 libboost-headers=1.92.0
      openssl=3.6.5 freetype=2.14.3 harfbuzz=15.0.0 libode=0.16.5 zlib=1.3.2)

DEPS="$HERE/.deps"
ENV="$DEPS/env"
mkdir -p "$DEPS"
export LC_ALL=C   # the conda env has no locales; silences setlocale warnings

# 1. Toolchain and libraries (conda-forge via micromamba).
if [ ! -x "$DEPS/micromamba" ] || ! echo "$MM_SHA  $DEPS/micromamba" | sha256sum -c --status -; then
    curl -fL -o "$DEPS/micromamba.tmp" "$MM_URL"
    echo "$MM_SHA  $DEPS/micromamba.tmp" | sha256sum -c -
    chmod +x "$DEPS/micromamba.tmp" && mv "$DEPS/micromamba.tmp" "$DEPS/micromamba"
fi
export MAMBA_ROOT_PREFIX="$DEPS/mamba"
if [ ! -f "$ENV/.tt-pkgs" ] || [ "$(cat "$ENV/.tt-pkgs")" != "${PKGS[*]}" ]; then
    rm -rf "$ENV"
    "$DEPS/micromamba" create -y -q -p "$ENV" -c conda-forge --override-channels "${PKGS[@]}"
    echo "${PKGS[*]}" > "$ENV/.tt-pkgs"
fi
CC="$ENV/bin/x86_64-conda-linux-gnu-gcc"
CXX="$ENV/bin/x86_64-conda-linux-gnu-g++"

checkout() {  # checkout <dir> <repo> <commit>
    if [ ! -d "$1/.git" ]; then git init -q "$1" && git -C "$1" remote add origin "$2"; fi
    if [ "$(git -C "$1" rev-parse -q --verify HEAD 2>/dev/null)" != "$3" ]; then
        git -C "$1" fetch -q --depth 1 origin "$3"
        git -C "$1" checkout -q -f FETCH_HEAD
    fi
}

# 2. astrond.
ASTROND="$DEPS/astron/bin/astrond"
if [ ! -x "$ASTROND" ] || [ "$(cat "$DEPS/astron/.commit" 2>/dev/null)" != "$ASTRON_COMMIT" ]; then
    checkout "$DEPS/astron-src" "$ASTRON_REPO" "$ASTRON_COMMIT"
    rm -rf "$DEPS/astron-build" "$DEPS/astron"
    # Astron's CMakeLists predates CMake 3.5 (policy floor needed by CMake 4).
    # Bison/flex are not needed: the dclass parser sources are pre-generated.
    (cd "$DEPS" && PATH="$ENV/bin:$PATH" nice -n 19 "$ENV/bin/cmake" -S astron-src -B astron-build \
        -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
        -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" \
        -DCMAKE_PREFIX_PATH="$ENV" -DYAMLCPP_USE_STATIC_LIBS=OFF -DBUILD_DB_MONGO=OFF \
        -DPYTHON_EXECUTABLE=/bin/false -DCMAKE_EXE_LINKER_FLAGS="-Wl,-rpath,$ENV/lib" >/dev/null)
    PATH="$ENV/bin:$PATH" nice -n 19 "$ENV/bin/make" -C "$DEPS/astron-build" -j"$JOBS" astrond
    mkdir -p "$DEPS/astron/bin"
    cp "$DEPS/astron-build/astrond" "$ASTROND"
    echo "$ASTRON_COMMIT" > "$DEPS/astron/.commit"
fi
"$ASTROND" --version 2>&1 | head -1 || true

# 3. venv (Python 3.9 from the conda env) with pytz.
if [ ! -x .venv/bin/python ] || ! .venv/bin/python -I -c "import sys; assert sys.version_info[:2] == (3, 9)" 2>/dev/null; then
    rm -rf .venv
    "$ENV/bin/python" -m venv .venv
fi
.venv/bin/python -I -m pip install -q --disable-pip-version-check pytz

# 4. The Open Toontown Panda3D fork.
CHECK="import panda3d.toontown, panda3d.otp, panda3d.physics, panda3d.ode; from panda3d.core import DynamicTextFont, HTTPClient"
if .venv/bin/python -I -c "$CHECK" 2>/dev/null; then
    echo "Panda3D fork already installed in .venv"
    exit 0
fi

checkout "$DEPS/panda3d-ott" "$PANDA_REPO" "$PANDA_COMMIT"
cd "$DEPS/panda3d-ott"
# makewheel only bundles shared-library dependencies found next to Panda's own
# libs; also let it pick up (and rpath to $ORIGIN) the conda-forge libraries.
if ! grep -q PANDA_WHEEL_LIB_PATH makepanda/makewheel.py; then
    sed -i 's|^    whl.lib_path = \[libs_dir\]$|    whl.lib_path = [libs_dir] + [p for p in os.environ.get("PANDA_WHEEL_LIB_PATH", "").split(":") if p]|' makepanda/makewheel.py
    grep -q PANDA_WHEEL_LIB_PATH makepanda/makewheel.py || { echo "makewheel.py patch failed"; exit 1; }
fi

# Same server-only feature set as setup.sh (minus NEON). makepanda finds the
# conda headers/libs through gcc's CPATH/LIBRARY_PATH search dirs (conda's ld is a
# cross linker, so indirect deps need -rpath-link); makepanda also
# pip-installs panda3d-interrogate into the conda Python.
rm -f panda3d-*.whl
START=$(date +%s)
PATH="$ENV/bin:$PATH" CC="$CC" CXX="$CXX" CPATH="$ENV/include" LIBRARY_PATH="$ENV/lib" \
PKG_CONFIG_PATH="$ENV/lib/pkgconfig:$ENV/share/pkgconfig" LDFLAGS="-Wl,-rpath,$ENV/lib -Wl,-rpath-link,$ENV/lib" \
PANDA_WHEEL_LIB_PATH="$ENV/lib" \
nice -n 19 "$ENV/bin/python" makepanda/makepanda.py \
    --nothing --use-python --use-direct --use-zlib --use-freetype --use-harfbuzz --use-openssl --use-ode \
    --use-pandaphysics --use-pandaparticlesystem \
    --use-nametag --use-movement --use-dna --use-suit --use-pets \
    --wheel --threads "$JOBS" --outputdir built > ../build.log 2>&1 || { tail -40 ../build.log; echo "makepanda failed; see $DEPS/build.log"; exit 1; }
echo "makepanda: $(( $(date +%s) - START )) s"

WHEEL="$(ls -t panda3d-*.whl | head -1)"
"$HERE/.venv/bin/python" -I -m pip install -q --disable-pip-version-check --force-reinstall --no-deps "$WHEEL"
"$HERE/.venv/bin/python" -I -c "$CHECK; import panda3d.core; print('panda3d', panda3d.core.PandaSystem.getVersionString(), 'OK')"
