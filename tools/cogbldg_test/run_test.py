#!/usr/bin/env python3
"""Host checks for the Cog-building wire/state contract."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
DC = ROOT / "source/net/dc_generated.h"
CLIENT_H = ROOT / "source/net/client.h"
DOC = ROOT / "docs/design/cogbuildings.md"


def main():
    generated = DC.read_text()
    header = CLIENT_H.read_text()
    doc = DOC.read_text()
    assert "constexpr uint32_t kHash = 547155168u;" in generated
    for namespace in ("DistributedBuilding", "DistributedSuitInterior", "DistributedElevator",
                      "DistributedElevatorInt", "DistributedBattleBldg"):
        assert f"namespace {namespace}" in generated
    for state in ("toon", "suit", "becomingSuit", "becomingToon"):
        assert state in header or state in doc
    assert "sendCogElevatorBoard" in header
    assert "sendCogInteriorReserveJoinDone" in header

    def building_state(value):
        return {
            "toon": "Toon", "suit": "Suit", "becomingSuit": "BecomingSuit",
            "becomingToon": "BecomingToon",
        }.get(value, "Other")

    assert building_state("suit") == "Suit"
    assert building_state("becomingToon") == "BecomingToon"
    assert building_state("unknown") == "Other"
    assert len(re.findall(r"k_fillSlot[0-7]", generated)) >= 8
    print("cog building host test: PASS")


if __name__ == "__main__":
    main()
