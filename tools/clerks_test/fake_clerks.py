"""Opt-in service-clerk fixtures for nettest's strict Panda/Astron fake server.

Offers and successful fish sales are fixtures, not capabilities of the pinned
stub pet/fish managers. Default nettest and production AI are unchanged.
"""

import struct
import ast
from pathlib import Path
from types import SimpleNamespace

# Read literal constants without importing NPCToons' application initialization.
_source = (
    Path(__file__).resolve().parents[2]
    / "ref/ref/open-toontown/toontown/toon/NPCToons.py"
)
_modes = {}
for _node in ast.parse(_source.read_text()).body:
    if isinstance(_node, ast.Assign):
        for _target in _node.targets:
            if isinstance(_target, ast.Name) and _target.id in (
                "SELL_MOVIE_NOFISH",
                "SELL_MOVIE_TROPHY",
            ):
                _modes[_target.id] = ast.literal_eval(_node.value)
NPCToons = SimpleNamespace(**_modes)


class ClerkFixture:
    def __init__(self, connection, server):
        self.c = connection
        self.s = server
        self.ids = {
            200000100: "DistributedNPCPetclerk",
            200000101: "DistributedNPCKartClerk",
            200000102: "DistributedNPCFisherman",
            200000103: "DistributedNPCPartyPerson",
        }
        self.busy = 0
        self.transaction = ""
        self.tickets = 10000
        self.owned = [-1] * 16
        self.fisher_visits = 0
        self.party_visits = 0

    def owns(self, number):
        return number in self.ids

    def field(self, number, name, value):
        self.c.send(self.s.set_field(number, self.s.CLS[self.ids[number]], name, value))

    def owner(self, name, value):
        self.c.send(self.s.set_field(self.c.avatar.doId, self.s.TOON, name, value))

    def movie(self, number, mode, extra="[]"):
        self.field(
            number,
            "setMovie",
            f"({mode},2013,{self.c.avatar.doId if mode else 0},{extra},{self.s.net_time16()})",
        )

    def enter(self):
        for index, (number, name) in enumerate(self.ids.items()):
            cls = self.s.CLS[name]
            data = self.s.required_blob(
                cls,
                False,
                {"setName": f'("Fixture {name}")', "setPositionIndex": f"({index})"},
            )
            self.c.send(
                self.s.msg(
                    self.s.CLIENT_ENTER_OBJECT_REQUIRED,
                    struct.pack(
                        "<IIIH", number, self.s.DISTRICT_ID, self.s.TTC, cls.getNumber()
                    )
                    + data,
                )
            )
        cls = self.s.CLS["NewsManager"]
        data = self.s.required_blob(cls, False, {})
        data += self.s.other_blob(cls, [('setHolidayIdList','([98])')])
        self.c.send(
            self.s.msg(
                self.s.CLIENT_ENTER_OBJECT_REQUIRED_OTHER,
                struct.pack(
                    "<IIIH", 200000104, self.s.DISTRICT_ID, self.s.TTC, cls.getNumber()
                )
                + data,
            )
        )
        self.owner("setMoney", "(1000)")
        self.owner("setBankMoney", "(5000)")
        self.owner("setTickets", f"({self.tickets})")
        self.owner("setFishTank", "([0,1],[0,0],[16,40])")

    async def handle(self, number, field_id, data):
        cls = self.s.CLS[self.ids[number]]
        field = cls.getFieldByIndex(field_id)
        if not field or not field.isClsend() or not self.s.unpack_ok(field, data):
            await self.c.eject(113, "illegal clerk field")
            return
        name = field.getName()
        self.s.log(f"clerks fixture {number}.{name} {data.hex()}")
        if name == "avatarEnter":
            if self.busy:
                self.field(number, "freeAvatar", "()")
                return
            self.busy = number
            self.transaction = ""
            if number == 200000100:
                seeds = (
                    "[]" if self.c.args.clerks == "stock" else "[1,1,42,42,31337,31337]"
                )
                self.field(number, "setPetSeeds", f"({seeds})")
            if number == 200000102:
                self.fisher_visits += 1
                if self.fisher_visits == 2:
                    self.movie(number, NPCToons.SELL_MOVIE_NOFISH)
                    self.clear(number)
                    return
                self.owner("setFishTank", "([0],[0],[32])")
            if number == 200000103:
                self.party_visits += 1
                if self.party_visits == 1:
                    self.movie(number, 6)
                    self.clear(number)
                    return
            self.movie(number, 1)
            return
        if self.busy != number:
            await self.c.eject(113, "clerk not busy with this toon")
            return
        if name == "petAdopted":
            index, name_id = struct.unpack("<BI", data)
            assert self.c.args.clerks == "offers" and index < 6 and name_id < 2713
            self.transaction = "adopt"
            self.owner("setPetId", "(501)")
        elif name == "petReturned":
            self.transaction = "return"
            self.owner("setPetId", "(0)")
        elif name in ("fishSold", "completeSale"):
            sell = name == "fishSold" or data[0]
            if sell:
                self.owner("setFishTank", "([],[],[])")
                self.owner("setMoney", "(1042)")
                self.movie(number, NPCToons.SELL_MOVIE_TROPHY, "[10,70]")
            else:
                self.movie(number, NPCToons.SELL_MOVIE_NOFISH)
            self.clear(number)
        elif name == "buyKart":
            costs = [100, 7500, 2500]
            body = data[0]
            assert body < 3 and self.tickets >= costs[body]
            self.tickets -= costs[body]
            self.owner("setTickets", f"({self.tickets})")
            self.owner("setKartBodyType", f"({body})")
        elif name == "buyAccessory":
            accessory = data[0]
            assert accessory == 0 and accessory not in self.owned
            self.tickets -= 200
            self.owned[0] = accessory
            self.owner("setTickets", f"({self.tickets})")
            self.owner("setKartAccessoriesOwned", f"({self.owned})")
            self.owner("setKartEngineBlockType", f"({accessory})")
        elif name == "transactionDone":
            self.movie(
                number,
                10
                if self.transaction == "adopt"
                else 9
                if self.transaction == "return"
                else 11
                if number == 200000100
                else 2,
            )
            self.clear(number)
        elif name == "answer":
            assert data == b"\0"
            self.movie(number, 4)
            self.clear(number)

    def clear(self, number):
        self.movie(number, 0)
        self.busy = 0
