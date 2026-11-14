#!/bin/sh
# Host check for the follow-camera sphere cast. No Panda, no baked terrain, no devkitARM.
# Usage: tools/collision_test/run_camera_sphere.sh
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${CAMERA_SPHERE_OUT:-${TMPDIR:-/tmp}/tt_camera_sphere}
mkdir -p "$OUT"
export ROOT OUT
python3 -I - <<'PY'
import os
import sys
sys.path.insert(0, os.path.join(os.environ["ROOT"], "tools", "pipeline"))
import collide

out = os.environ["OUT"]

solids = collide.CollisionMesh()
# WALL-only slab at x = 2. The camera query must ignore it.
solids.add_tri((2, -1, 0), (2, -1, 3), (2, 1, 0), collide.WALL)
solids.add_tri((2, 1, 0), (2, -1, 3), (2, 1, 3), collide.WALL)
# Camera wall at x = 5, y in [-2, 2], facing -X.
solids.add_tri((5, -2, 0), (5, -2, 4), (5, 2, 0), collide.CAMERA)
solids.add_tri((5, 2, 0), (5, -2, 4), (5, 2, 4), collide.CAMERA)
# Camera wall that sits in the next grid cell (y >= 8.1; cell size 8).
solids.add_tri((5, 8.1, 0), (5, 8.1, 4), (5, 12, 0), collide.CAMERA)
solids.add_tri((5, 12, 0), (5, 8.1, 4), (5, 12, 4), collide.CAMERA)
# Prop the centre ray can miss.
solids.add_cap((8, 3, 1), (8, 3, 1), 0.4, collide.CAMERA)
solids.write(os.path.join(out, "solids.ttc"), cell_size=8)

plane = collide.CollisionMesh()
# Free side is n·p >= d, i.e. x <= 5. Solid is x > 5.
plane.planes.append(((-1.0, 0.0, 0.0), -5.0, collide.CAMERA))
plane.write(os.path.join(out, "plane.ttc"), cell_size=8)
print("wrote", out)
PY
# g++, not clang++: this image's clang is aimed at a GCC 14 libstdc++ that is not installed.
${CXX:-g++} -std=c++17 -O2 -Wall -Wextra -fno-exceptions -fno-rtti -I "$ROOT/source" \
    "$ROOT/source/world/collision.cpp" "$ROOT/tools/collision_test/log_stub.cpp" \
    "$ROOT/tools/collision_test/camera_sphere_test.cpp" -o "$OUT/camera_sphere_test"
"$OUT/camera_sphere_test" "$OUT/solids.ttc" "$OUT/plane.ttc"
