"""Validate every generated asset, hashes, budgets and a fresh-process rebake."""

from pathlib import Path
import argparse
import hashlib
import json
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "sdmc-out/3ds/toontown3ds/data/clerks/karts"
parser = argparse.ArgumentParser()
parser.add_argument("--rebake", action="store_true")
args = parser.parse_args()
manifest = json.loads((OUT / "manifest.json").read_text())
first = {
    path.name: hashlib.sha256(path.read_bytes()).hexdigest()
    for path in sorted(OUT.iterdir())
}
for name, digest in manifest["sha256"].items():
    assert first[name] == digest, (name, "digest")
for name, stats in manifest["worst_resident"].items():
    assert (
        stats["geometry_bytes"] <= 512 * 1024 and stats["texture_bytes"] <= 1024 * 1024
    )
    assert stats["triangles"] <= 15000 and stats["batches"] <= 150
for path in OUT.glob("*.ttm"):
    data = path.read_bytes()
    header = struct.unpack_from("<4sHHHHII3f3fIIIII", data)
    magic, version, _, textures, batches, vertices, indices = header[:7]
    tex_off, batch_off, vertex_off, index_off, _ = header[13:]
    assert magic == b"TTM1" and version == 1 and vertices <= 65535 and indices % 3 == 0
    assert index_off + indices * 2 <= len(data)
    for index in range(textures):
        width, height, fmt, mips, ws, wt, offset, size, name = struct.unpack_from(
            "<HHBBBBII32s", data, tex_off + 48 * index
        )
        assert 8 <= width <= 128 and 8 <= height <= 128 and offset + size <= len(data)
    for index in range(batches):
        tex, blend, flags, tint, layer, _, first_vertex, count, first_index, num = (
            struct.unpack_from("<HBBBBHIIII", data, batch_off + 24 * index)
        )
        assert first_vertex + count <= vertices and first_index + num <= indices
        for value in struct.unpack_from(
            "<" + "H" * num, data, index_off + first_index * 2
        ):
            assert value < count
if args.rebake:
    with (ROOT / "sdmc-out/clerks/rebake.log").open("w") as log:
        subprocess.run(
            [
                sys.executable,
                "-I",
                "-B",
                str(ROOT / "tools/pipeline/bake_clerk_karts.py"),
            ],
            cwd=ROOT,
            stdout=log,
            stderr=subprocess.STDOUT,
            check=True,
        )
    second = {
        path.name: hashlib.sha256(path.read_bytes()).hexdigest()
        for path in sorted(OUT.iterdir())
    }
    assert first == second, "fresh-process output mismatch"
    proof = {
        "files": len(first),
        "bytes": sum(path.stat().st_size for path in OUT.iterdir()),
        "sha256": first,
        "result": "byte-identical across independent baker processes",
    }
    (ROOT / "docs/design/clerk_reproducibility.json").write_text(
        json.dumps(proof, indent=2, sort_keys=True) + "\n"
    )
print(
    f"Clerk kart asset headers/indices/textures/budgets/hashes: {len(first)} files PASS"
)
if args.rebake:
    print(
        "Clerk kart fresh-process rebake: every output including manifest byte-identical PASS"
    )
