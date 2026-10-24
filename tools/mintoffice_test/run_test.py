#!/usr/bin/env python3
from pathlib import Path
import struct
ROOT = Path(__file__).resolve().parents[2]
def read(path, magic):
    data = path.read_bytes()
    assert data[:4] == magic and struct.unpack_from("<H", data, 4)[0] == 1
    count = struct.unpack_from("<H", data, 6)[0]
    return [struct.unpack_from("<HBB", data, 8 + i * 4) for i in range(count)]
mint = read(ROOT/"sdmc-out/3ds/toontown3ds/data/mint/room_spec.bin", b"MNT1")
stage = read(ROOT/"sdmc-out/3ds/toontown3ds/data/stage/room_spec.bin", b"STG1")
assert mint[0][0] == 0 and mint[-1][0] == 25 and len(mint) == 26
assert stage[0][0] == 0 and 100 in {x[0] for x in stage}
header = (ROOT/"source/net/dc_generated.h").read_text()
assert "547155168u" in header and "namespace DistributedMint" in header
print("mint rooms:", len(mint), "stage rooms:", len(stage), "dc hash: ok")
