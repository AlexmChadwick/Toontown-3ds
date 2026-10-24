#!/usr/bin/env python3
"""Host checks for the deterministic facility runtime contracts."""
from pathlib import Path
import struct
import tempfile
import math


def lerp(start, target, now, started, duration=1500):
    amount = max(0.0, min(1.0, (now - started) / duration))
    return start + (target - start) * amount


def read_spec(path, magic):
    data = Path(path).read_bytes()
    assert data[:4] == magic
    version, count = struct.unpack_from("<HH", data, 4)
    assert version == 1
    assert len(data) == 8 + count * 4
    return count


def main():
    assert math.isclose(lerp(0.0, 10.0, 1000, 0), 10.0 / 1.5)
    assert math.isclose(lerp(0.0, 10.0, 2000, 0), 10.0)
    assert math.isclose(lerp(0.0, 10.0, 0, 1000), 0.0)

    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        for name, magic, count in (("factory", b"FCT1", 2),
                                   ("mint", b"MNT1", 3),
                                   ("stage", b"STG1", 3)):
            path = root / f"{name}.bin"
            path.write_bytes(magic + struct.pack("<HH", 1, count) + b"\0" * (count * 4))
            assert read_spec(path, magic) == count

    rooms = [0, 1, 2, 3]
    current = 0
    for next_room in rooms[1:]:
        assert next_room == current + 1
        current = next_room
    assert current == 3
    print("facility runtime host checks passed")


if __name__ == "__main__":
    main()
