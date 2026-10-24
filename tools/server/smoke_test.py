#!/usr/bin/env python3
"""End-to-end smoke test of an Astron/Open Toontown server, speaking the raw
client protocol exactly like the 3DS client must (no ClientRepository).

    tools/server/.venv/bin/python -I tools/server/smoke_test.py [--host 127.0.0.1]
        [--port 7198] [--token smoketest] [--zone 2000] [--linger 5] [--move]

Uses panda3d.direct.DCFile (from the venv) only to pack/unpack field values in
DC order; all framing / message ids are done by hand so this doubles as a
protocol reference. Exit code 0 = whole flow succeeded.

Flow: HELLO -> requestLogin -> interest(4618,3) [districts] -> requestAvatarList
-> createAvatar (if slot empty) -> requestPlayAvatar -> owner view of our toon
-> OBJECT_LOCATION(av, shard, zone) -> interest(shard,2) -> interest(shard,zone)
-> (optional) send setAnimState/setSmPosHpr -> linger and print what arrives.
"""
import argparse
import json
import os
import socket
import struct
import sys
import threading
import time

from panda3d.core import Filename
from panda3d.direct import DCFile, DCPacker

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
ETC = os.path.join(ROOT, "ref", "open-toontown", "etc")

# Astron client message types (direct/distributed/MsgTypes.py, Astron section)
CLIENT_HELLO = 1
CLIENT_HELLO_RESP = 2
CLIENT_DISCONNECT = 3
CLIENT_EJECT = 4
CLIENT_HEARTBEAT = 5
CLIENT_OBJECT_SET_FIELD = 120
CLIENT_OBJECT_SET_FIELDS = 121
CLIENT_OBJECT_LEAVING = 132
CLIENT_OBJECT_LEAVING_OWNER = 161
CLIENT_ENTER_OBJECT_REQUIRED = 142
CLIENT_ENTER_OBJECT_REQUIRED_OTHER = 143
CLIENT_ENTER_OBJECT_REQUIRED_OWNER = 172
CLIENT_ENTER_OBJECT_REQUIRED_OTHER_OWNER = 173
CLIENT_OBJECT_LOCATION = 140
CLIENT_ADD_INTEREST = 200
CLIENT_ADD_INTEREST_MULTIPLE = 201
CLIENT_REMOVE_INTEREST = 203
CLIENT_DONE_INTEREST_RESP = 204

LOGIN_MANAGER_ID = 4670
GAME_ROOT_ID = 4618       # OTP_DO_ID_TOONTOWN
DISTRICT_ZONE = 3         # OTP_ZONE_ID_DISTRICTS
UBER_ZONE = 2
DC_HASH = 547155168
VERSION = "sv1.0.47.38"

# A valid 15-byte ToonDNA net string: 't', head dls(0), torso ms(1), legs m(1),
# male(1), shirt 0/0, sleeve 0/0, shorts 0/0, arm 5, glove 0 (must be 0),
# leg 5, head 5.
TEST_DNA = b"t" + bytes([0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 5, 0, 5, 5])


class DC:
    def __init__(self):
        self.f = DCFile()
        for name in ("otp.dc", "toon.dc"):  # same order as astrond.yml
            if not self.f.read(Filename.fromOsSpecific(os.path.join(ETC, name))):
                raise SystemExit("cannot read " + name)
        self.hash = self.f.getHash()
        self.by_class = {self.f.getClass(i).getNumber(): self.f.getClass(i)
                         for i in range(self.f.getNumClasses())}

    def cls(self, name):
        return self.f.getClassByName(name)

    def field(self, cls_name, field_name):
        return self.cls(cls_name).getFieldByName(field_name)

    def pack(self, field, args):
        p = DCPacker()
        p.beginPack(field)
        field.packArgs(p, list(args))
        if not p.endPack():
            raise ValueError("pack failed for %s %r" % (field.getName(), args))
        return bytes(p.getBytes())

    @staticmethod
    def unpack_one(field, data, offset):
        """Unpack one field value from data[offset:]; returns (value, new_offset)."""
        p = DCPacker()
        p.setUnpackData(data[offset:])
        p.beginUnpack(field)
        try:
            val = p.unpackObject()
        except Exception as e:
            raise ValueError("unpack of %s failed at offset %d: %r" % (field.getName(), offset, e))
        if not p.endUnpack():
            raise ValueError("unpack failed for %s" % field.getName())
        return val, offset + p.getNumUnpackedBytes()

    def required_fields(self, dclass, owner=False):
        """Required fields in the order Astron sends them to a client.

        Astron's state server (DistributedObject::append_required_data with
        client_only=true) sends, in field-id order, every non-molecular
        `required` field that is also `broadcast` or `clrecv`, plus `ownrecv`
        ones when the receiver is the owner (ENTER_*_OWNER messages)."""
        out = []
        for i in range(dclass.getNumInheritedFields()):
            fld = dclass.getInheritedField(i)
            if not fld.isRequired() or fld.asMolecularField() is not None:
                continue
            if fld.isBroadcast() or fld.isClrecv() or (owner and fld.isOwnrecv()):
                out.append(fld)
        return out


class Conn:
    def __init__(self, host, port, dc, verbose):
        self.sock = socket.create_connection((host, port), timeout=15)
        self.buf = b""
        self.dc = dc
        self.verbose = verbose
        self.lock = threading.Lock()
        self.alive = True
        self.objects = {}   # doId -> dict(cls, parent, zone, fields)
        self.ctx = 0

    def send(self, body):
        with self.lock:
            self.sock.sendall(struct.pack("<H", len(body)) + body)

    def recv(self, timeout=15.0):
        self.sock.settimeout(timeout)
        while len(self.buf) < 2 or len(self.buf) < 2 + struct.unpack_from("<H", self.buf)[0]:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise ConnectionError("server closed the connection")
            self.buf += chunk
        n = struct.unpack_from("<H", self.buf)[0]
        body, self.buf = self.buf[2:2 + n], self.buf[2 + n:]
        return struct.unpack_from("<H", body)[0], body[2:]

    def heartbeat_loop(self):
        while self.alive:
            time.sleep(10)
            if self.alive:
                try:
                    self.send(struct.pack("<H", CLIENT_HEARTBEAT))
                except OSError:
                    return

    # -- outgoing -------------------------------------------------------
    def set_field(self, do_id, cls_name, field_name, *args):
        fld = self.dc.field(cls_name, field_name)
        self.send(struct.pack("<HIH", CLIENT_OBJECT_SET_FIELD, do_id, fld.getNumber())
                  + self.dc.pack(fld, args))

    def add_interest(self, parent, zone, handle):
        self.ctx += 1
        self.send(struct.pack("<HIHII", CLIENT_ADD_INTEREST, self.ctx, handle, parent, zone))
        return self.ctx

    # -- incoming -------------------------------------------------------
    def handle(self, mtype, d):
        """Decode one message; returns a tuple describing it."""
        if mtype == CLIENT_OBJECT_SET_FIELD:
            do_id, fid = struct.unpack_from("<IH", d)
            fld = self.dc.f.getFieldByIndex(fid)
            val, _ = self.dc.unpack_one(fld, d, 6)
            obj = self.objects.get(do_id)
            if obj is not None:
                obj["fields"][fld.getName()] = val
            return ("set_field", do_id, fld.getName(), val)
        if mtype in (CLIENT_ENTER_OBJECT_REQUIRED, CLIENT_ENTER_OBJECT_REQUIRED_OTHER,
                     CLIENT_ENTER_OBJECT_REQUIRED_OWNER, CLIENT_ENTER_OBJECT_REQUIRED_OTHER_OWNER):
            do_id, parent, zone, cls_id = struct.unpack_from("<IIIH", d)
            dclass = self.dc.by_class[cls_id]
            owner = mtype in (CLIENT_ENTER_OBJECT_REQUIRED_OWNER, CLIENT_ENTER_OBJECT_REQUIRED_OTHER_OWNER)
            off = 14
            fields = {}
            for fld in self.dc.required_fields(dclass, owner):
                fields[fld.getName()], off = self.dc.unpack_one(fld, d, off)
            if mtype in (CLIENT_ENTER_OBJECT_REQUIRED_OTHER, CLIENT_ENTER_OBJECT_REQUIRED_OTHER_OWNER):
                (n,) = struct.unpack_from("<H", d, off); off += 2
                for _ in range(n):
                    (fid,) = struct.unpack_from("<H", d, off); off += 2
                    fld = self.dc.f.getFieldByIndex(fid)
                    fields[fld.getName()], off = self.dc.unpack_one(fld, d, off)
            if off != len(d):
                raise ValueError("enter %d: %d trailing bytes" % (do_id, len(d) - off))
            self.objects[do_id] = dict(cls=dclass.getName(), parent=parent, zone=zone,
                                       fields=fields, owner=owner)
            return ("enter", do_id, dclass.getName(), parent, zone, owner)
        if mtype == CLIENT_DONE_INTEREST_RESP:
            ctx, handle = struct.unpack_from("<IH", d)
            return ("done_interest", ctx, handle)
        if mtype == CLIENT_OBJECT_LOCATION:
            do_id, parent, zone = struct.unpack_from("<III", d)
            if do_id in self.objects:
                self.objects[do_id].update(parent=parent, zone=zone)
            return ("location", do_id, parent, zone)
        if mtype in (CLIENT_OBJECT_LEAVING, CLIENT_OBJECT_LEAVING_OWNER):
            (do_id,) = struct.unpack_from("<I", d)
            obj = self.objects.pop(do_id, None)
            return ("leaving", do_id, obj and obj["cls"])
        if mtype == CLIENT_EJECT:
            code, = struct.unpack_from("<H", d)
            (slen,) = struct.unpack_from("<H", d, 2)
            raise SystemExit("EJECTED code=%d reason=%r" % (code, d[4:4 + slen].decode()))
        return ("msg", mtype, d.hex())

    def pump_until(self, pred, timeout, what):
        end = time.time() + timeout
        while True:
            left = end - time.time()
            if left <= 0:
                raise SystemExit("timeout waiting for " + what)
            try:
                ev = self.handle(*self.recv(left))
            except socket.timeout:
                raise SystemExit("timeout waiting for " + what)
            self.log(ev)
            if pred(ev):
                return ev

    def log(self, ev):
        if ev[0] == "enter":
            o = self.objects[ev[1]]
            name = o["fields"].get("setName", ("",))[0] if o["fields"].get("setName") else ""
            print("  <- ENTER%s %-28s doId=%-10d loc=(%d,%d) %s" % (
                "_OWNER" if ev[5] else "", ev[2], ev[1], ev[3], ev[4], repr(name) if name else ""))
            if self.verbose:
                for k, v in o["fields"].items():
                    s = repr(v)
                    print("       %s = %s" % (k, s if len(s) < 120 else s[:117] + "..."))
        elif ev[0] == "set_field":
            s = repr(ev[3])
            print("  <- SET_FIELD doId=%d %s %s" % (ev[1], ev[2], s if len(s) < 160 else s[:157] + "..."))
        else:
            print("  <-", *ev)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=7198)
    ap.add_argument("--token", default="smoketest")
    ap.add_argument("--zone", type=int, default=2000)
    ap.add_argument("--linger", type=float, default=5.0)
    ap.add_argument("--move", action="store_true", help="walk in a circle while lingering")
    ap.add_argument("--dna", help="15-byte ToonDNA net string as hex, used when creating the toon")
    ap.add_argument("--center", default="0,0", help="circle centre x,y for --move")
    ap.add_argument("--radius", type=float, default=40.0, help="circle radius for --move")
    ap.add_argument("--speed", type=float, default=0.5, help="angular speed (rad/s) for --move")
    ap.add_argument("--z", type=float, default=4.0, help="height for --move")
    ap.add_argument("-v", "--verbose", action="store_true", help="dump every field")
    a = ap.parse_args()

    dc = DC()
    print("DC hash %d (expected %d)" % (dc.hash, DC_HASH))
    assert dc.hash == DC_HASH
    c = Conn(a.host, a.port, dc, a.verbose)
    print("connected to %s:%d" % (a.host, a.port))

    # 1. HELLO
    v = VERSION.encode()
    c.send(struct.pack("<HIH", CLIENT_HELLO, DC_HASH, len(v)) + v)
    mtype, _ = c.recv()
    assert mtype == CLIENT_HELLO_RESP, "expected HELLO_RESP, got %d" % mtype
    print("  <- CLIENT_HELLO_RESP")
    threading.Thread(target=c.heartbeat_loop, daemon=True).start()

    # 2. login
    c.set_field(LOGIN_MANAGER_ID, "AstronLoginManager", "requestLogin", a.token)
    ev = c.pump_until(lambda e: e[0] == "set_field" and e[2] == "loginResponse", 20, "loginResponse")
    resp = json.loads(ev[3][0])
    assert resp.get("returnCode") == 0, resp
    print("login OK: userName=%s accountNumber=%s" % (resp.get("userName"), resp.get("accountNumber")))

    # 3. districts
    ctx = c.add_interest(GAME_ROOT_ID, DISTRICT_ZONE, 1)
    c.pump_until(lambda e: e[0] == "done_interest" and e[1] == ctx, 20, "district interest")
    districts = [(d, o) for d, o in c.objects.items() if o["cls"] == "ToontownDistrict"]
    assert districts, "no ToontownDistrict under (4618,3)"
    shard, dobj = districts[0]
    print("district %d %r available=%r" % (shard, dobj["fields"].get("setName"), dobj["fields"].get("setAvailable")))

    # 4. avatars
    c.set_field(LOGIN_MANAGER_ID, "AstronLoginManager", "requestAvatarList")
    ev = c.pump_until(lambda e: e[0] == "set_field" and e[2] == "avatarListResponse", 20, "avatarListResponse")
    avs = ev[3][0]
    print("avatar list: %r" % (avs,))
    if avs:
        av_id = avs[0][0]
    else:
        dna = bytes.fromhex(a.dna) if a.dna else TEST_DNA
        c.set_field(LOGIN_MANAGER_ID, "AstronLoginManager", "createAvatar", dna, 0)
        ev = c.pump_until(lambda e: e[0] == "set_field" and e[2] == "createAvatarResponse", 20, "createAvatarResponse")
        av_id = ev[3][0]
        print("created avatar %d" % av_id)

    # 5. play
    c.set_field(LOGIN_MANAGER_ID, "AstronLoginManager", "requestPlayAvatar", av_id)
    c.pump_until(lambda e: e[0] == "enter" and e[1] == av_id and e[5], 20, "owner view of our toon")
    me = c.objects[av_id]
    print("playing %d %r (%d required+other fields)" % (av_id, me["fields"]["setName"][0], len(me["fields"])))

    # 6. enter zone: location first, then uber zone, then the zone itself
    c.send(struct.pack("<HIII", CLIENT_OBJECT_LOCATION, av_id, shard, a.zone))
    ctx = c.add_interest(shard, UBER_ZONE, 2)
    c.pump_until(lambda e: e[0] == "done_interest" and e[1] == ctx, 30, "uber zone interest")
    ctx = c.add_interest(shard, a.zone, 3)
    c.pump_until(lambda e: e[0] == "done_interest" and e[1] == ctx, 30, "zone %d interest" % a.zone)

    # 7. linger
    print("in zone %d; lingering %.1fs" % (a.zone, a.linger))
    end = time.time() + a.linger
    t0 = time.time()
    next_move = 0
    while time.time() < end:
        if a.move and time.time() >= next_move:
            import math
            ang = (time.time() - t0) * a.speed
            cx, cy = (float(v) for v in a.center.split(","))
            x, y = cx + a.radius * math.cos(ang), cy + a.radius * math.sin(ang)
            ts = ((int(time.time() * 100) & 0xFFFF) ^ 0x8000) - 0x8000  # signed int16 centiseconds
            if next_move == 0:
                c.set_field(av_id, "DistributedToon", "setAnimState", "run", 1.0, ts)
            c.set_field(av_id, "DistributedSmoothNode", "setSmPosHpr", x, y, a.z,
                        math.degrees(ang) + 180, 0, 0, ts)
            next_move = time.time() + 0.2
        try:
            c.log(c.handle(*c.recv(0.2)))
        except socket.timeout:
            pass

    print("\nobjects visible (%d):" % len(c.objects))
    by_cls = {}
    for d, o in sorted(c.objects.items()):
        by_cls.setdefault(o["cls"], []).append((d, o))
    for cls, lst in sorted(by_cls.items()):
        print("  %-32s x%-3d %s" % (cls, len(lst), " ".join("%d@(%d,%d)" % (d, o["parent"], o["zone"]) for d, o in lst[:6])))
    c.alive = False
    c.send(struct.pack("<H", CLIENT_DISCONNECT))
    c.sock.close()
    print("SMOKE TEST OK")


if __name__ == "__main__":
    main()
