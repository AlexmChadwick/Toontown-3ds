#!/usr/bin/env python3
"""Host contract test for the baked factory table and native protocol surface."""
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[2]


def main():
    header = (ROOT / "source/net/dc_generated.h").read_text()
    client_h = (ROOT / "source/net/client.h").read_text()
    factory_h = (ROOT / "source/world/factory.h").read_text()
    assert "constexpr uint32_t kHash = 547155168u;" in header
    for name in ("DistributedFactory", "DistributedLift", "DistributedCrate",
                 "DistributedGoon", "DistributedStomper", "DistributedBattleFactory"):
        assert f"namespace {name}" in header, name
    for token in ("FactoryEntity", "sendFactoryCratePush", "sendFactoryGoonBattle"):
        assert token in client_h, token
    assert "class Factory" in factory_h
    table = ROOT / "sdmc-out/3ds/toontown3ds/data/factory/level_spec.bin"
    with table.open("rb") as f:
        assert f.read(4) == b"FCT1"
        version, count = struct.unpack("<HH", f.read(4))
        assert version == 1 and count >= 5
        record_size = struct.calcsize("<IHBxfff")
        assert len(f.read()) == count * record_size
    print("factory host test: PASS")


if __name__ == "__main__":
    main()
