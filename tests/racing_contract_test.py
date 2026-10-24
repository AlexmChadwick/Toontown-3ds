"""Host checks for the racing contract; no Panda3D or network is required."""
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]

def order(progress):
    return sorted(range(len(progress)), key=lambda i: (-progress[i][0], -progress[i][1], progress[i][2]))

def test_lap_and_position_order():
    assert order([(2, 0.4, 20), (1, 0.9, 2), (2, 0.4, 10)]) == [2, 0, 1]

def test_track_ids_and_reverse_variants_are_documented():
    text = (ROOT / "docs/design/racing.md").read_text()
    for token in ("RT_Speedway_1_rev", "RT_Rural_1_rev", "RT_Urban_1_rev", "tickets"):
        assert token in text

def test_dc_hash_and_racing_classes():
    header = (ROOT / "source/net/dc_generated.h").read_text()
    assert "constexpr uint32_t kHash = 547155168u;" in header
    for cls in ("DistributedVehicle", "DistributedRace", "DistributedStartingBlock"):
        assert f"namespace {cls}" in header

if __name__ == "__main__":
    test_lap_and_position_order()
    test_track_ids_and_reverse_variants_are_documented()
    test_dc_hash_and_racing_classes()
    print("racing contract: ok")
