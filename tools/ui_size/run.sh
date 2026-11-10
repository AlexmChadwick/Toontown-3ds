#!/usr/bin/env bash
# Compile source/render/ui.cpp and the Ui sizeof assert for ARM, then print the size.
# Needs arm-none-eabi-g++ and the libctru / citro3d / citro2d headers.
# DEVKITPRO is the devkitPro root. TT_LIBCTRU, TT_CITRO3D and TT_CITRO2D override
# the three include directories when the headers are not installed under DEVKITPRO.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CXX="${ARM_CXX:-arm-none-eabi-g++}"

if [ -n "${TT_LIBCTRU:-}" ]; then
    INC_CTRU="$TT_LIBCTRU"
    INC_C3D="$TT_CITRO3D"
    INC_C2D="$TT_CITRO2D"
elif [ -n "${DEVKITPRO:-}" ] && [ -d "$DEVKITPRO/libctru/include" ]; then
    INC_CTRU="$DEVKITPRO/libctru/include"
    INC_C3D="$DEVKITPRO/portlibs/3ds/include"
    INC_C2D="$DEVKITPRO/portlibs/3ds/include"
else
    echo "ui_size: set DEVKITPRO or TT_LIBCTRU/TT_CITRO3D/TT_CITRO2D" >&2
    exit 1
fi

FLAGS=(-std=c++17 -Wall -Wextra -D__3DS__ -DARM11 -Os -mfloat-abi=soft -march=armv6k -mtune=mpcore -mtp=soft -fno-rtti -fno-exceptions)
INCS=(-I"$ROOT/source" -I"$INC_CTRU" -I"$INC_C3D" -I"$INC_C2D")

"$CXX" "${FLAGS[@]}" -c "$ROOT/source/render/ui.cpp" -o /tmp/toontown-ui.o "${INCS[@]}"
"$CXX" "${FLAGS[@]}" -c "$ROOT/tools/ui_size/check.cpp" -o /tmp/toontown-ui-size.o "${INCS[@]}"

LINE="$(arm-none-eabi-nm -S /tmp/toontown-ui-size.o | awk '/kUiBytes/ {print $2}')"
SIZE=$((16#$LINE))
echo "sizeof(Ui)=$SIZE"
if [ "$SIZE" -gt $((112 * 1024)) ]; then
    echo "Ui exceeds 112 KiB" >&2
    exit 1
fi
