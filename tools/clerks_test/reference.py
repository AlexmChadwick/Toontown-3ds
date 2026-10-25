from pathlib import Path
import random
import struct
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
from upstream import PetUtil, PetNameGenerator

out = ROOT / "sdmc-out/clerks-test"
out.mkdir(parents=True, exist_ok=True)
names = PetNameGenerator.PetNameGenerator()
records = []
rng = random.Random(349)
for zone in (2000, 1000, 5000, 4000, 3000, 9000):
    for seed in [0, 1, 42, 2147483647, 4294967295] + [
        rng.getrandbits(32) for _ in range(250)
    ]:
        name, dna, trait = PetUtil.getPetInfoFromSeed(seed, zone)
        cost = PetUtil.getPetCostFromSeed(seed, zone)
        records.append(
            struct.pack(
                "<II9bIII", seed, zone, *dna, trait, names.returnUniqueID(name), cost
            )
        )
(out / "pets.bin").write_bytes(b"".join(records))
print("Pinned PetUtil vectors:", len(records))

from upstream import KartDNA, FishTank, FishBase
from toontown.fishing import FishGlobals
import builtins
from types import SimpleNamespace

FishGlobals.getBase = lambda: SimpleNamespace(cr=None)
builtins.simbase = SimpleNamespace(
    air=SimpleNamespace(holidayManager=SimpleNamespace(currentHolidays=[]))
)
records = []
for _ in range(5000):
    body = rng.randrange(-1, 3)
    current = rng.randrange(-1, 3)
    accessory = rng.randrange(92)
    owned = rng.sample(range(92), rng.randrange(17))
    tickets = rng.randrange(9000)
    available = KartDNA.getAccessoryDictFromOwned(owned)
    buy_accessory = (
        body >= 0
        and len(owned) < 16
        and accessory in sum(available.values(), [])
        and KartDNA.getAccCost(accessory) <= tickets
    )
    buy_body = body >= 0 and body != current and KartDNA.getKartCost(body) <= tickets
    records.append(
        struct.pack(
            "<iiiI16bii",
            body,
            current,
            accessory,
            tickets,
            *(owned + [-1] * (16 - len(owned))),
            buy_body,
            buy_accessory,
        )
    )
(out / "karts.bin").write_bytes(b"".join(records))
print("Pinned KartDNA catalogue/ownership vectors:", len(records))
records = []
fish = [
    (genus, species)
    for genus, entries in FishGlobals.__dict__["__fishDict"].items()
    for species in range(len(entries))
]
for _ in range(1000):
    tank = FishTank.FishTank()
    values = []
    for _ in range(rng.randrange(21)):
        genus, species = rng.choice(fish)
        weight = rng.randrange(1, 321)
        tank.addFish(FishBase.FishBase(genus, species, weight))
        values.extend((genus, species, weight))
    records.append(
        struct.pack("<ii", len(tank), tank.getTotalValue())
        + struct.pack("<" + "i" * len(values), *values)
    )
(out / "fish.bin").write_bytes(b"".join(records))
print("Pinned FishTank sale-total vectors:", len(records))
