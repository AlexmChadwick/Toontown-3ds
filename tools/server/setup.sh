#!/bin/bash
# One-time setup for the local Open Toontown server (Astron + UberDOG + AI).
#
# Produces (all gitignored, all inside tools/server/):
#   .venv/              Python 3.9.25 (uv-managed, arm64) venv with the Open
#                       Toontown Panda3D fork (panda3d.otp + panda3d.toontown)
#                       and pytz
#   .deps/panda3d-ott/  shallow clone of github.com/open-toontown/panda3d, built
#                       with makepanda into an arm64 wheel (server-only feature
#                       set: no graphics/audio; ~10 min on an M1 Max)
#   .deps/src/tp/       Panda3D's official prebuilt macOS thirdparty libs
#                       (universal; we use openssl, freetype+harfbuzz, ode, zlib)
#
# Needs: uv (~/.local/bin/uv), git, curl, Xcode or Command Line Tools, Rosetta 2
# (the prebuilt astrond in ref/open-toontown/astron/darwin is x86_64).
# No sudo, nothing installed system-wide.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"

PANDA_REPO=https://github.com/open-toontown/panda3d.git
# Commit that was verified to build & run the server (2026-10-23).
PANDA_COMMIT=06615744481946654610833679a0893589114504
TP_URL=https://www.panda3d.org/download/panda3d-1.10.16/panda3d-1.10.16-tools-mac.tar.gz
TP_SHA=b56176be39a160d65653b13489d786808f5357896013d95dca8e3ea55f1858c9

command -v uv >/dev/null || { echo "uv not found (install: https://docs.astral.sh/uv/)"; exit 1; }
arch -x86_64 /usr/bin/true 2>/dev/null || { echo "Rosetta 2 is required for the x86_64 astrond"; exit 1; }

if [ ! -x .venv/bin/python ]; then
    uv python install 3.9
    uv venv --python 3.9 .venv
fi
uv pip install --python .venv/bin/python pip setuptools wheel pytz

CHECK="import panda3d.toontown, panda3d.otp, panda3d.physics, panda3d.ode; from panda3d.core import DynamicTextFont, HTTPClient"
if .venv/bin/python -I -c "$CHECK" 2>/dev/null; then
    echo "Panda3D fork already installed in .venv"
    exit 0
fi

mkdir -p .deps/src
if [ ! -d .deps/src/tp/panda3d-1.10.16/thirdparty/darwin-libs-a ]; then
    [ -f .deps/src/panda3d-1.10.16-tools-mac.tar.gz ] || curl -fL -o .deps/src/panda3d-1.10.16-tools-mac.tar.gz "$TP_URL"
    echo "$TP_SHA  .deps/src/panda3d-1.10.16-tools-mac.tar.gz" | shasum -a 256 -c -
    mkdir -p .deps/src/tp && tar xzf .deps/src/panda3d-1.10.16-tools-mac.tar.gz -C .deps/src/tp
fi

if [ ! -d .deps/panda3d-ott/.git ]; then
    git clone --depth 1 "$PANDA_REPO" .deps/panda3d-ott
fi
cd .deps/panda3d-ott
if [ "$(git rev-parse HEAD)" != "$PANDA_COMMIT" ]; then
    git fetch --depth 1 origin "$PANDA_COMMIT" && git checkout -q "$PANDA_COMMIT" || \
        echo "WARNING: could not pin $PANDA_COMMIT, building $(git rev-parse HEAD)"
fi
rm -rf thirdparty && ln -s ../src/tp/panda3d-1.10.16/thirdparty thirdparty

# makepanda only knows macOS SDKs up to 15.5; teach it about the installed one.
SDKVER="$(xcrun --show-sdk-version)"
if ! grep -q "\"$SDKVER\"" makepanda/makepandacore.py; then
    sed -i '' "s/sdk_versions += \[\"15.5\",/sdk_versions += [\"$SDKVER\", \"15.5\",/" makepanda/makepandacore.py
fi
# uv's python-build-standalone links _ctypes statically (no __file__).
sed -i '' 's/        import _ctypes$/        import sysconfig/; s/        return os.path.dirname(_ctypes.__file__)/        return sysconfig.get_config_var("DESTSHARED")/' makepanda/makewheel.py

# Server needs: freetype (NameGenerator loads a .ttf at import), openssl
# (HTTPClient in BanManagerAI), ode (golf/ice physics), physics+particles
# (imported by battle code), and the OTP/Toontown C++ modules.
"$HERE/.venv/bin/python" makepanda/makepanda.py \
    --nothing --use-python --use-direct --use-zlib --use-freetype --use-harfbuzz --use-openssl --use-ode \
    --use-pandaphysics --use-pandaparticlesystem --use-neon \
    --use-nametag --use-movement --use-dna --use-suit --use-pets \
    --wheel --threads "${JOBS:-4}" --outputdir built 2>&1 | tee ../build.log | grep -E "^(Using|Writing|Build)" || true

WHEEL="$(ls -t panda3d-*.whl | head -1)"
uv pip install --python "$HERE/.venv/bin/python" --reinstall "$WHEEL"
"$HERE/.venv/bin/python" -I -c "$CHECK; import panda3d.core; print('panda3d', panda3d.core.PandaSystem.getVersionString(), 'OK')"
