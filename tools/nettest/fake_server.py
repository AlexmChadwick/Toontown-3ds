#!/usr/bin/env python3
"""Minimal fake Astron client agent + UberDOG + AI for unit-testing the 3DS
client's protocol code (source/net) on a PC.

It speaks the same wire protocol as Open Toontown on Astron for the subset the
client uses (AGENTS.md section 2, "Network"), packs every object with stock
Panda3D's DC packer (so field layouts come from the real .dc files, independently
of dcgen's tables), and is strict like Astron: wrong hash/version, a message
before HELLO, or an update to a field that is not clsend (or ownsend on our own
avatar) gets the client ejected.

World: one district ("Fake District"), a TimeManager, a toon "Walker" that runs
in a circle in zone 2000 and talks, a toon "Stander" that never moves (sent as
ENTER_OBJECT_REQUIRED without OTHER fields), and some objects of classes the
client has no table for (it must drop them). Several clients can connect; they
see each other's avatars and movement.

    .venv/bin/python -I tools/nettest/fake_server.py [--port 7298] [--drop-after S]
"""
import argparse
import asyncio
import json
import math
import os
import struct
import sys
import time

from panda3d.direct import DCFile, DCPacker

HERE = os.path.dirname(os.path.abspath(__file__))
ETC = os.path.join(HERE, '..', '..', 'ref', 'open-toontown', 'etc')

CLIENT_HELLO, CLIENT_HELLO_RESP, CLIENT_DISCONNECT, CLIENT_EJECT, CLIENT_HEARTBEAT = 1, 2, 3, 4, 5
CLIENT_OBJECT_SET_FIELD, CLIENT_OBJECT_LEAVING, CLIENT_OBJECT_LOCATION = 120, 132, 140
CLIENT_ENTER_OBJECT_REQUIRED, CLIENT_ENTER_OBJECT_REQUIRED_OTHER = 142, 143
CLIENT_ENTER_OBJECT_REQUIRED_OTHER_OWNER = 173
CLIENT_ADD_INTEREST, CLIENT_REMOVE_INTEREST, CLIENT_DONE_INTEREST_RESP = 200, 203, 204

LOGIN_MANAGER = 4670
GAME_ROOT, DISTRICTS_ZONE, UBER_ZONE, TTC = 4618, 3, 2, 2000
DISTRICT_ID = 200000000
STATS_ID = 200000001
TIME_MANAGER_ID = 200000002
WALKER_ID = 100000001
STANDER_ID = 100000002
JUNK_ID = 200000010
VERSION = 'sv1.0.47.38'

dc = DCFile()
for _n in ('otp.dc', 'toon.dc'):
    if not dc.read(os.path.join(ETC, _n)):
        sys.exit(f'cannot read {_n}')
DC_HASH = dc.getHash()
CLS = {dc.getClass(i).getName(): dc.getClass(i) for i in range(dc.getNumClasses())}
T0 = time.monotonic()


def log(msg):
    t = time.time()
    print(time.strftime('[%H:%M:%S', time.localtime(t)) + f'.{int(t * 1000) % 1000:03d}] server: {msg}',
          flush=True)


def net_time32():
    return int(math.floor((time.monotonic() - T0) * 100 + 0.5))


def net_time16():
    return ((net_time32() + 32768) & 0xffff) - 32768


def inherited(cls):
    return [cls.getInheritedField(i) for i in range(cls.getNumInheritedFields())]


def pack(cls, fname, text):
    f = cls.getFieldByName(fname)
    data = f.parseString(text)
    if not data and text != '()':
        raise ValueError(f'cannot pack {fname}{text}')
    return f.getNumber(), data


def required_blob(cls, owner, overrides):
    """Astron DistributedObject::append_required_data(client_only=True, also_owner)."""
    out = b''
    for f in inherited(cls):
        if f.asMolecularField() is not None or not f.isRequired():
            continue
        if not (f.isBroadcast() or f.isClrecv() or (owner and f.isOwnrecv())):
            continue
        if f.getName() in overrides:
            out += pack(cls, f.getName(), overrides[f.getName()])[1]
        else:
            out += f.getDefaultValue()
    return out


def other_blob(cls, items):
    out = struct.pack('<H', len(items))
    for fname, text in items:
        num, data = pack(cls, fname, text)
        out += struct.pack('<H', num) + data
    return out


def msg(mtype, payload=b''):
    return struct.pack('<H', mtype) + payload


def set_field(doId, cls, fname, text):
    num, data = pack(cls, fname, text)
    return msg(CLIENT_OBJECT_SET_FIELD, struct.pack('<IH', doId, num) + data)


def unpack_ok(field, data):
    p = DCPacker()
    p.setUnpackData(data)
    p.beginUnpack(field)
    p.unpackSkip()
    return p.endUnpack() and p.getNumUnpackedBytes() == len(data)


def format_field(field, data):
    try:
        return field.formatData(data)
    except Exception:  # noqa: BLE001 - debugging aid only
        return '<?>'


def dna_hex(b):
    # DC text syntax: <hex> is the packed value, so a blob literal carries its
    # own u16 length prefix.
    return '<' + struct.pack('<H', len(b)).hex() + b.hex() + '>'


TOON = CLS['DistributedToon']
WALKER_DNA = bytes([ord('t'), 4, 1, 0, 0, 3, 1, 3, 1, 2, 3, 6, 0, 6, 6])   # cat
STANDER_DNA = bytes([ord('t'), 8, 2, 2, 1, 1, 2, 1, 2, 1, 1, 9, 0, 9, 9])


class Avatar:
    def __init__(self, doId, name, dna, owner=None):
        self.doId, self.name, self.dna, self.owner = doId, name, dna, owner
        self.parent, self.zone = 0, 0
        self.ram = {}  # field number -> bytes (broadcast ram fields set by the owner)

    def required(self, owner_view):
        return required_blob(TOON, owner_view, {'setName': f'("{self.name}")',
                                                'setDNAString': f'({dna_hex(self.dna)})'})


class World:
    def __init__(self):
        self.clients = set()
        self.accounts = {}  # token -> [avatars]
        self.next_av = 100000100
        self.walker = Avatar(WALKER_ID, 'Walker', WALKER_DNA)
        self.walker.parent, self.walker.zone = DISTRICT_ID, TTC
        self.stander = Avatar(STANDER_ID, 'Stander', STANDER_DNA)
        self.stander.parent, self.stander.zone = DISTRICT_ID, TTC

    def watchers(self, parent, zone, exclude=None):
        return [c for c in self.clients if c is not exclude and c.has_interest(parent, zone)]


world = World()


class ClientConn:
    def __init__(self, reader, writer, args):
        self.r, self.w, self.args = reader, writer, args
        self.peer = writer.get_extra_info('peername')
        self.hello = False
        self.token = None
        self.avatar = None
        self.interests = {}  # handle -> (parent, zone)
        self.heartbeats = 0
        self.last_heartbeat = time.monotonic()
        self.stats = {'pos': 0, 'stop': 0, 'anim': 0, 'parent': 0, 'time': 0}
        self.closed = False

    # -- io
    def send(self, body):
        if self.closed:
            return
        self.w.write(struct.pack('<H', len(body)) + body)

    async def eject(self, code, reason):
        log(f'{self.peer} EJECT {code}: {reason}')
        self.send(msg(CLIENT_EJECT, struct.pack('<H', code) + struct.pack('<H', len(reason)) + reason.encode()))
        await self.w.drain()
        self.close()

    def close(self):
        if not self.closed:
            self.closed = True
            self.w.close()

    def has_interest(self, parent, zone):
        return (parent, zone) in self.interests.values()

    async def run(self):
        log(f'{self.peer} connected')
        world.clients.add(self)
        try:
            while not self.closed:
                hdr = await self.r.readexactly(2)
                (n,) = struct.unpack('<H', hdr)
                body = await self.r.readexactly(n)
                await self.handle(body)
                await self.w.drain()
        except (asyncio.IncompleteReadError, ConnectionResetError, BrokenPipeError):
            pass
        finally:
            world.clients.discard(self)
            self.leave_world()
            self.close()
            log(f'{self.peer} gone: heartbeats={self.heartbeats} stats={self.stats}')

    def leave_world(self):
        av = self.avatar
        if av and av.zone:
            for c in world.watchers(av.parent, av.zone, exclude=self):
                c.send(msg(CLIENT_OBJECT_LEAVING, struct.pack('<I', av.doId)))
        if av:
            av.parent = av.zone = 0

    # -- protocol
    async def handle(self, body):
        (mtype,) = struct.unpack_from('<H', body)
        p = body[2:]
        if not self.hello:
            if mtype != CLIENT_HELLO:
                return await self.eject(107, 'First datagram is not CLIENT_HELLO')
            (h,) = struct.unpack_from('<I', p)
            (vl,) = struct.unpack_from('<H', p, 4)
            ver = p[6:6 + vl].decode()
            if h != DC_HASH:
                return await self.eject(124, f'DC hash mismatch {h} != {DC_HASH}')
            if ver != VERSION:
                return await self.eject(122, f'version mismatch {ver}')
            self.hello = True
            log(f'{self.peer} HELLO ok (hash {h}, {ver})')
            self.send(msg(CLIENT_HELLO_RESP))
            return
        if mtype == CLIENT_HEARTBEAT:
            self.heartbeats += 1
            self.last_heartbeat = time.monotonic()
            log(f'{self.peer} heartbeat #{self.heartbeats}')
        elif mtype == CLIENT_DISCONNECT:
            log(f'{self.peer} CLIENT_DISCONNECT')
            self.close()
        elif mtype == CLIENT_OBJECT_SET_FIELD:
            doId, fnum = struct.unpack_from('<IH', p)
            await self.handle_field(doId, fnum, p[6:])
        elif mtype == CLIENT_ADD_INTEREST:
            ctx, handle, parent, zone = struct.unpack_from('<IHII', p)
            await self.add_interest(ctx, handle, parent, zone)
        elif mtype == CLIENT_REMOVE_INTEREST:
            ctx, handle = struct.unpack_from('<IH', p)
            self.interests.pop(handle, None)
            self.send(msg(CLIENT_DONE_INTEREST_RESP, struct.pack('<IH', ctx, handle)))
        elif mtype == CLIENT_OBJECT_LOCATION:
            doId, parent, zone = struct.unpack_from('<III', p)
            if not self.avatar or doId != self.avatar.doId:
                return await self.eject(113, f'relocate of object {doId} we do not own')
            self.leave_world()
            self.avatar.parent, self.avatar.zone = parent, zone
            log(f'{self.peer} avatar {doId} -> location ({parent}, {zone})')
            for c in world.watchers(parent, zone, exclude=self):
                c.send(enter_avatar(self.avatar))
        else:
            return await self.eject(108, f'unsupported message type {mtype}')

    async def handle_field(self, doId, fnum, data):
        if doId == LOGIN_MANAGER:
            cls = CLS['AstronLoginManager']
        elif self.avatar and doId == self.avatar.doId:
            cls = TOON
        elif doId == TIME_MANAGER_ID and self.avatar:
            cls = CLS['TimeManager']
        else:
            return await self.eject(113, f'update to unknown/unowned object {doId}')
        f = cls.getFieldByIndex(fnum)
        if f is None:
            return await self.eject(113, f'field {fnum} not in {cls.getName()}')
        own = cls is TOON
        if not (f.isClsend() or (own and f.isOwnsend())):
            return await self.eject(113, f'{cls.getName()}.{f.getName()} is not clsend/ownsend')
        if not unpack_ok(f, data):
            return await self.eject(113, f'{cls.getName()}.{f.getName()} malformed: {data.hex()}')
        if self.token is None and doId != LOGIN_MANAGER:
            return await self.eject(113, 'sandboxed client may only talk to the login manager')
        name = f.getName()
        if cls is TOON:
            self.on_own_field(f, data)
            return
        if cls.getName() == 'TimeManager':
            self.stats['time'] += 1
            (ctx,) = struct.unpack_from('<B', data)
            self.send(set_field(TIME_MANAGER_ID, CLS['TimeManager'], 'serverTime',
                                f'({ctx}, {net_time32()}, {int(time.time())})'))
            return
        lm = CLS['AstronLoginManager']
        if name == 'requestLogin':
            (n,) = struct.unpack_from('<H', data)
            self.token = data[2:2 + n].decode()
            log(f'{self.peer} login token "{self.token}"')
            resp = json.dumps({'returnCode': 0, 'respString': '', 'accountNumber': 1,
                               'access': 'FULL', 'serverTime': int(time.time())})
            self.send(set_field(LOGIN_MANAGER, lm, 'loginResponse', '(' + dna_hex(resp.encode()) + ')'))
        elif name == 'requestAvatarList':
            avs = world.accounts.get(self.token, [])
            items = ', '.join(f'{{{a.doId}, "{a.name}", {dna_hex(a.dna)}, {i}, 0}}' for i, a in enumerate(avs))
            log(f'{self.peer} avatar list: {len(avs)}')
            self.send(set_field(LOGIN_MANAGER, lm, 'avatarListResponse', f'([{items}])'))
        elif name == 'createAvatar':
            (n,) = struct.unpack_from('<H', data)
            dna = data[2:2 + n]
            slot = data[2 + n]
            if n != 15 or dna[0:1] != b't':
                return await self.eject(122, 'Invalid DNA specified!')
            av = Avatar(world.next_av, 'Toon', dna)
            world.next_av += 1
            world.accounts.setdefault(self.token, []).append(av)
            log(f'{self.peer} created avatar {av.doId} slot {slot} dna {dna.hex()}')
            self.send(set_field(LOGIN_MANAGER, lm, 'createAvatarResponse', f'({av.doId})'))
        elif name == 'requestPlayAvatar':
            (avId,) = struct.unpack_from('<I', data)
            avs = {a.doId: a for a in world.accounts.get(self.token, [])}
            if avId not in avs:
                return await self.eject(122, f'avatar {avId} is not on this account')
            for c in list(world.clients):
                if c is not self and c.avatar and c.avatar.doId == avId:
                    await c.eject(100, 'This account has been logged in elsewhere.')
            self.avatar = avs[avId]
            self.avatar.owner = self
            log(f'{self.peer} playing avatar {avId}')
            # Astron activates the toon at (0, 0) and sends the owner view.
            payload = struct.pack('<IIIH', avId, 0, 0, TOON.getNumber()) + self.avatar.required(True)
            payload += other_blob(TOON, [('setAnimState', '("neutral", 1.0, 0)')])
            self.send(msg(CLIENT_ENTER_OBJECT_REQUIRED_OTHER_OWNER, payload))

    def on_own_field(self, f, data):
        name = f.getName()
        key = {'setSmPosHpr': 'pos', 'setSmStop': 'stop', 'setAnimState': 'anim',
               'setParent': 'parent'}.get(name)
        if key:
            self.stats[key] += 1
            if self.stats[key] <= 3 or self.stats[key] % 25 == 0:
                log(f'{self.peer} avatar {name}{format_field(f, data)} (#{self.stats[key]})')
        if f.isRam():
            m = f.asMolecularField()
            if m is None:
                self.avatar.ram[f.getNumber()] = data
            else:  # store molecular updates as their atomics, like a state server
                off = 0
                for i in range(m.getNumAtomics()):
                    a = m.getAtomic(i)
                    p = DCPacker()
                    p.setUnpackData(data[off:])
                    p.beginUnpack(a)
                    p.unpackSkip()
                    used = p.getNumUnpackedBytes()
                    p.endUnpack()
                    self.avatar.ram[a.getNumber()] = data[off:off + used]
                    off += used
        av = self.avatar
        if f.isBroadcast() and av.zone:
            for c in world.watchers(av.parent, av.zone, exclude=self):
                c.send(msg(CLIENT_OBJECT_SET_FIELD, struct.pack('<IH', av.doId, f.getNumber()) + data))

    async def add_interest(self, ctx, handle, parent, zone):
        self.interests[handle] = (parent, zone)
        log(f'{self.peer} interest #{handle} ctx {ctx} -> ({parent}, {zone})')
        if (parent, zone) == (GAME_ROOT, DISTRICTS_ZONE):
            district = CLS['ToontownDistrict']
            self.send(msg(CLIENT_ENTER_OBJECT_REQUIRED, struct.pack('<IIIH', DISTRICT_ID, parent, zone,
                                                                     district.getNumber()) +
                          required_blob(district, False, {'setName': '("Fake District")',
                                                          'setAvailable': '(1)'})))
            stats = CLS['ToontownDistrictStats']
            self.send(msg(CLIENT_ENTER_OBJECT_REQUIRED, struct.pack('<IIIH', STATS_ID, parent, zone,
                                                                     stats.getNumber()) +
                          required_blob(stats, False, {'settoontownDistrictId': f'({DISTRICT_ID})',
                                                       'setAvatarCount': '(3)'})))
        elif (parent, zone) == (DISTRICT_ID, UBER_ZONE):
            tm = CLS['TimeManager']
            self.send(msg(CLIENT_ENTER_OBJECT_REQUIRED, struct.pack('<IIIH', TIME_MANAGER_ID, parent, zone,
                                                                     tm.getNumber())))
        elif (parent, zone) == (DISTRICT_ID, TTC):
            self.send(enter_walker())
            self.send(msg(CLIENT_ENTER_OBJECT_REQUIRED, struct.pack('<IIIH', STANDER_ID, DISTRICT_ID, TTC,
                                                                     TOON.getNumber()) +
                          world.stander.required(False)))
            for junk in ('DistributedButterfly', 'DistributedTrolley', 'DistributedPartyGate'):
                if junk in CLS:
                    cls = CLS[junk]
                    self.send(msg(CLIENT_ENTER_OBJECT_REQUIRED, struct.pack('<IIIH', JUNK_ID, DISTRICT_ID, TTC,
                                                                             cls.getNumber()) +
                                  required_blob(cls, False, {})))
                    f = [x for x in inherited(cls) if x.isBroadcast() and x.asMolecularField() is None][0]
                    self.send(msg(CLIENT_OBJECT_SET_FIELD, struct.pack('<IH', JUNK_ID, f.getNumber()) +
                                  f.getDefaultValue()))
                    break
            for c in world.clients:
                if c is not self and c.avatar and (c.avatar.parent, c.avatar.zone) == (parent, zone):
                    self.send(enter_avatar(c.avatar))
        self.send(msg(CLIENT_DONE_INTEREST_RESP, struct.pack('<IH', ctx, handle)))


def enter_avatar(av):
    payload = struct.pack('<IIIH', av.doId, av.parent, av.zone, TOON.getNumber()) + av.required(False)
    payload += struct.pack('<H', len(av.ram))
    for num, data in av.ram.items():
        payload += struct.pack('<H', num) + data
    return msg(CLIENT_ENTER_OBJECT_REQUIRED_OTHER, payload)


def walker_pose(t):
    a = t * 2 * math.pi / 12.0
    x, y = 10 * math.cos(a), 10 * math.sin(a)
    h = (math.degrees(a) + 180.0) % 360.0   # facing along the circle
    return x, y, 4.0, h


def enter_walker():
    x, y, z, h = walker_pose(time.monotonic() - T0)
    payload = struct.pack('<IIIH', WALKER_ID, DISTRICT_ID, TTC, TOON.getNumber()) + world.walker.required(False)
    payload += other_blob(TOON, [('setAnimState', f'("run", 1.0, {net_time16()})'),
                                 ('setParent', '(2)'),
                                 ('setComponentX', f'({x:.1f})'), ('setComponentY', f'({y:.1f})'),
                                 ('setComponentZ', f'({z:.1f})'), ('setComponentH', f'({h:.1f})'),
                                 ('setComponentT', f'({net_time16()})')])
    return msg(CLIENT_ENTER_OBJECT_REQUIRED_OTHER, payload)


async def walker_loop():
    tick = 0
    while True:
        await asyncio.sleep(0.2)
        tick += 1
        x, y, z, h = walker_pose(time.monotonic() - T0)
        out = [set_field(WALKER_ID, TOON, 'setSmPosHpr', f'({x:.2f}, {y:.2f}, {z:.2f}, {h:.2f}, 0, 0, {net_time16()})')]
        if tick % 30 == 0:
            out.append(set_field(WALKER_ID, TOON, 'setTalk',
                                 f'(0, 0, "Walker", "Hello from the fake server! tick {tick}", [], 0)'))
        if tick % 50 == 0:
            out.append(set_field(WALKER_ID, TOON, 'setAnimState', f'("walk", 0.5, {net_time16()})'))
        if tick % 50 == 25:
            out.append(set_field(WALKER_ID, TOON, 'setAnimState', f'("run", 1.0, {net_time16()})'))
        for c in world.watchers(DISTRICT_ID, TTC):
            if c.avatar and c.avatar.zone == TTC:
                for m in out:
                    c.send(m)


async def heartbeat_watch():
    while True:
        await asyncio.sleep(1)
        for c in list(world.clients):
            if c.hello and time.monotonic() - c.last_heartbeat > 60:
                await c.eject(345, 'Server timed out while waiting for heartbeat.')


async def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', type=int, default=7298)
    ap.add_argument('--drop-after', type=float, default=0,
                    help='close the first in-zone connection after this many seconds (reconnect test)')
    args = ap.parse_args()
    dropped = [False]

    async def on_client(reader, writer):
        c = ClientConn(reader, writer, args)
        if args.drop_after and not dropped[0]:
            dropped[0] = True

            async def dropper():
                await asyncio.sleep(args.drop_after)
                log(f'{c.peer} dropping connection (--drop-after)')
                c.close()
            asyncio.ensure_future(dropper())
        await c.run()

    server = await asyncio.start_server(on_client, '0.0.0.0', args.port)
    log(f'listening on {args.port} (dc hash {DC_HASH})')
    asyncio.ensure_future(walker_loop())
    asyncio.ensure_future(heartbeat_watch())
    async with server:
        await server.serve_forever()


if __name__ == '__main__':
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
