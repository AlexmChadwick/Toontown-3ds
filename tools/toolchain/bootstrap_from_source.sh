#!/usr/bin/env bash
# Build the devkitARM 3DS toolchain from source into $DEVKITPRO (default ~/devkitpro),
# without sudo and without pkg.devkitpro.org.
#
# Prefer the official installer (`dkp-pacman -S 3ds-dev`) when it works. This script
# exists because pkg.devkitpro.org/downloads.devkitpro.org returned a Cloudflare
# block page for our dev machine (2026-10-23). Every input below comes from GitHub
# (devkitPro's own repos) or GNU/sourceware mirrors.
#
# Tested on macOS 27 arm64 with Xcode clang (2026-10-23). Takes ~30-60 min.
# Usage: tools/toolchain/bootstrap_from_source.sh [workdir]
set -euo pipefail

DEVKITPRO=${DEVKITPRO:-$HOME/devkitpro}
WORK=${1:-$(mktemp -d)}
JOBS=$(getconf _NPROCESSORS_ONLN)
mkdir -p "$WORK" "$DEVKITPRO/tools/bin"
cd "$WORK"
echo "work dir: $WORK  install: $DEVKITPRO"

# Versions pinned to what this repo was tested with.
BINUTILS=2.46.0 GCC=16.1.0 NEWLIB=4.6.0.20260123
RULES=1.6.1 CRTLS=1.2.7
LIBCTRU=v2.7.0 CITRO3D=v1.7.1 CITRO2D=v1.7.0
GENERAL_TOOLS=v1.4.4 TOOLS3DS=v1.3.1 PICASSO=v2.7.2

clone() { [ -d "$2" ] || git clone -q --depth 1 ${3:+-b "$3"} "$1" "$2"; }

# ---- 1. devkitARM (binutils + gcc + newlib + crtls/rules) via devkitPro buildscripts
clone https://github.com/devkitPro/buildscripts.git buildscripts
mkdir -p src stubbin
fetch() { [ -f "src/$2" ] || curl -sfL -o "src/$2" "$1"; }
fetch https://ftp.gnu.org/gnu/binutils/binutils-$BINUTILS.tar.xz binutils-$BINUTILS.tar.xz
fetch https://ftp.gnu.org/gnu/gcc/gcc-$GCC/gcc-$GCC.tar.xz gcc-$GCC.tar.xz
fetch https://sourceware.org/pub/newlib/newlib-$NEWLIB.tar.gz newlib-$NEWLIB.tar.gz
fetch https://github.com/devkitPro/devkitarm-rules/archive/refs/tags/v$RULES.tar.gz devkitarm-rules-$RULES.tar.gz
fetch https://github.com/devkitPro/devkitarm-crtls/archive/refs/tags/v$CRTLS.tar.gz devkitarm-crtls-$CRTLS.tar.gz

# newlib/libgloss docs call makeinfo unconditionally; docs aren't needed.
cat > stubbin/makeinfo <<'EOF'
#!/bin/sh
out=""
while [ $# -gt 0 ]; do case "$1" in -o) out="$2"; shift;; --output=*) out="${1#*=}";; esac; shift; done
[ -n "$out" ] && touch "$out"
exit 0
EOF
chmod +x stubbin/makeinfo

cd buildscripts
# Install prefix is hardcoded to /opt/devkitpro; /usr/local include/lib paths would
# pull in an x86_64 Homebrew's gmp on Apple Silicon, so drop them and build gmp/mpfr/mpc in-tree.
sed -i '' "s|^INSTALLDIR=/opt/devkitpro|INSTALLDIR=$DEVKITPRO|; s| -I/usr/local/include||; s| -L/usr/local/lib||" build-devkit.sh
cat > config.sh <<EOF
BUILD_DKPRO_PACKAGE=1
BUILD_DKPRO_SRCDIR=$WORK/src
export MAKEFLAGS="-j$JOBS"
BUILD_DKPRO_AUTOMATED=1
GCC_DOWNLOAD_PREREQS=1
export OSXMIN=11.0
export MAKEINFO=true
EOF
# BSD patch skips empty new files from git-style patches; pre-create them after extraction.
create_empty_new_files() {
    python3 -I - "$@" <<'EOF'
import re, sys, os
patch, root = sys.argv[1], sys.argv[2]
for chunk in re.split(r'(?m)^diff --git ', open(patch, encoding='latin-1').read())[1:]:
    if 'new file mode' in chunk.split('\n@@', 1)[0] and '\n+++ ' not in chunk:
        path = os.path.join(root, chunk.split('\n', 1)[0].split(' b/', 1)[1])
        os.makedirs(os.path.dirname(path), exist_ok=True)
        open(path, 'a').close()
        print('created empty', path)
EOF
}
# First run extracts + patches; it may fail at newlib on the missing empty file.
PATH="$WORK/stubbin:$PATH" ./build-devkit.sh || true
create_empty_new_files patches/newlib-$NEWLIB-5.patch .devkitARM/newlib-$NEWLIB
PATH="$WORK/stubbin:$PATH" ./build-devkit.sh
cd "$WORK"

DKA=$DEVKITPRO/devkitARM
GCCLIB=$DKA/lib/gcc/arm-none-eabi/$GCC
# GCC stage 1 was configured before newlib's headers existed, so its limits.h does
# not chain to newlib's (PATH_MAX etc. go missing). Rebuild it the way fixincludes
# would and drop the non-chaining include-fixed copy.
mkdir -p gcclim && tar -xJf src/gcc-$GCC.tar.xz -C gcclim gcc-$GCC/gcc/limitx.h gcc-$GCC/gcc/glimits.h gcc-$GCC/gcc/limity.h
cat gcclim/gcc-$GCC/gcc/limitx.h gcclim/gcc-$GCC/gcc/glimits.h gcclim/gcc-$GCC/gcc/limity.h > "$GCCLIB/include/limits.h"
rm -f "$GCCLIB/include-fixed/limits.h" "$GCCLIB/include-fixed/syslimits.h"

# The rules/crtls install steps hardcode /opt/devkitpro and fail silently; do them by hand.
mkdir -p crt && tar -xzf src/devkitarm-rules-$RULES.tar.gz -C crt && tar -xzf src/devkitarm-crtls-$CRTLS.tar.gz -C crt
(cd crt/devkitarm-rules-$RULES && cp 3ds_rules base_rules base_tools ds_rules gba_rules gp32_rules "$DKA/")
sed -i '' "s|/opt/devkitpro|$DEVKITPRO|" "$DKA/base_tools"
(
    cd crt/devkitarm-crtls-$CRTLS
    DEVKITPRO=$DEVKITPRO DEVKITARM=$DKA PATH=$DKA/bin:$PATH make DEPSDIR="$PWD"
    cp *.specs *.ld *.mem "$DKA/arm-none-eabi/lib/"
    cp -R thumb armv6k *.o "$DKA/arm-none-eabi/lib/"
)

# ---- 2. Host tools (compiled directly; no autotools needed)
clone https://github.com/devkitPro/general-tools.git general-tools $GENERAL_TOOLS
clone https://github.com/devkitPro/3dstools.git 3dstools $TOOLS3DS
clone https://github.com/devkitPro/picasso.git picasso $PICASSO
clone https://github.com/3DSGuy/Project_CTR.git Project_CTR
clone https://github.com/carstene1ns/3ds-bannertool.git bannertool
B=$DEVKITPRO/tools/bin
V=(-DPACKAGE_STRING='"dkp-local"' -DVERSION='"local"')
clang -O2 "${V[@]}" -o "$B/bin2s" general-tools/bin2s.c
clang -O2 "${V[@]}" -o "$B/padbin" general-tools/padbin.c
clang++ -O2 -std=c++17 "${V[@]}" -o "$B/3dsxtool" 3dstools/src/3dsxtool.cpp 3dstools/src/romfs.cpp
clang++ -O2 -std=c++17 "${V[@]}" -o "$B/smdhtool" 3dstools/src/smdhtool.cpp 3dstools/src/lodepng/lodepng.cpp
clang++ -O2 -std=c++17 "${V[@]}" -o "$B/picasso" picasso/source/picasso_assembler.cpp picasso/source/picasso_frontend.cpp
(cd Project_CTR/makerom && make deps -j"$JOBS" >/dev/null && make -j"$JOBS" >/dev/null && cp bin/makerom "$B/")
sed -i '' 's/u8 pad\[padLength\] = {0};/u8 pad[4] = {0};/' bannertool/source/3ds/lz11.cpp # VLA init is not C++
(cd bannertool && clang++ -O2 -std=c++20 -DVERSION='"1.2.3"' -Isource -Isource/pc -o "$B/bannertool" $(find source -name '*.cpp'))

# ---- 3. 3DS libraries
export DEVKITPRO DEVKITARM=$DEVKITPRO/devkitARM PATH=$DEVKITPRO/devkitARM/bin:$B:$PATH
clone https://github.com/devkitPro/libctru.git libctru $LIBCTRU
clone https://github.com/devkitPro/citro3d.git citro3d $CITRO3D
clone https://github.com/devkitPro/citro2d.git citro2d $CITRO2D
# libctru 2.7.0 / citro3d 1.7.1 predate GCC 16's new warnings; drop -Werror for them only.
sed -i '' 's/-Werror//g' libctru/libctru/Makefile citro3d/Makefile
make -C libctru/libctru -j"$JOBS" install
make -C citro3d -j"$JOBS" install
# citro2d moved to CMake/catnip; build its four sources + shader by hand.
(
    cd citro2d && mkdir -p build && cd build
    picasso -o render2d.shbin ../source/render2d.v.pica
    bin2s -a 4 -H render2d_shbin.h render2d.shbin | arm-none-eabi-as -o render2d.shbin.o
    for f in base font spritesheet text; do
        arm-none-eabi-gcc -O2 -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft -ffunction-sections \
            -fdata-sections -Wall -DCITRO2D_BUILD -D__3DS__ -I../include -I. -I"$DEVKITPRO/libctru/include" \
            -c ../source/$f.c -o $f.o
    done
    arm-none-eabi-ar rcs libcitro2d.a base.o font.o spritesheet.o text.o render2d.shbin.o
    cp libcitro2d.a "$DEVKITPRO/libctru/lib/"
    cp -R ../include/* "$DEVKITPRO/libctru/include/"
)

echo
echo "Done. Add to your shell profile:"
echo "  export DEVKITPRO=$DEVKITPRO DEVKITARM=$DEVKITPRO/devkitARM PATH=$DEVKITPRO/tools/bin:\$PATH"
